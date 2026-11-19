#include "style.h"

QString appStyleSheet()
{
    // 说明: 界面要求"近似方角", 所以除单选圈外所有圆角都压到 0~2px;
    // 所有按钮使用同一套样式(白底 + 浅灰边框), 不再区分强调/危险/幽灵。
    return QString::fromUtf8(R"QSS(
* {
    font-family: "Microsoft YaHei UI", "Microsoft YaHei", "Segoe UI", sans-serif;
    /* 11px: 所有表格的行高统一为 20 (见 uikit.h 的 kTableRowHeight),
       11px 的字在 20px 行里上下各有余量, 不会顶到格子边框。 */
    font-size: 11px;
    color: #111111;
}

QMainWindow, QDialog {
    background: #f2f2f2;
}

/* ---------- 菜单栏 ---------- */
QMenuBar {
    background: #ffffff;
    border-bottom: 1px solid #dcdcdc;
    padding: 1px 4px;
}
QMenuBar::item {
    background: transparent;
    padding: 4px 10px;
    border-radius: 0;
    color: #222222;
}
QMenuBar::item:selected {
    background: #e8e8e8;
    color: #111111;
}
QMenuBar::item:pressed {
    background: #dcdcdc;
}

/* ---------- 卡片 ---------- */
QFrame#Card {
    background: #ffffff;
    border: 1px solid #dcdcdc;
    border-radius: 2px;
}
QLabel#CardTitle {
    font-size: 12px;
    font-weight: bold;
    color: #111111;
    /* 四张卡的标题等高: 只靠字号的话, 不同 DPI 下会差 1~2px, 卡片顶边看着就不齐 */
    min-height: 18px;
    padding: 0;
}
QLabel#Hint {
    color: #7a7a7a;
    font-size: 11px;
}
QLabel#Stat {
    color: #4a4a4a;
    font-size: 11px;
}
/* 「编辑出怪条件」里那段浅底摘要框 (显示当前条件的一句话概括)。
   以前它在 banddialog.cpp 里用内联 setStyleSheet 写, 是唯一一处绕过这份样式表的
   地方 —— 内联样式会盖掉全局 QSS, 以后想统一改底色得先找到它。
   min-height 只算内容区 (不含 padding 与边框): 26 + 上下 padding 10×2 + 边框 2
   正好是原来代码里 setMinimumHeight(48) 的总高, 换过来外观不变。 */
QLabel#Summary {
    background: #f7f7f7;
    border: 1px solid #dcdcdc;
    border-radius: 0;
    padding: 10px;
    min-height: 26px;
}

/* ---------- 输入控件 ---------- */
QLineEdit, QSpinBox, QComboBox {
    background: #ffffff;
    border: 1px solid #cccccc;
    border-radius: 0;
    /* 参数卡有 6 个输入控件, 上下各压 1px 就把这一卡省下了一大截 */
    padding: 2px 6px;
    min-height: 16px;
    selection-background-color: #111111;
    selection-color: #ffffff;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border: 1px solid #111111;
    background: #ffffff;
}
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled {
    background: #f4f4f4;
    color: #a8a8a8;
}
QComboBox::drop-down {
    border: none;
    width: 22px;
}
QComboBox QAbstractItemView {
    background: #ffffff;
    border: 1px solid #cccccc;
    selection-background-color: #e8e8e8;
    selection-color: #111111;
    outline: none;
}

/* ---------- 按钮: 全部统一为同一种样式 ---------- */
QPushButton {
    background: #ffffff;
    border: 1px solid #cccccc;
    border-radius: 0;
    /* 上下内边距只给 2px, 再配一个固定的 min-height:
       主界面三张卡各有一行按钮, 高度定死之后卡片高度才可预测,
       窗口压定之后才不会因为字号变化而忽高忽低。 */
    padding: 2px 12px;
    min-height: 20px;
    color: #222222;
}
QPushButton:hover {
    background: #f0f0f0;
    border-color: #999999;
}
QPushButton:pressed {
    background: #e2e2e2;
}
QPushButton:disabled {
    color: #b0b0b0;
    border-color: #e4e4e4;
    background: #f8f8f8;
}
QPushButton:focus {
    border-color: #888888;
    outline: none;
}

/* 微调按钮交给系统绘制: 一旦用 QSS 覆盖 up-button/down-button,
   箭头图形就会丢失, 所以这里不写这条规则 */

/* ---------- 菜单 ---------- */
QMenu {
    background: #ffffff;
    border: 1px solid #d0d0d0;
    border-radius: 0;
    padding: 4px;
}
QMenu::item {
    padding: 6px 22px 6px 14px;
    border-radius: 0;
    color: #222222;
}
QMenu::item:selected {
    background: #e8e8e8;
    color: #111111;
}
QMenu::separator {
    height: 1px;
    background: #dedede;
    margin: 4px 8px;
}

/* ---------- 表格 ---------- */
QTableWidget {
    background: #ffffff;
    border: 1px solid #dcdcdc;
    border-radius: 0;
    gridline-color: #eeeeee;
    alternate-background-color: #fafafa;
    outline: none;
    selection-background-color: #e4e4e4;
    selection-color: #111111;
}
QTableWidget::item {
    /* 上下只留 1px: 行高 20 加 11px 的字, 上下各 1px 内边距正好居中,
       3px+3px 会把字挤掉一截, 看起来像被裁了。横向 4px 保留, 挨着列边框才不显得挤。 */
    padding: 1px 4px;
}
QTableWidget::item:selected {
    background: #e4e4e4;
    color: #111111;
}
QHeaderView::section {
    background: #f0f0f0;
    color: #111111;
    font-weight: bold;
    border: none;
    border-right: 1px solid #e2e2e2;
    border-bottom: 1px solid #d8d8d8;
    /* 表头高度写死 20, 上下各留 2px 才够放下 11px 的字 (原来 5px 只剩 10px 可用) */
    padding: 2px 4px;
}
QTableCornerButton::section {
    background: #f0f0f0;
    border: none;
}

/* ---------- 进度条 ---------- */
/* 高度交给代码控制 (mainwindow 里的 setFixedHeight): QSS 的 max-height 会盖掉
   setFixedHeight, 两处各写一个值只会让人以为改了没用。 */
QProgressBar {
    border: 1px solid #dcdcdc;
    border-radius: 0;
    background: #f0f0f0;
    text-align: center;
    color: #111111;
    font-size: 11px;
}
QProgressBar::chunk {
    border-radius: 0;
    background: #b0b0b0;
}

/* ---------- 滚动条 ---------- */
/* 位置交给 Qt: 竖条独占表格极右一整列 (从表头顶端到底部), 横条独占底部一整行,
   右下角是原生拐角 —— 既不压内容也不留白。这里只管外观。
   外观按 1.2 版 + 输入框右边那个小箭头的样子做: 两端各一个不带边框的窄条按钮,
   中间一枚细三角; 轨道一整条浅灰; 把手比轨道深一档。
   三条走过的弯路 (省得以后再试):
     1) 用 CSS 的"透明边围三角"画箭头 —— Qt 不认那套写法, 画出个实心方块;
     2) 只写轨道与按钮、不写箭头 —— 一旦用 QSS 描述了 add-line/sub-line,
        Qt 就不再走原生绘制, 箭头直接不画了 (变成两个空方块);
     3) 箭头图太大 (9x5 塞进 14px 方块) —— 又粗又钝, 跟输入框右边的小三角
        完全不是一个味道。现在用 7x4 的细三角, 按钮本身 12px 高、无边框。 */
QScrollBar:vertical {
    background: #f4f4f4;
    width: 14px;
    margin: 12px 0 12px 0;
    border: none;
}
QScrollBar::handle:vertical {
    background: #c8c8c8;
    border-radius: 0;
    min-height: 24px;
    margin: 0 2px;
}
QScrollBar::handle:vertical:hover { background: #9e9e9e; }
QScrollBar::sub-line:vertical {
    subcontrol-origin: margin;
    subcontrol-position: top;
    height: 12px;
    background: #f4f4f4;
    border: none;
}
QScrollBar::add-line:vertical {
    subcontrol-origin: margin;
    subcontrol-position: bottom;
    height: 12px;
    background: #f4f4f4;
    border: none;
}
QScrollBar::sub-line:vertical:hover, QScrollBar::add-line:vertical:hover { background: #e2e2e2; }
QScrollBar::up-arrow:vertical { image: url(:/arrow_up.png); width: 7px; height: 4px; }
QScrollBar::down-arrow:vertical { image: url(:/arrow_down.png); width: 7px; height: 4px; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QScrollBar:horizontal {
    background: #f4f4f4;
    height: 14px;
    margin: 0 12px 0 12px;
    border: none;
}
QScrollBar::handle:horizontal {
    background: #c8c8c8;
    border-radius: 0;
    min-width: 24px;
    margin: 2px 0;
}
QScrollBar::handle:horizontal:hover { background: #9e9e9e; }
QScrollBar::sub-line:horizontal {
    subcontrol-origin: margin;
    subcontrol-position: left;
    width: 12px;
    background: #f4f4f4;
    border: none;
}
QScrollBar::add-line:horizontal {
    subcontrol-origin: margin;
    subcontrol-position: right;
    width: 12px;
    background: #f4f4f4;
    border: none;
}
QScrollBar::sub-line:horizontal:hover, QScrollBar::add-line:horizontal:hover { background: #e2e2e2; }
QScrollBar::left-arrow:horizontal { image: url(:/arrow_left.png); width: 4px; height: 7px; }
QScrollBar::right-arrow:horizontal { image: url(:/arrow_right.png); width: 4px; height: 7px; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ---------- 复选 ---------- */
QCheckBox {
    spacing: 6px;
    color: #222222;
}

/* 勾选框有两种宿主: 独立的 QCheckBox (参数卡), 以及表格里 QTableWidgetItem 的
   CheckState 格 (「编辑出怪条件」)。后者走的是另一条选择器, 上面的
   QCheckBox::indicator 管不到它 —— 不写就会退回系统默认的蓝色方块。
   两边必须一模一样, 所以写成同一个选择器组, 改一次两处都变。 */
QCheckBox::indicator, QTableWidget::indicator {
    width: 14px;
    height: 14px;
}
QCheckBox::indicator:unchecked, QTableWidget::indicator:unchecked {
    border: 1px solid #b0b0b0;
    border-radius: 0;
    background: #ffffff;
}
QCheckBox::indicator:checked, QTableWidget::indicator:checked {
    border: 1px solid #2f6fed;
    border-radius: 0;
    background: #2f6fed;
    image: url(:/check_white.png);
}
)QSS");
}
