#include "detaildialog.h"

#include "detailview.h"
#include "uikit.h"

#include <QLabel>
#include <QMargins>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

DetailDialog::DetailDialog(QWidget *parent)
    : QDialog(parent)
{
    // 最小高度仍按"标题 + 若干行 + 按钮"给一个下限; 最小宽度由 prepareDialog
    // 统一设 (ui::kDialogMinWidth), 这里不再各写一个数。
    setMinimumHeight(420);

    // 外壳 (标题 / 去"这是什么?"按钮 / 边距与间距) 统一走 uikit
    QVBoxLayout *root = ui::prepareDialog(this, QString::fromUtf8("出怪详情"));

    m_title = ui::cardTitle(QString());
    root->addWidget(m_title);

    m_view = new DetailView(this);
    root->addWidget(m_view, 1);

    // 窗口宽度跟着表格需要的宽度走。竖条出现/消失会让需要的宽度变一条滚动条,
    // 只在这里算一次的话, 竖条一露面又压回最后一列 —— 所以表格那边一变就跟着收放。
    connect(m_view, &DetailView::preferredWidthChanged, this,
            [this](int) { fitToContent(); });

    // 只有一个"关闭"按钮, 宽度与使用说明/关于页的按钮统一 (uikit 里定 96)
    QPushButton *close = ui::addCloseRow(root, this, QString::fromUtf8("关闭"));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
}

void DetailDialog::clear()
{
    m_title->setText(QString());
    m_view->clear();
}

void DetailDialog::setData(quint32 seed, int scene,
                           const std::vector<sf::DetailRow> &rows,
                           const sf::RuleTable &rank, const sf::RuleTable &hard)
{
    m_title->setText(QString::fromUtf8("种子 %1　·　%2")
                         .arg(ui::hex8(seed))
                         .arg(sceneDisplayName(scene)));
    m_view->setData(seed, scene, rows, rank, hard);
    fitToContent();
}

// 窗口收到刚好放得下表格的宽度, 右侧不留白。
// 必须"能涨也能缩": 只涨不缩的话, 先看一个有 20 列僵尸的场景, 再点开一个
// 只有 15 列的场景, 窗口仍然停在旧宽度上, 右边就留出一条空白。
// 这里不能多留余量: DetailView 的表格被钉成一列一列定宽, 窗口多给的每一像素
// 都会原样变成表格右边的一条灰底 —— 看着就是"没消干净"的留白。
// (横向滚动条在 DetailView 里是关掉的, 所以也不需要用余量去躲它。)
//
// 宽度上下限都钉到内容宽度上, 窗口横向就不可拖动了:
//   - 只 resize 不设下限, 用户还能把窗口拖窄, 右边的几列被裁掉 (横向滚动条
//     是关的, 裁掉的部分再也看不到) —— 就是"详情窗口能缩到没字"的那个问题;
//   - 只设下限不设上限, 用户拖宽以后右边又多出一条白底。
// 下限必须跟着内容一起变: 换个僵尸列少的场景, 下限要跟着降下来, 否则窗口
// 收不回去。高度不动, 仍由用户拖。
void DetailDialog::fitToContent()
{
    const QMargins mg = layout()->contentsMargins();
    const int needW = m_view->preferredWidth() + mg.left() + mg.right();
    // 先设范围再 resize: setMinimumWidth 会顺手把当前宽度顶到下限,
    // 顺序反了会多一次跳动。
    setMinimumWidth(needW);
    setMaximumWidth(needW);
    if (width() != needW) resize(needW, height());
}
