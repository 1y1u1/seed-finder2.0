#include "uikit.h"

#include <QDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QVBoxLayout>

namespace ui {

// ---------------------------------------------------------------------------
//  对话框外壳
// ---------------------------------------------------------------------------
QVBoxLayout *prepareDialog(QDialog *dlg, const QString &title)
{
    dlg->setWindowTitle(title);
    // 去掉标题栏上的"这是什么?"按钮 (理由见头文件)
    dlg->setWindowFlags(dlg->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    // 最小宽度统一: 四个对话框的按钮行宽度本来就不一样, 各自 setMinimumWidth
    // 会出现"这个窗口能压到很窄、那个压不下去"的不一致。
    // 取 max 而不是直接覆盖: 使用说明/关于页在调用之前已经按内容设了更大的最小
    // 尺寸, 直接覆盖会把那个下限降回去, 窗口就能被压到内容放不下。
    if (dlg->minimumWidth() < kDialogMinWidth) dlg->setMinimumWidth(kDialogMinWidth);

    QVBoxLayout *root = new QVBoxLayout(dlg);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);
    return root;
}

// ---------------------------------------------------------------------------
//  卡片
// ---------------------------------------------------------------------------
QLabel *cardTitle(const QString &text)
{
    QLabel *l = new QLabel(text);
    l->setObjectName("CardTitle");
    return l;
}

QFrame *makeCard(QWidget *titleRow, QWidget *child)
{
    QFrame *card = new QFrame;
    card->setObjectName("Card");
    QVBoxLayout *lay = new QVBoxLayout(card);
    // 内边距与间距: 主界面四张卡必须用同一套数字, 否则卡片顶边、按钮位置
    // 会相差 1~2px, 一排看过去就是歪的。
    lay->setContentsMargins(8, 2, 8, 2);
    lay->setSpacing(3);
    lay->addWidget(titleRow);
    lay->addWidget(child, 1);
    return card;
}

QFrame *makeCard(const QString &title, QWidget *child)
{
    return makeCard(cardTitle(title), child);
}

// ---------------------------------------------------------------------------
//  按钮行
// ---------------------------------------------------------------------------
QPushButton *addOkCancelRow(QVBoxLayout *root, QDialog *owner, const QString &okText,
                            QWidget *leading)
{
    QHBoxLayout *btns = new QHBoxLayout;
    root->addLayout(btns);

    // 左侧的自定义按钮 (只有"编辑出怪条件"用到, 放"全部清空")
    if (leading) btns->addWidget(leading);
    btns->addStretch(1);

    QPushButton *cancel = new QPushButton(QString::fromUtf8("取消"), owner);
    // 不参与默认按钮竞争: 输入框里敲回车应当走"确定", 不该悄悄取消掉
    cancel->setAutoDefault(false);
    QObject::connect(cancel, &QPushButton::clicked, owner, &QDialog::reject);
    btns->addWidget(cancel);

    QPushButton *ok = new QPushButton(okText, owner);
    ok->setDefault(true);
    btns->addWidget(ok);
    return ok;
}

QPushButton *addCloseRow(QVBoxLayout *root, QWidget *owner, const QString &text)
{
    QHBoxLayout *btns = new QHBoxLayout;
    root->addLayout(btns);
    btns->addStretch(1);

    QPushButton *btn = new QPushButton(text, owner);
    btn->setDefault(true);
    // 几个只有单个按钮的窗口统一宽度, 否则"知道了"和"关闭"宽度差一截
    btn->setMinimumWidth(96);
    btns->addWidget(btn);
    return btn;
}

// ---------------------------------------------------------------------------
//  表格
// ---------------------------------------------------------------------------
void setupTable(QTableWidget *t, bool alternatingRows)
{
    // 行号没有信息量 (轮次/序号都在第一列里)
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setShowGrid(false);
    t->setAlternatingRowColors(alternatingRows);
    // 先把行高的**下限**压到 20 再设 20: QHeaderView 的 minimumSectionSize 默认
    // 是按字体/样式算出来的 (本机量到 28), 而 setDefaultSectionSize() 内部会把
    // 传进去的值夹到这个下限之上 —— 只设 20 的话实际行高还是 28, 表头高又写死
    // 20, 结果"表头 20 + 行 20"的约定从来没生效过。所有按行数算表格高度的地方
    // (ui::tableHeightForRows、条件卡最小高) 也因此全都少算了 8px/行。
    t->verticalHeader()->setMinimumSectionSize(kTableRowHeight);
    t->verticalHeader()->setDefaultSectionSize(kTableRowHeight);
    t->horizontalHeader()->setFixedHeight(kTableHeaderHeight);
    // 横向按**像素**滚。Qt 默认是 ScrollPerItem (按列滚), 那时横向滚动条的
    // 范围是"列数"而不是像素: 两列的表就只有 range 0..1, 于是把手长度永远是
    // 50%, 跟内容到底宽出多少毫无关系 —— 内容只宽 40% 也显示滚了一半。
    t->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
}

int preferredTableWidth(const QTableWidget *t)
{
    int w = t->frameWidth() * 2;
    for (int i = 0; i < t->columnCount(); ++i) w += t->columnWidth(i);
    // 内置滚动条可见时才是它真实占的宽度; sizeHint() 在样式表套上前后会差几像素
    const QScrollBar *vs = t->verticalScrollBar();
    if (vs && vs->isVisible()) w += vs->width();
    return w;
}

int tableHeightForRows(const QTableWidget *t, int rows)
{
    return kTableHeaderHeight + rows * kTableRowHeight + t->frameWidth() * 2 + 2;
}

// ---------------------------------------------------------------------------
//  滚动条行为统一的表格
// ---------------------------------------------------------------------------
FloatScrollTable::FloatScrollTable(QWidget *parent)
    : QTableWidget(parent)
{
    // 用原生滚动条 (见头文件注释: 独占一列/一行, 不压内容也不留白)。
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // 滚动条出现/消失会改变可用宽度, 外面 (比如详情窗口) 要跟着调窗口宽度。
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int) { notifyBarState(verticalScrollBar()->isVisible()); });
    // 横向滚动条出现/消失改变的是可用高度: 竖条可能因此出现或消失。
    connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this,
            [this](int, int) { relayout(); });
}

void FloatScrollTable::resizeEvent(QResizeEvent *e)
{
    QTableWidget::resizeEvent(e);
    relayout();
}

void FloatScrollTable::updateGeometries()
{
    QTableWidget::updateGeometries();
    // Qt 在 updateGeometries() 里才定下滚动条的可见性; pageStep 变化也不发
    // rangeChanged, 所以这里补一次。
    notifyBarState(verticalScrollBar()->isVisible());
}

int FloatScrollTable::floatBarWidth() const
{
    const QScrollBar *vs = verticalScrollBar();
    return (vs && vs->isVisible()) ? vs->width() : 0;
}

bool FloatScrollTable::verticalScrollNeeded() const
{
    const QScrollBar *vs = verticalScrollBar();
    return vs && vs->isVisible();
}

void FloatScrollTable::setBarStateCallback(std::function<void()> cb)
{
    m_onBarStateChanged = std::move(cb);
}

// 竖条的有无会改变外面需要的宽度, 所以只在"出现/消失"这一刻回调一次。
// 不能在每次同步时都回调 —— 外面调 setFixedWidth 会触发 resizeEvent,
// resizeEvent 又回头调 relayout(), 一路递归下去。
void FloatScrollTable::notifyBarState(bool shown)
{
    if (shown == m_barShown) return;
    m_barShown = shown;
    if (m_onBarStateChanged) m_onBarStateChanged();
}

// ---------------------------------------------------------------------------
//  文本
// ---------------------------------------------------------------------------
QString hex8(quint32 v)
{
    return QString::number(v, 16).toUpper().rightJustified(8, QLatin1Char('0'));
}

} // namespace ui
