#pragma once

// 界面公共部件的唯一来源。
//
// 之前"去帮助按钮 + 10px 边距 + 8px 间距"这套对话框外壳在四个文件里各写了一遍;
// 卡片构造在 mainwindow.cpp 与 helppages.cpp 各有一套; 表格的
// "隐藏行号 / 不画网格 / 只读 / 交替行色" 样板重复了四次; 十六进制格式化和
// 场景名查找也各有两份。这些都属于"同一种东西写成好几样", 改一处忘了另一处
// 就会出现两张表行高不同、两个对话框边距不同的情况。
//
// 统一收进这里之后: 想调整全局观感只改这一个文件; 新加窗口/表格直接复用,
// 不用再抄一遍样板。

#include <QString>
#include <QTableWidget>

#include <functional>

class QDialog;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QResizeEvent;
class QSpinBox;
class QVBoxLayout;
class QWidget;

namespace ui {

// ---------------------------------------------------------------------------
//  统一尺寸
// ---------------------------------------------------------------------------
// 所有表格共用一套行高与表头高。以前条件表是 18/20、结果表和详情表是 22/默认、
// 「编辑出怪条件」表是 18/22 —— 三张表并排看过去行距参差, 高度也算不准
// (同一份"表头 + n 行"在三处各算一遍)。
//
// 20 的取法: 11px 的字一行约 15px, 加上 QTableWidget::item 的上下各 1px 内边距、
// 以及行间距的余量, 20 刚好放得下且不切字。表头同理 (QSS 里上下各 2px 内边距)。
const int kTableRowHeight = 20;
const int kTableHeaderHeight = 20;

// 输入框里上下箭头 (QSpinBox 的 step 按钮) 占的宽度。来自 style.cpp 里
// QComboBox::drop-down 的 22px —— 数字框的箭头区是系统绘制的, 宽度与下拉框一致,
// 所以量数字框需要留多宽时要用它。写死成魔数的话, 哪天改了 QSS 就量不准。
const int kSpinArrowWidth = 22;
// 输入控件的左右内边距 (style.cpp: QLineEdit/QSpinBox/QComboBox 的 padding 6px)。
const int kInputPaddingH = 6;

// 滚动条宽度 (px)。位置交给 Qt (竖条独占极右一整列), 但外观由 style.cpp 的
// QScrollBar 规则接管, 宽度那个 14px 就写在样式表里 —— 两边必须一致, 所以这里
// 也写 14。给滚动区预留宽度的地方要用它, 写死别的数字就会多留一条白缝。
const int kScrollBarWidth = 14;

// 表格里勾选框的边长, 与 style.cpp 的 QCheckBox::indicator / QTableWidget::indicator
// 一致。「编辑出怪条件」把勾选框居中自绘, 那个 14 必须和这里同步。
const int kCheckIndicatorSize = 14;

// 对话框的最小宽度。几个窗口的按钮行、标题行宽度各不相同, 各自写一个 420/96
// 之类的数字会让窗口最小宽度参差; 统一放在 prepareDialog 里设, 想改只改这里。
const int kDialogMinWidth = 420;

// ---------------------------------------------------------------------------
//  对话框外壳
// ---------------------------------------------------------------------------
// 去标题栏的"这是什么?"按钮 (程序没设任何 whatsThis 文本, 点它只会进入上下文
// 帮助模式却没有内容可显示, 看起来像失灵), 设标题, 装上内容边距 10px / 间距 8px
// 的根布局。返回根布局, 调用方往里面塞内容。
//
// 四个对话框 (编辑出怪条件 / 编辑全局条件 / 出怪详情 / 使用说明 / 关于) 的边距
// 与间距从这里统一, 不再各写一套。
QVBoxLayout *prepareDialog(QDialog *dlg, const QString &title);

// ---------------------------------------------------------------------------
//  卡片
// ---------------------------------------------------------------------------
// 主界面左栏三卡与右栏一卡、使用说明与关于页里的每张卡, 都是同一个外观:
// objectName 为 "Card" 的白底圆角框 + 8/2 的内边距 + 3px 间距。
QLabel *cardTitle(const QString &text);

// 标题行是自定义控件时走这个 (右栏那张卡的标题行里有输入框和按钮)。
QFrame *makeCard(QWidget *titleRow, QWidget *child);

// 标题为纯文字时走这个 (左栏三卡与帮助页的卡)。
QFrame *makeCard(const QString &title, QWidget *child);

// ---------------------------------------------------------------------------
//  按钮行
// ---------------------------------------------------------------------------
// 对话框底部右侧的 "取消" + "确定"。确定的默认按钮状态统一在这里设,
// 取消一律 setAutoDefault(false) —— 否则在输入框里敲回车可能触发取消,
// 而 Qt 对"哪个是默认按钮"的处理在不同对话框里本来是一致的, 之前只有部分
// 对话框设置了 autoDefault, 行为因此不一致。
//
// leading 不为空时先放在最左边 ("编辑出怪条件"的"全部清空"就摆在那个位置),
// 与右侧的取消/确定之间用弹簧隔开 —— 两种按钮行的左侧内容不同, 但整体布局
// 与按钮宽度必须一致, 否则几个对话框的按钮不在一条规律上。
//
// 返回确定按钮, 调用方连自己的校验槽 (校验逻辑各不相同, 不能替调用方决定)。
QPushButton *addOkCancelRow(QVBoxLayout *root, QDialog *owner,
                            const QString &okText = QString::fromUtf8("确定"),
                            QWidget *leading = nullptr);

// 只有一个按钮的按钮行 (使用说明 / 关于 / 出怪详情)。返回该按钮, 由调用方
// 自己 connect —— 三个窗口对"关闭"的处理不一样 (帮助页是 accept, 出怪详情是
// reject), 替调用方决定反而要再 disconnect 一次。宽度统一给到 minWidth,
// 几个窗口的按钮才不会一个宽一个窄。
QPushButton *addCloseRow(QVBoxLayout *root, QWidget *owner, const QString &text);

// ---------------------------------------------------------------------------
//  表格
// ---------------------------------------------------------------------------
// 所有表格共用的样板: 隐藏行号、只读、不画网格、行高与表头高统一。
// 交替行色由调用方决定 (出怪详情要靠灰底标注"这轮没满足", 两套条纹会打架)。
void setupTable(QTableWidget *t, bool alternatingRows = true);

// 表格各列定宽之和 (含边框)。窗口用它把自己收到刚好放得下内容, 右侧不留白。
// 以前 CondDialog 与 DetailView 各算一份, 且滚动条宽度一个用 width() 一个用
// sizeHint().width() —— 同一件事两种结果。
int preferredTableWidth(const QTableWidget *t);

// 只放 rows 行时表格该有多高: 表头 + rows 行 + 边框。
// 「编辑出怪条件」要把窗口收到刚好放下整张表, 高度差一点就会多出滚动条,
// 或者底下多一条灰边。以前那个式子写在 rebuildTable() 里, 只有那一处用得到,
// 但公式本身属于"表格怎么量"这一类, 放在这里以后加表格不用再推一遍。
int tableHeightForRows(const QTableWidget *t, int rows);

// ---------------------------------------------------------------------------
//  滚动条行为统一的表格
// ---------------------------------------------------------------------------
// 用 Qt 自带的滚动条, 不做浮动覆盖。
//
// 为什么改回原生: 早先为了"表头通到右边", 把自带竖条关掉, 另挂一根只盖数据区
// 的自绘条。副作用是出一堆毛病 —— 竖条压住最后一列的字形 (详情表僵尸列只有
// 24px)、竖条与横条在右下角撞在一起、"右上角空一块" (自绘条只盖 viewport,
// 表头右侧那一列是表格背景)。1.2 版的做法是让滚动条**独占一列/一行**: 竖条
// 占极右整列 (从表头顶端画到表底), 横条占底部整行, 右下角是原生拐角。这样既
// 不压内容, 也没有任何空白。
//
// 这个类因此只做三件事: 记住"哪些列铺满剩余宽度"(子类重写 relayout),
// 在滚动条出现/消失时通知外面 (窗口宽度要跟着变), 以及提供量宽度的小工具。
// 主界面三张表 (出怪条件/全局条件/结果) 与出怪详情都继承它。
class FloatScrollTable : public QTableWidget
{
public:
    explicit FloatScrollTable(QWidget *parent = nullptr);

    // 由子类在填完内容后调用: 重算列宽。默认实现什么都不做
    // (列宽由调用方自己设), 子类按需要重写。
    virtual void relayout() {}

    // 竖向滚动条占的宽度 (px)。滚动条不可见时返回 0。
    // 外面要按"内容 + 滚动条"算窗口宽度时用它。
    int floatBarWidth() const;

    // 当前是否真的滚得动 (即竖条可见)。
    bool verticalScrollNeeded() const;

    // 登记"竖条出现/消失"的回调。外面包着表格的控件用它来跟着调整自身宽度:
    // 需要滚动时加宽一条滚动条的宽度, 不需要时收回, 免得白留一条缝。
    // 只在状态真的翻转时回调一次 —— 每次同步都回调的话, 外面一改宽度就触发
    // resizeEvent, resizeEvent 又回头重算, 会无限递归。
    void setBarStateCallback(std::function<void()> cb);

protected:
    void resizeEvent(QResizeEvent *e) override;

    // Qt 在 updateGeometries() 里才把滚动条的 visible 状态定下来,
    // 这里在基类算完之后补一次通知 —— 只在 rangeChanged 里通知会漏掉
    // "行数没变但视口高度变了, 滚动条因此消失"这一种。
    void updateGeometries() override;

private:
    void notifyBarState(bool shown);

    bool m_barShown = false;
    std::function<void()> m_onBarStateChanged;
};

// ---------------------------------------------------------------------------
//  文本
// ---------------------------------------------------------------------------
// 8 位大写十六进制种子。结果表、详情窗口标题、状态栏都用它,
// 以前 detaildialog.cpp 里内联写了一遍相同的表达式。
QString hex8(quint32 v);

} // namespace ui
