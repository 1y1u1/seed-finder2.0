#include "mainwindow.h"

#include "banddialog.h"
#include "conddialog.h"
#include "detaildialog.h"
#include "detailview.h"
#include "gpuhook.h"
#include "helppages.h"
#include "searchworker.h"
#include "uikit.h"
#include "zombiedata.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegExp>
#include <QRegExpValidator>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <cstdio>

#include <algorithm>

namespace {
// 条件分段对话框的扫描区间上限: 条件自己决定真正范围, 这里给足整段。
// 上限只受 int 与计算成本的限制, 不必卡在 10000 —— 实际用下来有人会填
// 五位数的 F 数去查很靠后的轮次, 这里放到 1000000 F (第 50 万轮)。
// 注意: 区间越靠后轮次越多, 扫描越慢, 但结果不会因为轮次高而失真
// (第 9 轮以后每轮的种类上限已经封顶, rowBits 也只取到 d1[9])。
const int kOuterBegin = 2;      // F 3 起: 第 1 ~ 2 轮固定是普僵+路障/读报, 查不到别的
const int kOuterEnd = 1000000;

// 左栏定宽的兜底下限。实际宽度由 buildParamCard 按控件实测得出 (存 m_leftWidth),
// 这里只是个起点: 万一量失败也不至于把界面压得没法看。
const int kLeftWidth = 300;

// 范围列的兜底下限 (实际宽度按字体量, 见 rangeColumnWidth(), 取两者大的那个)
const int kRangeColumnWidth = 100;

// 条件列至少露出这么宽, 更长的条件用表格自带的横向滚动条看。
// 不再为了"一行放下 20 个僵尸"把整张卡撑到 350+: 那种条件一年也用不到几次,
// 真遇到了滚一下就行, 不值得让所有人天天看一条宽出来的空白。
const int kConditionMinWidth = 150;

// 出怪条件与全局条件两张表的高度统一: 表头 + 3 行可见, 再多就在表格里滚。
// 行高与表头高用 uikit 的公共值 (详情窗口、编辑条件对话框也走同一套),
// 三张表放在一起看才不会一行 18 一行 22。
const int kConditionTableHeight =
    ui::kTableHeaderHeight + 3 * ui::kTableRowHeight + 2;

// 右栏宽度。只剩"种子输入框 104 + 查看出怪 ~90"这一行了, 240 就够。
// 再宽就是给结果表留白: 种子列是 Stretch, 多出来的宽度全变空白。
const int kRightWidth = 240;

// 范围列要多宽才刚好放得下最长的 "999999 到 1000000 F"。
// 按字体量而不是拍一个数字: 范围列窄了会把范围截成省略号,
// 宽了又白白吃掉条件列的地方。f 传表格的字体 (含粗体表头的实际情况由调用方决定)。
static int rangeColumnWidth(const QFont &f)
{
    QFontMetrics fm(f);
    // 界面里 F 数最大能填到 kOuterEnd, 所以最宽的一串是它自己的上下界。
    const QString widest = QString::fromUtf8("%1 到 %1 F").arg(kOuterEnd);
    // 单元格左右各有 4px 内边距 (QTableWidget::item { padding: 1px 4px }), 再留 4px
    const int need = fm.width(widest) + 4 * 2 + 4;
    return std::max(kRangeColumnWidth, need);
}

// 浮动竖向滚动条的表格基类本身在 uikit.h (ui::FloatScrollTable):
// 出怪详情窗口的表格也用它, 几处必须共享同一份实现才能保证外观一致。
// 这里只放主界面这两张表各自的列宽逻辑。

// 条件表 (出怪条件 / 全局条件)。
//
// 1) 默认 sizeHint 是按内容算的, 3 行条件时只有 78px 高, 但 QTableWidget
//    自带的 sizeHint 是 256x192, 布局会照着它给高度 —— 主窗口一开就冒出竖向
//    滚动条。这里把 sizeHint 压到"最小时该有的高度", 再由 Expanding 跟着长。
//
// 2) 竖向滚动条是 Qt 原生的: 它独占表格极右一整列 (从表头顶端画到表底),
//    宽度由 Qt 自己从控件的可用宽度里扣掉, viewport()->width() 已经把这一列
//    排除在外 —— 所以列宽照 viewport 算就行, 不用再手动让出滚动条的宽度。
//    (以前用自绘的浮动条时得手动减, 减多了是白缝, 减少了压字。)
//
// 3) 横向滚动条同理: 原生横条独占底部一整行, 不与竖条抢拐角。
//
// 4) 行高恒为 ui::kTableRowHeight, 不随窗口高度变化。早先这里有个 fitRowHeight()
//    把行高在 20~22 之间微调, 想让可见区正好放下整数行 (不露半行); 但代价是
//    用户拉窗口时行高一直在变, 看着像在抖, 而"不露半行"从来也没真正达成过
//    (那时 minimumSectionSize 是 28, 微调全被夹掉了)。行高恒定, 底部露一点
//    余量是可接受的。
class ConditionTable : public ui::FloatScrollTable
{
public:
    explicit ConditionTable(int minHeight, QWidget *parent = nullptr)
        : ui::FloatScrollTable(parent), m_minHeight(minHeight)
    {
        // 横向要能滚: 长条件必须看得全, 不能让列被压到只剩省略号。
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // 逐行滚: 表格里是条件条目, 半行没有意义。
        setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    }

    QSize sizeHint() const override { return QSize(0, m_minHeight); }

    // 由 fillRuleRows 填完内容后调用: 告诉表格两列各自"内容需要多宽"。
    // 条件列不会低于这个宽度 (长条件横向滚着看), 但内容比剩余空间窄时
    // 会撑满剩余宽度 —— 这样短条件不会莫名其妙冒出一条横向滚动条。
    void setColumnNeeds(int rangeWidth, int condWidth)
    {
        m_rangeWidth = rangeWidth;
        m_condWidth = condWidth;
        updateColumnWidths();
    }

protected:
    void relayout() override
    {
        updateColumnWidths();
    }

private:
    void updateColumnWidths()
    {
        // viewport()->width() 已经扣掉了原生竖条占的那一列, 直接用。
        const int avail = viewport()->width();
        if (avail <= 0) return;
        setColumnWidth(0, m_rangeWidth);
        // 条件列: 至少放得下内容, 内容更窄时撑满剩余宽度。
        setColumnWidth(1, std::max(m_condWidth, avail - m_rangeWidth));

        // 横向滚动条要不要, 必须自己判断并显式设定。
        //
        // Qt 只在 updateGeometries() 里按"表头总长 vs viewport 宽"决定横条的
        // 显隐, 而改列宽本身并不触发它 —— 于是会留下这种坏状态: 竖条刚出现、
        // viewport 从 296 窄成 282, Qt 按旧列宽(122+174=296)判定需要横条并把它
        // 显示出来; 紧接着本函数把条件列收窄到 160(总长 282, 其实正好放下),
        // 但 Qt 不会再算一次, 横条就白占着一行 —— 探针实测到 range=0..0 却
        // visible=yes。
        //
        // 判据可以直接算: 列宽总长 = max(m_condWidth + m_rangeWidth, avail),
        // 所以"放不下"等价于 m_condWidth + m_rangeWidth > avail。
        // 用这个常量条件而不是读当前表头长度, 结果与 Qt 内部是否已经重算过无关。
        const bool needHBar = (m_condWidth + m_rangeWidth) > avail;
        const Qt::ScrollBarPolicy want =
            needHBar ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
        if (horizontalScrollBarPolicy() != want) {
            setHorizontalScrollBarPolicy(want);
        }
    }

    int m_minHeight;
    int m_rangeWidth = kRangeColumnWidth;
    int m_condWidth = kConditionMinWidth;
};

// 结果表。「满足轮数」列定宽 84, 「种子」列撑满剩余宽度。
// 竖向滚动条同样是原生独占一列, viewport 宽度里已经不含它。
class ResultTable : public ui::FloatScrollTable
{
public:
    explicit ResultTable(QWidget *parent = nullptr) : ui::FloatScrollTable(parent)
    {
        // 结果表不需要横向滚动: 只有两列, 一列撑满一列定宽。
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        // 逐项滚: 半行没有意义, 也免得滚动时行高看着抖。
        setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    }

protected:
    void relayout() override
    {
        const int avail = viewport()->width();
        if (avail > 0) {
            const int second = 84;
            setColumnWidth(1, second);
            setColumnWidth(0, std::max(0, avail - second));
        }
    }
};

// 状态行里的种子数: 亿/万缩写。完整数字 (4,294,967,296) 太长,
// 会把状态行顶到换行, 卡片高度一跳进度条和按钮就跟着抖。
// 精度到小数点后一位, 浏览时足够判断"扫到哪儿了"。
QString shortNumber(quint64 v)
{
    if (v >= 100000000ULL) {
        return QString::fromUtf8("%1 亿").arg(QString::number(double(v) / 1e8, 'f', 1));
    }
    if (v >= 10000ULL) {
        return QString::fromUtf8("%1 万").arg(QString::number(double(v) / 1e4, 'f', 1));
    }
    return QString::number(v);
}

// 预设列表里的每一行文案。载入 / 删除两个入口显示的内容必须一致,
// 否则同一套参数在两处"看起来"不一样, 用户会怀疑自己选错了。
// 条件数要把出怪条件与全局条件都算上: 只数前者时, 一套"只有全局条件"的
// 预设会显示成"0 条条件", 看起来像空预设。
QStringList presetLabels(const QVector<Preset> &list)
{
    QStringList names;
    for (int i = 0; i < list.size(); ++i) {
        const Preset &p = list[i];
        const int total = p.bands.size() + p.hard.size();
        names << QString::fromUtf8("%1　· %2 %3~%4 F（%5 条条件）")
                     .arg(p.name)
                     .arg(sceneDisplayName(p.scene))
                     .arg(p.beginFlag + 1)
                     .arg(p.endFlag)
                     .arg(total);
    }
    return names;
}

// 两张条件表 (出怪条件 / 全局条件) 的结构完全一样: 两列、定宽、行高与表头
// 走 uikit、竖条是原生独占一列。差别只有标题、挂到哪个成员、按钮连到哪两个槽。
// 以前这里是两份几乎逐行相同的 45 行代码, 改一处很容易漏掉另一处
// (结果就是一张表能双击编辑、另一张不能)。
ConditionTable *makeRuleTable(int minHeight)
{
    ConditionTable *t = new ConditionTable(minHeight);
    t->setColumnCount(2);
    t->setHorizontalHeaderLabels(QStringList()
        << QString::fromUtf8("范围") << QString::fromUtf8("条件"));
    ui::setupTable(t);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    // 条件列不 Stretch: 让它保持"内容需要多宽就多宽", 超出表格宽度时横向滚动。
    // Stretch 会把列压成表格宽度, 长条件直接变成省略号, 等于看不到。
    // 具体宽度由 fillRuleRows -> setColumnNeeds 按内容算, 这里只定策略。
    t->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    t->setColumnWidth(0, rangeColumnWidth(t->font()));
    t->setColumnWidth(1, kConditionMinWidth);
    // 两张条件表高度一致 (表头 + 3 行), 左右两块卡片看起来才整齐。
    // 用最小高度而不是固定高度: 主窗口纵向拉高时, 两张表跟着长高、多显示几行,
    // 而不是把多出来的高度全变成卡片下方的空白。
    t->setMinimumHeight(minHeight);
    t->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    return t;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("种子查找器 2.0"));
    buildUi();

    // 尺寸放在 buildUi 之后: 左栏宽度是 buildParamCard 按控件实测出来的
    // (存在 m_leftWidth 里), 先定尺寸就会用上还没量出来的默认值。
    // 左右两栏都是定宽, 所以默认宽度 = 两栏 + 边距 + 间距 + 窗口边框。
    // 高度按实测的最小可用高度给: 参数卡三行 (用户/存档、场景/输出、GPU 开关)
    // + 状态两行 + 进度条 + 按钮行, 再加两张条件表各 3 行可见的空间。
    // 这些数字都会被 tests/uiprobe 量出来核对, 不能凭估算写死。
    const int wantW = m_leftWidth + ui::kScrollBarWidth + 2 + kRightWidth + 32;
    resize(wantW, 500);
    setMinimumSize(wantW, 500);

    // 启动时不预置任何出怪条件: 用户自己从"添加"开始加,
    // 免得上来就带着一条他没设过、也不知道该不该留的红眼条件。
    refreshGlobalTable();
}

MainWindow::~MainWindow()
{
    if (m_thread) {
        if (m_worker) m_worker->requestStop();
        m_thread->quit();
        m_thread->wait(3000);
    }
}

// 命令行截图用: 一套能同时展示"多段出怪条件 + 四组条件 + 全局条件"的演示参数。
// 每段 F 数范围都只覆盖 1 轮, 保证条件不会严到无解。
void MainWindow::loadDemo()
{
    m_bands.clear();

    sf::RuleBand b1;
    b1.beginFlag = 10;
    b1.endFlag = 12;
    b1.cond.needMask = (1u << Z_GIGA);      // 必出红眼
    b1.cond.sanitize();
    m_bands << b1;

    sf::RuleBand b2;
    b2.beginFlag = 20;
    b2.endFlag = 22;
    b2.cond.needMask = (1u << Z_GARG);      // 必出白眼
    b2.cond.sanitize();
    m_bands << b2;

    sf::RuleBand b3;
    b3.beginFlag = 30;
    b3.endFlag = 32;
    b3.cond.atLeastMask = (1u << Z_BUCKET) | (1u << Z_DOOR) | (1u << Z_FOOTBALL);
    b3.cond.atLeast = 2;                    // 铁桶 / 铁门 / 橄榄 至少出 2 种
    b3.cond.sanitize();
    m_bands << b3;

    sf::RuleBand b4;
    b4.beginFlag = 40;
    b4.endFlag = 42;
    b4.cond.refuseMask = (1u << Z_JACK);    // 禁出小丑
    b4.cond.sanitize();
    m_bands << b4;

    // 全局条件(硬性): 第 11~12 轮必须出现撑杆或读报
    m_hard.clear();
    sf::RuleBand h;
    h.beginFlag = 10;
    h.endFlag = 12;
    h.cond.needMask = (1u << Z_POLE) | (1u << Z_NEWS);
    h.cond.sanitize();
    m_hard << h;

    refreshBandTable();
    refreshGlobalTable();
}

void MainWindow::setSpanForTest(quint64 n)
{
    m_spanOverride = n;
}

void MainWindow::setSceneForTest(int sceneIndex)
{
    m_scene->setCurrentIndex(sceneIndex);
}

void MainWindow::setBadDemo()
{
    loadDemo();
    sf::RuleBand &b = m_bands.first();
    b.beginFlag = 2;                    // 第 2 轮 (F 3~4)
    b.endFlag = 4;
    b.cond = sf::WaveCondition();
    b.cond.needMask = (1u << Z_ZOMBONI); // 必出冰车
    b.cond.sanitize();
    refreshBandTable();
}

// 测试用: 按 index 装入第 n 组非法条件, 覆盖 checkConditions 的每条拦截规则。
// 返回 false 表示 index 超出范围。
bool MainWindow::setBadCase(int index)
{
    loadDemo();
    m_bands.clear();
    m_hard.clear();
    // 每组用例都从默认场景 (PE/FE) 开始, 免得受上一条用例的影响。
    setSceneForTest(2);
    sf::RuleBand b;
    switch (index) {
    case 0:     // 必出红眼早于第 6 轮
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.needMask = (1u << Z_GIGA);
        break;
    case 1:     // 必出冰车早于第 3 轮
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.needMask = (1u << Z_ZOMBONI);
        break;
    case 2:     // 第 2 轮路障读报同时禁出 (任意范围都非法, 这里用界面上填得出的范围)
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.refuseMask = (1u << Z_CONE) | (1u << Z_NEWS);
        break;
    case 3:     // 早期轮要求种类数超过上限 (第 2 轮最多 4 种)
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.atLeastMask = (1u << Z_BUCKET) | (1u << Z_DOOR) | (1u << Z_FOOTBALL)
                             | (1u << Z_DANCE) | (1u << Z_SNORKEL);
        b.cond.atLeast = 5;
        break;
    case 4:     // 「至多出」填 0 —— 等于"这几只一个都不许出", 想这么填该用「禁出」列
        b.beginFlag = 10; b.endFlag = 12;
        b.cond.atMostMask = (1u << Z_CONE) | (1u << Z_NEWS);
        b.cond.atMost = 0;
        break;
    case 5:     // 必出∩禁出
        b.beginFlag = 10; b.endFlag = 12;
        b.cond.needMask = (1u << Z_JACK);
        b.cond.refuseMask = (1u << Z_JACK);
        break;
    case 6:     // 「至少出」名额超限: 第 1 轮 (F 3~4) 只有 2 个新增格
        // 走的是 atLeast 分支 (case 9 走 needMask 分支), 两条路都要拦。
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.atLeastMask = (1u << Z_POLE) | (1u << Z_BUCKET) | (1u << Z_DOOR)
                             | (1u << Z_GARG);
        b.cond.atLeast = 3;
        break;
    case 7: {   // 跨段矛盾: 一段必出小丑, 重叠段禁出小丑
        b.beginFlag = 10; b.endFlag = 16;
        b.cond.needMask = (1u << Z_JACK);
        m_bands << b;
        sf::RuleBand c;
        c.beginFlag = 14; c.endFlag = 20;
        c.cond.refuseMask = (1u << Z_JACK);
        c.cond.sanitize();
        m_bands << c;
        refreshBandTable();
        refreshGlobalTable();
        return true;
    }
    case 8:     // 路障 + 读报同时禁出 (任意范围都非法)
        b.beginFlag = 10; b.endFlag = 16;
        b.cond.refuseMask = (1u << Z_CONE) | (1u << Z_NEWS);
        break;
    case 9:     // 必出 3 种非固定僵尸: 第 2 轮 (F 3~4) 只有 2 个新增名额
        b.beginFlag = 2; b.endFlag = 4;
        b.cond.needMask = (1u << Z_POLE) | (1u << Z_BUCKET) | (1u << Z_DOOR);
        b.cond.needTotal = 3;
        break;
    case 10:    // 长区间但起点很早: 3 到 10 F 必出 海豚/小丑/气球 (第 2 轮只有 2 个名额)
        b.beginFlag = 2; b.endFlag = 10;
        b.cond.needMask = (1u << Z_DOLPHIN) | (1u << Z_JACK) | (1u << Z_BALLOON);
        b.cond.needTotal = 3;
        break;
    case 11:
        // 五条各自"必出 1 种", 单条都合规, 但都压在 F 3~4 这一轮上。
        // 这一轮最多只有 4 种僵尸, 合起来要求 5 种 —— 只有把重叠的条件并起来
        // 看名额才发现无解。1.2 原版实测五个场景都是 0 个解。
        m_bands.clear();
        for (int z : { Z_CONE, Z_DOOR, Z_POGO, Z_CATAPULT, Z_DIGGER }) {
            sf::RuleBand one;
            one.beginFlag = 2;
            one.endFlag = 4;
            one.cond.needMask = (1u << z);
            one.cond.sanitize();
            m_bands << one;
        }
        refreshBandTable();
        refreshGlobalTable();
        return true;
    case 12:
        // 禁出方向的对应用例: 几条各禁几只, 单条都合规, 合起来把 F 5~6 这一轮
        // 禁到只剩 普僵 + 路障 这 2 种, 而这一轮至少要出 min(2+1, 9) = 3 种。
        // 四组一共禁掉 18 种 (留 普僵 与 路障 不勾), 单组最多禁 5 种 —— 单看都合规。
        m_bands.clear();
        {
            static const int groups[4][5] = {
                { Z_POLE, Z_BUCKET, Z_NEWS, Z_DOOR, Z_FOOTBALL },
                { Z_DANCE, Z_SNORKEL, Z_ZOMBONI, Z_DOLPHIN, Z_JACK },
                { Z_BALLOON, Z_DIGGER, Z_POGO, Z_BUNGEE, Z_LADDER },
                { Z_CATAPULT, Z_GARG, Z_GIGA, -1, -1 },
            };
            static const int sizes[4] = { 5, 5, 5, 3 };
            for (int group = 0; group < 4; ++group) {
                sf::RuleBand one;
                one.beginFlag = 4;
                one.endFlag = 6;
                for (int k = 0; k < sizes[group]; ++k) {
                    one.cond.refuseMask |= (1u << groups[group][k]);
                }
                one.cond.sanitize();
                m_bands << one;
            }
        }
        refreshBandTable();
        refreshGlobalTable();
        return true;
    default:
        return false;
    }
    b.cond.sanitize();
    m_bands << b;
    refreshBandTable();
    refreshGlobalTable();
    return true;
}

// ---------------------------------------------------------------------------
//  界面
// ---------------------------------------------------------------------------
void MainWindow::buildUi()
{
    buildMenuBar();

    QWidget *central = new QWidget(this);
    QVBoxLayout *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    QWidget *body = new QWidget(central);
    QHBoxLayout *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(10, 10, 10, 10);
    bodyLay->setSpacing(6);

    // ---- 左栏 ----
    // 两张条件卡各占一份纵向伸缩量: 窗口拉高时它们平分多出来的高度,
    // 而不是把高度全留给空白 (以前左栏底部一条大留白, 卡片本身不动)。
    QWidget *leftInner = new QWidget;
    QVBoxLayout *leftLay = new QVBoxLayout(leftInner);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(5);
    QWidget *paramCard = buildParamCard();
    leftLay->addWidget(paramCard);
    leftLay->addWidget(buildBandCard(), 1);
    leftLay->addWidget(buildGlobalCard(), 1);

    QScrollArea *scroll = new QScrollArea;
    scroll->setWidget(leftInner);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // 左栏宽度 = 参数卡"刚好放下"需要的宽度 (buildParamCard 量好放在 m_leftWidth),
    // 再留出竖向滚动条的位置。不能写死一个比内容小的数: 横向滚动条是关掉的,
    // 一旦卡片比可见区宽, 右边那截会被直接裁掉看不见。
    // 宽度取自 ui::kScrollBarWidth (与 style.cpp 里 QScrollBar 的 14px 一致),
    // 不再问 sizeHint —— 这里和构造函数里算默认窗口宽度用的是同一个数, 两处才一致。
    scroll->setFixedWidth(m_leftWidth + ui::kScrollBarWidth + 2);

    QWidget *right = buildResultCard();
    // 右栏也定宽: 窗口横向拉宽 (含全屏) 时不再把结果表和左栏卡片一起拉宽,
    // 多出来的宽度全部变成左右边距, 内容整体居中。
    right->setFixedWidth(kRightWidth);

    bodyLay->addStretch(1);
    bodyLay->addWidget(scroll);
    bodyLay->addWidget(right);
    bodyLay->addStretch(1);

    root->addWidget(body, 1);
    setCentralWidget(central);
}

// 菜单栏: 与 1.2 版一致, 帮助放在窗口顶部的原生菜单里
void MainWindow::buildMenuBar()
{
    QMenuBar *bar = menuBar();

    QMenu *presetMenu = bar->addMenu(QString::fromUtf8("预设"));
    QAction *actSave = presetMenu->addAction(QString::fromUtf8("保存当前参数…"));
    QAction *actLoad = presetMenu->addAction(QString::fromUtf8("载入预设…"));
    QAction *actDel = presetMenu->addAction(QString::fromUtf8("删除预设…"));
    connect(actSave, &QAction::triggered, this, &MainWindow::onSavePreset);
    connect(actLoad, &QAction::triggered, this, &MainWindow::onLoadPreset);
    connect(actDel, &QAction::triggered, this, &MainWindow::onDeletePreset);

    QMenu *helpMenu = bar->addMenu(QString::fromUtf8("帮助"));
    QAction *actHowto = helpMenu->addAction(QString::fromUtf8("使用说明"));
    QAction *actAbout = helpMenu->addAction(QString::fromUtf8("关于"));
    connect(actHowto, &QAction::triggered, this, &MainWindow::onShowHowto);
    connect(actAbout, &QAction::triggered, this, &MainWindow::onShowAbout);
}

QWidget *MainWindow::buildParamCard()
{
    QWidget *inner = new QWidget;
    QGridLayout *g = new QGridLayout(inner);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(6);
    g->setVerticalSpacing(3);

    auto addLabel = [g](int row, int col, const QString &text) {
        QLabel *l = new QLabel(text);
        l->setObjectName("Hint");
        g->addWidget(l, row, col);
    };

    int r = 0;
    // 第一行: 用户编号 / 存档编号。
    addLabel(r, 0, QString::fromUtf8("用户编号"));
    m_uid = new QSpinBox;
    m_uid->setRange(1, 99);
    m_uid->setValue(1);
    g->addWidget(m_uid, r, 1);
    addLabel(r, 2, QString::fromUtf8("存档编号"));
    m_mode = new QSpinBox;
    m_mode->setRange(1, 99);
    m_mode->setValue(13);
    g->addWidget(m_mode, r, 3);
    ++r;

    // 第二行: 输出种子数 / 场景。
    // 场景框是最宽的一格, 把它和数字框放在同一列 (第 3 列),
    // 第一行就不会出现"数字框只占一小格、右边空一大截"的松垮感。
    addLabel(r, 0, QString::fromUtf8("输出种子数"));
    m_outputSize = new QSpinBox;
    m_outputSize->setRange(1, 500);
    m_outputSize->setValue(20);
    g->addWidget(m_outputSize, r, 1);
    addLabel(r, 2, QString::fromUtf8("场景"));
    m_scene = new QComboBox;
    m_scene->addItems(sceneDisplayNames());
    m_scene->setCurrentIndex(2);
    g->addWidget(m_scene, r, 3);
    connect(m_scene, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onSceneChanged);
    ++r;

    // 第三行: GPU 开关, 独占一行。
    // GPU 开关: 只有真的编进了 CUDA 后端并且有可用设备时才可选,
    // 否则置灰勾不上 (此时引擎本来也只会走 CPU)。
    m_useGpu = new QCheckBox(QString::fromUtf8("用 GPU 加速"));
    m_useGpu->setChecked(sf::gpuBackendReady());
    m_useGpu->setEnabled(sf::gpuBackendReady());
    g->addWidget(m_useGpu, r, 0, 1, 4);
    ++r;

    // 三个数字框与场景框都取"刚好放得下内容"的宽度:
    //   数字框最小能放下 99 与上下箭头 (minimumSizeHint),
    //   场景框最小能放下最长的 "MGE/AQE"。
    // 量之前必须先 ensurePolished(): 样式表 (padding / drop-down 宽度) 是在
    // polish 时才套上去的, 不先 polish 量到的是没算内边距的旧值,
    // 场景框会被多算十几像素 (实测 131 而不是 117)。
    for (QWidget *w : {static_cast<QWidget *>(m_uid), static_cast<QWidget *>(m_mode),
                       static_cast<QWidget *>(m_outputSize), static_cast<QWidget *>(m_scene)}) {
        w->ensurePolished();
    }
    // 同一列里的控件取同一个宽度, 右边缘才对得齐:
    //   第 1 列 (用户编号/输出种子数) 取两个数字框里更宽的那个,
    //   第 3 列 (存档编号/场景) 取场景框的宽度 (它天生最宽)。
    int col1 = 0;
    for (QSpinBox *sp : {m_uid, m_outputSize}) {
        col1 = std::max(col1, sp->minimumSizeHint().width() + 2);
    }
    const int col3 = m_scene->minimumSizeHint().width() + 2;
    for (QWidget *w : {static_cast<QWidget *>(m_uid), static_cast<QWidget *>(m_outputSize)}) {
        w->setFixedWidth(col1);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    for (QWidget *w : {static_cast<QWidget *>(m_mode), static_cast<QWidget *>(m_scene)}) {
        w->setFixedWidth(col3);
        w->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    // 状态文字: 按 v1.2 的布局放在「输出种子数」和「开始查找」之间。
    // 不换行 —— 查找中这行每秒都在刷新, 一换行卡片高度就跟着跳, 下面的进度条
    // 和按钮会上下抖。所以文案必须自己短到一行放得下 (见 onProgress)。
    m_status = new QLabel(QString::fromUtf8("就绪"));
    m_status->setObjectName("Stat");
    m_status->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_status->setWordWrap(false);
    m_status->setMinimumHeight(14);

    // 进度条: 单独占一行, 固定在卡片上的位置, 一直显示。
    // 以前是隐藏的、开始查找才出现, 结果是左栏高度在"点开始"和"跑完"两个时刻
    // 各变一次, 会闪一下甚至临时撑出滚动条。常驻就没这个问题了。
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(16);

    // 按钮行。三个按钮等宽 (1:1:1): 原来"开始查找"占两份,
    // 一行看过去宽度不均, 也不利于把左栏收窄。
    QHBoxLayout *btns = new QHBoxLayout;
    btns->setSpacing(6);
    m_startBtn = new QPushButton(QString::fromUtf8("开始查找"));
    connect(m_startBtn, &QPushButton::clicked, this, &MainWindow::onStart);
    // 暂停键: 点一下原地暂停(文字变"继续"), 再点接着算。
    // 只在查找进行中可用, 暂停期间"开始查找"保持禁用, 避免暂停中又开一轮。
    m_pauseBtn = new QPushButton(QString::fromUtf8("暂停"));
    m_pauseBtn->setEnabled(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseToggle);
    m_stopBtn = new QPushButton(QString::fromUtf8("停止"));
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);

    // 状态文字在上, 进度条单独一行在下 (常驻, 不随查找显隐)。
    QVBoxLayout *wrap = new QVBoxLayout;
    wrap->setContentsMargins(0, 0, 0, 0);
    wrap->setSpacing(3);
    // 参数网格按内容定宽, 外面套一层把多余宽度全推到右边:
    // 卡片的右边缘因此与下面两张条件卡对齐, 卡里的控件又不会被拉宽。
    QHBoxLayout *gridRow = new QHBoxLayout;
    gridRow->setContentsMargins(0, 0, 0, 0);
    gridRow->setSpacing(0);
    gridRow->addWidget(inner);
    gridRow->addStretch(1);
    wrap->addLayout(gridRow);
    wrap->addWidget(m_status);
    wrap->addWidget(m_progress);
    btns->addWidget(m_startBtn, 1);
    btns->addWidget(m_pauseBtn, 1);
    btns->addWidget(m_stopBtn, 1);
    wrap->addLayout(btns);

    QWidget *holder = new QWidget;
    holder->setLayout(wrap);
    QFrame *card = ui::makeCard(QString::fromUtf8("搜索参数"), holder);

    // 把"这一摞刚好放下需要多宽"算出来, 给 buildUi 定左栏宽度用。
    // 不手算: 网格里标签列/控件列的宽度、列间距、卡片内边距、边框宽度各有各的
    // 来源, 手算一次就会漏一项 (漏了 3 处列间距, 结果卡片比可见区宽 2px,
    // 右边被裁掉一小条)。直接问 Qt 要卡片自己的 sizeHint 最准。
    // 必须先 ensurePolished + adjustSize: 窗口还没 show 前布局不会跑,
    // 那时候 sizeHint 还是初始值。
    card->ensurePolished();
    card->adjustSize();
    // +2: 表格/滚动区边框与取整带来的 1~2px 误差, 宁可多留这一点也别让
    // 卡片右侧被裁掉一条 (横向滚动条是关掉的, 裁了就真的看不到了)。
    m_leftWidth = std::max(kLeftWidth, card->sizeHint().width() + 2);

    // 卡片的宽度跟下面两张条件卡一致 (都占满左栏), 右边缘才对得齐 —— 这是
    // 外面那层"三张卡竖着排"的布局决定的, 这里不再单独给 Maximum 让卡片缩到
    // 284: 缩了之后它比条件卡窄一截, 一排看过去右边是台阶状的。
    // 卡里的控件仍然是"刚好放下"的宽度 (见上面 setFixedWidth), 多出来的空间
    // 被 gridRow 末尾那个 stretch 吃掉, 落在控件右边。
    return card;
}

QWidget *MainWindow::buildBandCard()
{
    return buildRuleCard(QString::fromUtf8("出怪条件"), false);
}

QWidget *MainWindow::buildGlobalCard()
{
    return buildRuleCard(QString::fromUtf8("全局条件"), true);
}

QWidget *MainWindow::buildRuleCard(const QString &title, bool global)
{
    QWidget *inner = new QWidget;
    QVBoxLayout *lay = new QVBoxLayout(inner);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    ConditionTable *table = makeRuleTable(kConditionTableHeight);
    if (global) {
        m_globalTable = table;
        connect(table, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) {
            onEditGlobal();
        });
    } else {
        m_bandTable = table;
        connect(table, &QTableWidget::doubleClicked, this, [this](const QModelIndex &) {
            onEditBand();
        });
    }
    lay->addWidget(table, 1);

    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing(6);
    QPushButton *add = new QPushButton(QString::fromUtf8("添加"));
    QPushButton *del = new QPushButton(QString::fromUtf8("删除"));
    if (global) {
        connect(add, &QPushButton::clicked, this, &MainWindow::onAddGlobal);
        connect(del, &QPushButton::clicked, this, &MainWindow::onRemoveGlobal);
    } else {
        connect(add, &QPushButton::clicked, this, &MainWindow::onAddBand);
        connect(del, &QPushButton::clicked, this, &MainWindow::onRemoveBand);
    }
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch(1);
    lay->addLayout(row);

    return ui::makeCard(title, inner);
}

QWidget *MainWindow::buildResultCard()
{
    // 标题、种子输入框、"查看出怪" 全部放在同一行: 纵向上省下一整行,
    // 右栏能多显示一行结果。
    QWidget *topRow = new QWidget;
    QHBoxLayout *top = new QHBoxLayout(topRow);
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(6);

    top->addWidget(ui::cardTitle(QString::fromUtf8("查找结果")));

    // 种子输入框: 手动输入一个种子, 直接看它在条件覆盖范围内的出怪。
    // 不加"种子"两个字的小标签: placeholder 已经说明了要填什么。
    const QFont mono(QString::fromUtf8("Consolas"), 10);
    const QFontMetrics monoFm(mono);

    m_seedInput = new QLineEdit;
    m_seedInput->setPlaceholderText(QString::fromUtf8("输入种子"));
    m_seedInput->setMaxLength(8);
    m_seedInput->setFont(mono);
    // 宽度交给布局去分, 自己只定上下限:
    //   上限 = 8 位十六进制 + 左右内边距, 再多也是空的;
    //   下限 = 还能看清 placeholder 的程度, 再窄就没意义了。
    // 不用 setFixedWidth 硬算: 不同字体度量下标题/按钮的宽度不一样,
    // 硬算的"剩余空间"在原生字体下会比实际更小, 把输入框压成一条缝。
    const int full = monoFm.width(QString::fromUtf8("00000000")) + 14;
    m_seedInput->setMinimumWidth(72);
    m_seedInput->setMaximumWidth(full);
    m_seedInput->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // 只允许 16 进制字符
    m_seedInput->setValidator(new QRegExpValidator(QRegExp(QString::fromUtf8("[0-9A-Fa-f]{1,8}")), this));
    connect(m_seedInput, &QLineEdit::returnPressed, this, &MainWindow::onLookupSeed);
    top->addWidget(m_seedInput, 1);

    QPushButton *lookup = new QPushButton(QString::fromUtf8("查看出怪"));
    connect(lookup, &QPushButton::clicked, this, &MainWindow::onLookupSeed);
    top->addWidget(lookup);
    // 挤不下时先压输入框, 不压标题与按钮。
    top->setStretch(1, 1);

    m_resultTable = new ResultTable;
    m_resultTable->setColumnCount(2);
    m_resultTable->setHorizontalHeaderLabels(QStringList()
        << QString::fromUtf8("种子") << QString::fromUtf8("满足轮数"));
    ui::setupTable(m_resultTable);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    // 两列都由 ResultTable::relayout() 摆: 「满足轮数」定宽 84 (表头"满足轮数"
    // 加粗后 72px, 加左右各 4px 内边距是 80, 留 4px 余量), 剩下的全给"种子"。
    // 不用 Stretch: 它会把窗口剩余宽度全吃掉, 主界面因此收不窄。
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_resultTable->horizontalHeader()->setStretchLastSection(false);
    m_resultTable->setColumnWidth(1, 84);
    // 结果表与左侧两张条件表共用同一个表格基类 (ui::FloatScrollTable):
    // 竖条由 Qt 独占极右一整列、横条独占底部一整行, 结果一多不会看到整张表横向抖。
    m_resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_resultTable->setFont(QFont(QString::fromUtf8("Consolas"), 10));
    connect(m_resultTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onSeedPicked);
    connect(m_resultTable, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *it) {
        if (!it) return;
        QTableWidgetItem *first = m_resultTable->item(it->row(), 0);
        if (first) showDetailFor(quint32(first->text().toUInt(nullptr, 16)));
    });

    // 详情不再占主界面: 双击结果行 (或点"查看出怪") 时弹独立窗口
    return ui::makeCard(topRow, m_resultTable);
}

// ---------------------------------------------------------------------------
//  参数
// ---------------------------------------------------------------------------
int MainWindow::currentScene() const
{
    const QVector<int> ids = sceneIds();
    const int idx = m_scene->currentIndex();
    if (idx < 0 || idx >= ids.size()) return 2;
    return ids[idx];
}

// 扫描区间 = 所有条件(出怪条件 + 全局条件)覆盖的 F 数范围之并
void MainWindow::effectiveRange(int *beginFlag, int *endFlag) const
{
    int b = -1, e = -1;
    const auto take = [&b, &e](const QVector<sf::RuleBand> &list) {
        for (int i = 0; i < list.size(); ++i) {
            const sf::RuleBand &x = list[i];
            if (x.endFlag <= x.beginFlag) continue;
            if (b < 0 || x.beginFlag < b) b = x.beginFlag;
            if (e < 0 || x.endFlag > e) e = x.endFlag;
        }
    };
    take(m_bands);
    take(m_hard);
    if (beginFlag) *beginFlag = b < 0 ? 0 : b;
    if (endFlag) *endFlag = e < 0 ? 0 : e;
}

sf::SearchSpec MainWindow::currentSpec(QString *error) const
{
    sf::SearchSpec spec;
    spec.uid = m_uid->value();
    spec.mode = m_mode->value();
    spec.scene = currentScene();

    int b = 0, e = 0;
    effectiveRange(&b, &e);
    spec.beginFlag = b;
    spec.endFlag = e;

    // 没有任何条件时区间必然是 0/0, 先判这一条, 免得用户看到"区间无效"这种不着边际的提示
    if (m_bands.isEmpty() && m_hard.isEmpty()) {
        if (error) *error = QString::fromUtf8("请至少添加一条出怪条件");
        return spec;
    }
    if (spec.endFlag <= spec.beginFlag) {
        // 有条件但范围为空: 例如所有带的结束轮都不大于起始轮
        if (error) *error = QString::fromUtf8("出怪条件的范围无效，终止轮必须大于起始轮");
        return spec;
    }

    // 从 0 扫到 FFFFFFFF: 检查全部种子, 不再随机抽样
    spec.seedBegin = 0;
    spec.span = m_spanOverride > 0 ? m_spanOverride : 0x100000000ULL;
    spec.outputSize = m_outputSize->value();
    // 结果表要够 outputSize 个种子: 集齐这么多"全部满足"的种子就够了, 提前收工
    spec.autoStop = quint64(m_outputSize->value());
    spec.bands = m_bands;
    spec.hard = m_hard;
    // 没勾"用 GPU 加速"或者后端不可用时走 CPU。引擎自己也会在小规模检索时
    // 退回 CPU, 所以这里只是用户的显式选择。
    spec.gpu = (m_useGpu && m_useGpu->isChecked()) ? 1 : 0;

    return spec;
}

// 条件合法性检查: 空串 = 合法; 否则返回可读的错误说明。
// 具体规则在引擎的 sf::checkBand 里 (对话框点"确定"时用的是同一个函数),
// 这里只负责把两张表都过一遍——保留它是为了兜底, 例如从预设文件读到
// 一套没经过对话框的脏条件。
QString MainWindow::checkConditions(QString *error) const
{
    if (error) error->clear();
    if (m_bands.isEmpty() && m_hard.isEmpty()) {
        return QString::fromUtf8("请先添加至少一条出怪条件。");
    }

    const int scene = currentScene();
    const QString bandKind = QString::fromUtf8("出怪条件");
    const QString hardKind = QString::fromUtf8("全局条件");

    for (int i = 0; i < m_bands.size(); ++i) {
        const QString bad = sf::checkBand(m_bands[i], scene, bandKind, m_bands);
        if (!bad.isEmpty()) return bad;
    }
    for (int i = 0; i < m_hard.size(); ++i) {
        const QString bad = sf::checkBand(m_hard[i], scene, hardKind, m_hard);
        if (!bad.isEmpty()) return bad;
    }
    return QString();
}

// setRunning 里要跟着灰掉的那批控件: 只会改到"查找参数或条件"的那一类。
// 三个运行按钮自己按状态分别处理, 结果表也不锁 (它就是这次查找的输出)。
static bool isLockableByRun(QWidget *w)
{
    return qobject_cast<QPushButton *>(w) != nullptr
        || qobject_cast<QSpinBox *>(w) != nullptr
        || qobject_cast<QComboBox *>(w) != nullptr
        || qobject_cast<QCheckBox *>(w) != nullptr
        || qobject_cast<QLineEdit *>(w) != nullptr;
}

void MainWindow::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setEnabled(!running);
    m_pauseBtn->setEnabled(running);
    m_stopBtn->setEnabled(running);
    m_startBtn->setText(running ? QString::fromUtf8("查找中…") : QString::fromUtf8("开始查找"));

    // 查找期间把其余控件全部灰掉, 直到查找结束或点「停止」。
    // 查找用的是一次性快照, 中途改条件/参数不会影响这一轮, 反而让人以为生效了;
    // 更糟的是把条件删空以后再点「查看出怪」, 详情里会是一片空表。
    // 用类型扫一遍而不是逐个成员开关: 参数区的控件、两张条件表的「添加/删除」、
    // 结果卡的「查看出怪」现在都是局部变量, 一个个提成成员既啰嗦又容易漏。
    // 只处理本窗口内的控件 —— 详情窗口是 MainWindow 的子对象, 它自己的「关闭」
    // 按钮不该被这里锁掉。
    const QList<QWidget *> all = findChildren<QWidget *>();
    for (QWidget *w : all) {
        if (!w || w->window() != this) continue;
        if (w == m_startBtn || w == m_pauseBtn || w == m_stopBtn) continue;
        if (!isLockableByRun(w)) continue;
        if (w == m_useGpu) {
            // GPU 开关还多一层: 后端没编进来时它本来就该是灰的
            w->setEnabled(!running && sf::gpuBackendReady());
            continue;
        }
        w->setEnabled(!running);
    }
    // 条件表本身也锁住: 双击行会弹编辑对话框, 光锁按钮拦不住
    if (m_bandTable) m_bandTable->setEnabled(!running);
    if (m_globalTable) m_globalTable->setEnabled(!running);
    // 菜单里的「保存/载入/删除预设」会整体替换条件, 同样锁掉
    if (QMenuBar *mb = menuBar()) mb->setEnabled(!running);
}

// ---------------------------------------------------------------------------
//  列表刷新
// ---------------------------------------------------------------------------
static void fillRuleRows(QTableWidget *table, const QVector<sf::RuleBand> &bands)
{
    const int n = bands.size();
    table->setRowCount(n);
    for (int i = 0; i < n; ++i) {
        const sf::RuleBand &b = bands.at(i);

        QTableWidgetItem *rng = new QTableWidgetItem(
            sf::flagRangeText(b.beginFlag, b.endFlag));
        rng->setTextAlignment(Qt::AlignCenter);
        rng->setForeground(QColor(0x11, 0x11, 0x11));
        QFont f = rng->font();
        f.setBold(true);
        rng->setFont(f);
        table->setItem(i, 0, rng);

        QTableWidgetItem *cond = new QTableWidgetItem(
            b.cond.isEmpty() ? QString::fromUtf8("（无限制）") : b.cond.shortText());
        table->setItem(i, 1, cond);
    }

    // 条件列按实际内容取宽度 (下限 kConditionMinWidth): 长条件不会变成省略号,
    // 超出表格可见宽度时出现横向滚动条; 内容比剩余空间窄时由表格自己撑满。
    // 用字体量而不是 sizeHintForColumn —— 那个是 QTableView 的 protected 成员。
    //
    // 量之前必须 ensurePolished: QSS 里的字体 (* { font-size: 11px }) 是在 polish
    // 时才写进控件的, 而 fillRuleRows 在构造期间就会被调用, 那时 table->font()
    // 还是系统默认字体, 量出来的宽度和实际画出来的对不上 —— 条件列因此被算窄,
    // 末尾就出现省略号 ("至多2(梯篮白…")。
    table->ensurePolished();
    const QFontMetrics fm(table->font());
    int widest = 0;
    for (int i = 0; i < n; ++i) {
        QTableWidgetItem *it = table->item(i, 1);
        if (it) widest = std::max(widest, fm.width(it->text()));
    }
    // 两张条件表都是 ConditionTable, 直接向下转 (没有 Q_OBJECT, 用不了 qobject_cast)
    ConditionTable *ct = static_cast<ConditionTable *>(table);
    // 4 * 2 是 QSS 里 QTableWidget::item 的左右 padding, 再留 8px 给字体的
    // hinting 与格线, 否则最后一两个字仍有可能被挤掉。
    ct->setColumnNeeds(rangeColumnWidth(table->font()),
                       std::max(kConditionMinWidth, widest + 4 * 2 + 8));
}

void MainWindow::refreshBandTable() { fillRuleRows(m_bandTable, m_bands); }

void MainWindow::refreshGlobalTable() { fillRuleRows(m_globalTable, m_hard); }

// ---------------------------------------------------------------------------
//  出怪条件操作
// ---------------------------------------------------------------------------
void MainWindow::onAddBand()
{
    BandDialog dlg(currentScene(), kOuterBegin, kOuterEnd, this);
    // 同时把已有条件交给对话框, 点"确定"时就能拦下跨段矛盾 (不用等开始查找)
    dlg.setSiblings(m_bands);
    // 不预填范围: 两个框留空("—"), 由用户自己填, 点"确定"时空着会报错。
    // 以前这里塞了 3~12 的默认值, 用户不留神就带着一个没想过的区间去查。
    if (dlg.exec() != QDialog::Accepted) return;

    sf::RuleBand b = dlg.band();
    m_bands << b;
    refreshBandTable();
    m_bandTable->selectRow(m_bands.size() - 1);
}

void MainWindow::onEditBand()
{
    const int row = m_bandTable->currentRow();
    if (row < 0 || row >= m_bands.size()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选中一条出怪条件。"));
        return;
    }
    BandDialog dlg(currentScene(), kOuterBegin, kOuterEnd, this);
    // 兄弟列表要排除自己, 否则"自己跟自己重叠"永远成立
    QVector<sf::RuleBand> others = m_bands;
    others.remove(row);
    dlg.setSiblings(others);
    dlg.setBand(m_bands[row]);
    if (dlg.exec() != QDialog::Accepted) return;
    sf::RuleBand b = dlg.band();
    m_bands[row] = b;
    refreshBandTable();
    m_bandTable->selectRow(row);
}

void MainWindow::onRemoveBand()
{
    const int row = m_bandTable->currentRow();
    if (row < 0 || row >= m_bands.size()) return;
    m_bands.remove(row);
    refreshBandTable();
}

// 全局条件: 与出怪条件同样的分段输入, 但语义是"这几轮必须全部满足"
void MainWindow::onAddGlobal()
{
    BandDialog dlg(currentScene(), kOuterBegin, kOuterEnd, this);
    dlg.setHardMode(true);
    dlg.setSiblings(m_hard);
    // 同样不预填范围, 留空让用户填
    if (dlg.exec() != QDialog::Accepted) return;

    sf::RuleBand b = dlg.band();
    m_hard << b;
    refreshGlobalTable();
    m_globalTable->selectRow(m_hard.size() - 1);
}

void MainWindow::onEditGlobal()
{
    const int row = m_globalTable->currentRow();
    if (row < 0 || row >= m_hard.size()) {
        QMessageBox::information(this, QString::fromUtf8("提示"),
                                 QString::fromUtf8("请先选中一条全局条件。"));
        return;
    }
    BandDialog dlg(currentScene(), kOuterBegin, kOuterEnd, this);
    dlg.setHardMode(true);
    QVector<sf::RuleBand> others = m_hard;
    others.remove(row);
    dlg.setSiblings(others);
    dlg.setBand(m_hard[row]);
    if (dlg.exec() != QDialog::Accepted) return;
    sf::RuleBand b = dlg.band();
    m_hard[row] = b;
    refreshGlobalTable();
    m_globalTable->selectRow(row);
}

void MainWindow::onRemoveGlobal()
{
    const int row = m_globalTable->currentRow();
    if (row < 0 || row >= m_hard.size()) return;
    m_hard.remove(row);
    refreshGlobalTable();
}

void MainWindow::onSceneChanged(int)
{
    // 场景一变, 原来条件里"这个场景不会出现的僵尸"就变成幽灵位:
    // 对话框里看不见(它按本场景的僵尸列表建行), 也就点不掉, 但 matches() 仍然
    // 拿它去过滤种子 —— 结果是"查不到本该查到的种子". 这里把这类位裁掉。
    const quint32 visible = sceneZombieMask(currentScene());

    auto prune = [visible](QVector<sf::RuleBand> &list) {
        for (int i = 0; i < list.size(); ++i) {
            sf::WaveCondition &c = list[i].cond;
            c.needMask &= visible;
            c.refuseMask &= visible;
            c.atLeastMask &= visible;
            c.atMostMask &= visible;
            c.sanitize();   // 重算 needTotal, 并把 atLeast/atMost 夹回合法范围
        }
    };
    prune(m_bands);
    prune(m_hard);

    refreshBandTable();
    refreshGlobalTable();
    m_status->setText(QString::fromUtf8("已切换场景：%1").arg(m_scene->currentText()));
}

// ---------------------------------------------------------------------------
//  检查
// ---------------------------------------------------------------------------
void MainWindow::onStart()
{
    if (m_running) return;

    QString error;
    const sf::SearchSpec spec = currentSpec(&error);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("参数有误"), error);
        return;
    }

    // 非法条件拦截: 例如"3~4 F 必出红眼"在任何种子里都不可能满足, 直接提示, 不开查找
    QString bad = checkConditions(&error);
    if (!bad.isEmpty()) {
        std::fprintf(stderr, "conditions rejected: %s\n", bad.toUtf8().constData());
        QMessageBox::warning(this, QString::fromUtf8("条件无法满足"), bad);
        return;
    }

    sf::RuleTable rank, hard;
    rank.build(spec.bands, spec.beginFlag / 2, spec.endFlag / 2);
    hard.build(spec.hard, spec.beginFlag / 2, spec.endFlag / 2);
    if (rank.empty() && hard.empty()) {
        QMessageBox::warning(this, QString::fromUtf8("缺少条件"),
                             QString::fromUtf8("请先给至少一条出怪条件设置具体要求。"));
        return;
    }

    m_resultTable->setRowCount(0);
    if (m_detailDlg) m_detailDlg->clear();
    m_progress->setValue(0);
    m_lastResult = sf::SearchResult();
    // 记下这一次查找用的"输出种子数": 结果表按它铺行, 之后用户再去改控件
    // 不会让已经出来的结果对不上。
    m_outputSizeAtRun = m_outputSize ? m_outputSize->value() : 20;
    // 上一轮可能停在"继续"上, 新的一轮要从"暂停"这个初始状态开始
    m_paused = false;
    m_pausedMs = 0;
    m_pauseBtn->setText(QString::fromUtf8("暂停"));
    setRunning(true);
    m_runClock.start();
    m_status->setText(QString::fromUtf8("正在查找…"));

    m_thread = new QThread(this);
    m_worker = new SearchWorker(spec);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &SearchWorker::run);
    connect(m_worker, &SearchWorker::progress, this, &MainWindow::onProgress);
    connect(m_worker, &SearchWorker::finished, this, &MainWindow::onFinished);
    connect(m_worker, &SearchWorker::finished, m_thread, &QThread::quit);
    // 标准的跨线程释放写法: deleteLater 投到 worker 自己所在的线程,
    // 线程事件循环结束时会被统一处理 (在 GUI 线程里对已停止线程的对象调
    // deleteLater 永远不会执行)
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this, th = m_thread, w = m_worker]() {
        // 只清掉"自己这一代"的指针: 旧线程的 finished 可能晚于新一轮查找到来
        if (m_worker == w) m_worker = nullptr;
        if (m_thread == th) m_thread = nullptr;
        th->deleteLater();
    });
    m_thread->start();
}

void MainWindow::onStop()
{
    // 先把暂停解掉: 工作线程在暂停循环里等的是"暂停标志", 只有它清掉才能走到
    // cancel 判断上去, 否则按"停止"会一直卡着。
    m_paused = false;
    if (m_pauseBtn) m_pauseBtn->setText(QString::fromUtf8("暂停"));
    if (m_worker) {
        m_worker->setPaused(false);
        m_worker->requestStop();
    }
    m_status->setText(QString::fromUtf8("正在停止…"));
}

void MainWindow::onPauseToggle()
{
    if (!m_worker) return;
    m_paused = !m_paused;
    m_worker->setPaused(m_paused);
    m_pauseBtn->setText(m_paused ? QString::fromUtf8("继续") : QString::fromUtf8("暂停"));
    if (m_paused) {
        m_pauseClock.start();
        onProgress(m_progress->value(), 0, 0);
    } else {
        // 暂停时长累计起来, 估算剩余时间时要从已用时间里扣掉, 否则一条"约剩 5 分钟"
        // 会随着暂停一起涨上去。
        m_pausedMs += m_pauseClock.elapsed();
    }
}

void MainWindow::onProgress(int percent, quint64 scanned, quint64 qualified)
{
    // scanned (已检查种子数) 不再显示: 它是"工作量", 不是用户关心的结果。
    // 用户要看的是"找到多少满足条件的", 所以换成第三个参数 qualified ——
    // 引擎上报的是"出怪条件和全局条件都满足"的种子数。
    Q_UNUSED(scanned);
    m_progress->setValue(percent);

    // 命中数用亿/万缩写。全量扫描且没挂全局条件时 qualified 能到十位数,
    // 完整数字会把状态行顶到换行, 一换行卡片高度就跳, 进度条和按钮跟着抖。
    // shortNumber 的缩写自带一个空格 ("1234.6 万"), 后面接 "个" 时不再补空格。
    const QString hitText =
        QString::fromUtf8("，已找到 %1个满足").arg(shortNumber(qualified));

    // 暂停时不再估时间: 已用时间停在这儿了, 报"约剩 N 秒"会一直在跳。
    if (m_paused) {
        m_status->setText(QString::fromUtf8("已暂停（已完成 %1%）").arg(percent) + hitText);
        return;
    }

    if (percent >= 100) {
        m_status->setText(QString::fromUtf8("正在收尾…"));
        return;
    }
    if (!m_runClock.isValid() || percent < 1) {
        m_status->setText(QString::fromUtf8("正在查找…已完成 %1%").arg(percent) + hitText);
        return;
    }

    const double sec = double(m_runClock.elapsed() - m_pausedMs) / 1000.0;
    const double left = sec / double(percent) * double(100 - percent);
    QString eta;
    if (left >= 60.0) {
        eta = QString::fromUtf8("约剩 %1 分 %2 秒")
                  .arg(int(left / 60.0))
                  .arg(int(left - 60.0 * int(left / 60.0) + 0.5));
    } else {
        eta = QString::fromUtf8("约剩 %1 秒").arg(int(left + 0.5));
    }
    m_status->setText(QString::fromUtf8("已完成 %1%，%2").arg(percent).arg(eta) + hitText);
}

void MainWindow::onFinished(const sf::SearchResult &result)
{
    m_lastResult = result;
    m_paused = false;
    if (m_pauseBtn) m_pauseBtn->setText(QString::fromUtf8("暂停"));
    setRunning(false);
    m_progress->setValue(100);

    if (!result.error.isEmpty()) {
        m_status->setText(result.error);
        if (m_shotPath.isEmpty()) {
            QMessageBox::warning(this, QString::fromUtf8("查找失败"), result.error);
        } else {
            // --runshot 模式下不能弹模态框, 否则自动化流程挂死
            std::printf("run error: %s\n", result.error.toUtf8().constData());
        }
        finishShotIfNeeded();
        return;
    }

    fillResultTable();

    const quint64 full = result.fullCount();
    const int best = result.buckets.isEmpty() ? -1 : result.buckets.first().matched;
    m_status->setText(statusLine(best, result.activeWaves, result.seconds,
                                 result.usedGpu, result.stopped, result.enoughFound));

    if (m_resultTable->rowCount() == 0) {
        if (m_detailDlg) m_detailDlg->clear();
        if (m_shotPath.isEmpty() && !result.stopped) {
            QMessageBox::information(
                this, QString::fromUtf8("没有结果"),
                QString::fromUtf8("没有任何种子满足条件，可以放宽出怪条件，或缩小轮次区间。"));
        }
    } else if (full == 0 && m_shotPath.isEmpty() && !result.stopped) {
        // 没有"全部满足"的种子时必须明确提示, 免得用户以为程序没跑出东西
        QMessageBox::information(
            this, QString::fromUtf8("没有完全满足的种子"),
            noFullMatchMessage(best, result.activeWaves));
    }

    finishShotIfNeeded();
}

// 完成态的状态行。抽成静态纯函数, 测试可以直接核对文案, 不用真的跑一遍全量查找。
// 只报"最好几轮 / 共几轮 / 几秒": 扫了多少个种子是固定值(整个区间), 写出来既长
// 又会把状态行顶到换行, 而"差几轮"才是用户真正要判断的东西。
QString MainWindow::statusLine(int best, int activeWaves, double seconds,
                               bool gpu, bool stopped, bool early)
{
    QString text;
    if (best >= 0 && activeWaves > 0) {
        // 有种子全部满足时 best 就等于 activeWaves, 自然显示成 29/29 的比例
        text = QString::fromUtf8("最好 %1/%2，%3 秒")
                   .arg(best)
                   .arg(activeWaves)
                   .arg(seconds, 0, 'f', 2);
    } else {
        text = QString::fromUtf8("无结果，%1 秒").arg(seconds, 0, 'f', 2);
    }
    if (stopped) {
        text += QString::fromUtf8("，已中断");
    } else if (early) {
        text += QString::fromUtf8("，早停");
    }
    // 让用户看得出这一轮到底是 GPU 算的还是 CPU 算的 ——
    // 小规模检索和没勾"用 GPU 加速"时都会走 CPU, 不标出来容易误以为 GPU 生效了。
    if (gpu) text.prepend(QStringLiteral("GPU · "));
    return text;
}

// "没有完全满足的种子" 弹窗的正文。
// 不再写"已检查 4,294,967,296 个种子": 那个数是整个区间的大小, 永远不变, 没有信息量;
// 用户要判断的是"离满足还差几轮", 所以只留最好的一档。
QString MainWindow::noFullMatchMessage(int best, int activeWaves)
{
    return QString::fromUtf8("已检查全部种子，最好的一档为 %1 / %2 轮。")
        .arg(std::max(0, best))
        .arg(activeWaves);
}

// 截图模式下的统一收尾: 无论成功还是失败都截图并退出, 避免自动化流程挂死
void MainWindow::finishShotIfNeeded()
{
    if (m_shotPath.isEmpty()) return;
    const QString path = m_shotPath;
    m_shotPath.clear();
    QTimer::singleShot(500, this, [this, path]() {
        const bool ok = grab().save(path);
        std::printf("shot %s -> %s\n", path.toUtf8().constData(), ok ? "ok" : "failed");
        QCoreApplication::quit();
    });
}

// 测试用: 让进度条显示出来并停在某个百分比, 用于截图确认样式。
// qualified 给一个真实的命中数, 否则截图里状态行永远是"已找到 0个满足"。
void MainWindow::showProgressForTest(int percent, quint64 qualified)
{
    if (!m_progress) return;
    m_progress->setValue(std::max(0, std::min(100, percent)));
    m_runClock.start();
    onProgress(std::max(0, std::min(100, percent)), 0, qualified);
}

// 测试用: 直接造出"查找中/已暂停"那一行状态文字, 用来量它会不会在左栏里换行。
// qualified 给真实量级的数, 否则测不出最长的那种情况。
void MainWindow::showRunStateForTest(int percent, quint64 qualified, bool paused)
{
    if (!m_status) return;
    m_runClock.start();
    m_paused = paused;
    m_pausedMs = 0;
    onProgress(std::max(0, std::min(100, percent)), 0, qualified);
    m_paused = false;
}

// 测试用: 模拟在种子输入框里打字再点"查看出怪"
void MainWindow::lookupSeedForTest(const QString &hex)
{
    if (m_seedInput) m_seedInput->setText(hex);
    onLookupSeed();
}

// 测试用: 绕过"添加条件"对话框直接塞一组出怪条件。
// 用来构造一行超长条件 ("禁出 20 种僵尸"), 验证条件列是横向滚动而不是被省略号截掉。
void MainWindow::setBandForTest(const QVector<sf::RuleBand> &bands)
{
    m_bands = bands;
    refreshBandTable();
}

// 手动输入种子 -> 看它在条件覆盖范围内的出怪表
void MainWindow::onLookupSeed()
{
    const QString text = m_seedInput ? m_seedInput->text().trimmed() : QString();
    if (text.isEmpty()) {
        QMessageBox::information(this, QString::fromUtf8("种子为空"),
                                 QString::fromUtf8("请先输入一个种子（16 进制，例如 1A2B3C4D）。"));
        return;
    }
    bool ok = false;
    const quint32 seed = text.toUInt(&ok, 16);
    if (!ok) {
        QMessageBox::warning(this, QString::fromUtf8("种子格式有误"),
                             QString::fromUtf8("种子必须是 1~8 位 16 进制数字，例如 1A2B3C4D。"));
        return;
    }

    // 没有条件时给个默认范围, 否则表里一行都没有。
    // 这里必须明确告诉用户"用的是默认范围", 否则很容易以为表里的轮次
    // 就是自己在条件表里写的区间。
    int beginFlag = 0, endFlag = 0;
    effectiveRange(&beginFlag, &endFlag);
    const bool defaultRange = (endFlag <= beginFlag);
    if (defaultRange) {
        beginFlag = 0;
        endFlag = 2 * 25;   // 25 轮全查
    }
    const int beginLevel = beginFlag / 2;
    const int endLevel = std::max(beginLevel + 1, endFlag / 2);

    // 规则表可能是空的(还没点过"开始查找"), 临时按当前条件建一张, 只为着色
    sf::RuleTable rank, hard;
    rank.build(m_bands, beginLevel, endLevel);
    hard.build(m_hard, beginLevel, endLevel);

    const std::vector<sf::DetailRow> rows =
        sf::viewDetail(seed, m_uid->value(), m_mode->value(), currentScene(),
                       beginLevel, endLevel, rank, hard);
    showDetailWindow(seed, rows, rank, hard);
    m_status->setText(QString::fromUtf8("已显示种子 %1 在 %2 到 %3 F 范围内的出怪%4")
                          .arg(ui::hex8(seed))
                          .arg(beginFlag + 1)   // F 数从 1 开始, 内部 flag 从 0 开始
                          .arg(endFlag)
                          .arg(defaultRange
                                   ? QString::fromUtf8("（还没有设置条件，按默认 25 轮显示）")
                                   : QString()));
}

// 详情窗口是常驻的非模态窗口 (参考 1.2 版): 第一次用的时候建, 之后复用。
// 自动选中第一行时只刷新内容、不抢焦点, 用户自己双击时才抬到最前。
void MainWindow::showDetailWindow(quint32 seed, const std::vector<sf::DetailRow> &rows,
                                  const sf::RuleTable &rank, const sf::RuleTable &hard)
{
    if (!m_detailDlg) m_detailDlg = new DetailDialog(this);
    m_detailDlg->setData(seed, currentScene(), rows, rank, hard);
    if (m_autoSelect) return;          // 自动选中第一行: 只在后台刷新内容
    m_detailDlg->show();
    m_detailDlg->raise();
    m_detailDlg->activateWindow();
}

void MainWindow::onSeedPicked()
{
    const int row = m_resultTable->currentRow();
    if (row < 0) return;
    QTableWidgetItem *it = m_resultTable->item(row, 0);
    if (!it) return;
    // 单击只把种子填进右上角的输入框, 不弹详情窗口。
    // 以前是选中就弹 —— 翻结果列表时每点一行就蹦一个窗口, 很挡视线;
    // 想看详情可以再点一次「查看出怪」, 或者双击这一行。
    if (m_seedInput) m_seedInput->setText(it->text());
}

// 把 m_lastResult 铺进结果表
void MainWindow::fillResultTable()
{
    const sf::SearchResult &result = m_lastResult;

    // "输出种子数"是总共列多少个种子: 从满足轮数最多的档位往下取。
    // 用发起查找时记下的值, 而不是控件当前值 —— 查找结束后用户可能再去调
    // "输出种子数", 那时表格应该反映这次查找实际用的设置, 不该被后来的输入改掉。
    const int want = m_outputSizeAtRun > 0 ? m_outputSizeAtRun : 20;

    QSignalBlocker block(m_resultTable);
    m_resultTable->setRowCount(0);

    int rows = 0;

    // 结果里只会有"满足全部全局条件"的种子: buckets 已按满足轮数降序
    for (int i = 0; i < result.buckets.size() && rows < want; ++i) {
        const sf::CountBucket &b = result.buckets[i];
        const bool full = (b.matched == result.activeWaves);
        // 一轮都没满足的种子不算"成绩", 不列出来;
        // 但只设了全局条件(没有出怪条件)时 activeWaves 就是 0, 这时能留下来的种子
        // 本身就是"全部满足", 不能按 0 轮淘汰, 否则界面会误报"没有结果"。
        if (b.matched == 0 && result.activeWaves > 0) continue;

        for (int k = 0; k < b.examples.size() && rows < want; ++k) {
            m_resultTable->insertRow(rows);
            QTableWidgetItem *seedItem = new QTableWidgetItem(ui::hex8(b.examples[k]));
            seedItem->setTextAlignment(Qt::AlignCenter);
            m_resultTable->setItem(rows, 0, seedItem);

            // 只设全局条件时没有"出怪条件轮数"可数, 显示 "0 / 0" 会让人以为一个都没满足
            QTableWidgetItem *mItem = new QTableWidgetItem(
                result.activeWaves > 0
                    ? QString::fromUtf8("%1 / %2").arg(b.matched).arg(result.activeWaves)
                    : QString::fromUtf8("—"));
            mItem->setTextAlignment(Qt::AlignCenter);
            mItem->setForeground(full ? QColor(0x11, 0x11, 0x11) : QColor(0x8a, 0x8a, 0x8a));
            QFont mf = mItem->font();
            mf.setBold(full);
            mItem->setFont(mf);
            m_resultTable->setItem(rows, 1, mItem);
            ++rows;
        }
    }
    block.unblock();

    if (rows > 0) {
        // 自动选中第一行只是为了让下方详情有内容可显示,
        // 不能顺带把种子回填进输入框 (用户 m02595 的要求)
        m_autoSelect = true;
        m_resultTable->selectRow(0);
        m_autoSelect = false;
    }
}

// 范围 / 条件表都按"当前界面"重算: 之前直接用缓存的规则表,
// 而它们只在 onStart 里重建, 用户改了条件却没重新查找时会画出过期的条件。
std::vector<sf::DetailRow> MainWindow::detailRows(quint32 seed, sf::RuleTable *rank,
                                                 sf::RuleTable *hard) const
{
    int beginFlag = 0, endFlag = 0;
    effectiveRange(&beginFlag, &endFlag);
    rank->build(m_bands, beginFlag / 2, endFlag / 2);
    hard->build(m_hard, beginFlag / 2, endFlag / 2);
    return sf::viewDetail(seed, m_uid->value(), m_mode->value(), currentScene(),
                          beginFlag / 2, endFlag / 2, *rank, *hard);
}

void MainWindow::showDetailFor(quint32 seed)
{
    sf::RuleTable rank, hard;
    const std::vector<sf::DetailRow> rows = detailRows(seed, &rank, &hard);
    showDetailWindow(seed, rows, rank, hard);

    // 手动选中/双击某个种子时回填输入框, 方便接着微调或复制
    if (!m_autoSelect && m_seedInput) m_seedInput->setText(ui::hex8(seed));
}

// ---------------------------------------------------------------------------
//  帮助
// ---------------------------------------------------------------------------
void MainWindow::onShowHowto() { showHowto(this); }
void MainWindow::onShowAbout() { showAbout(this); }

// ---------------------------------------------------------------------------
//  预设
// ---------------------------------------------------------------------------
Preset MainWindow::currentPreset() const
{
    Preset p;
    p.uid = m_uid->value();
    p.mode = m_mode->value();
    p.scene = currentScene();
    effectiveRange(&p.beginFlag, &p.endFlag);
    p.outputSize = m_outputSize->value();
    p.bands = m_bands;
    p.hard = m_hard;
    return p;
}

void MainWindow::applyPreset(const Preset &p)
{
    {
        const QSignalBlocker b1(m_uid);
        const QSignalBlocker b2(m_mode);
        const QSignalBlocker b3(m_scene);
        const QSignalBlocker b6(m_outputSize);

        m_uid->setValue(p.uid);
        m_mode->setValue(p.mode);
        const int idx = sceneIds().indexOf(p.scene);
        if (idx >= 0) m_scene->setCurrentIndex(idx);
        m_outputSize->setValue(p.outputSize);
    }

    // 普僵每轮必出，已从条件表里移除；旧预设里残留的普僵标记统一清掉,
    // 免得出现一条用户看不见、也改不了的条件把结果全过滤掉。
    // 同理, 预设如果是在别的场景下存的, 里面可能有本场景不会出现的僵尸,
    // 那些位在条件对话框里看不见、点不掉, 必须一起裁掉。
    const quint32 keep = (~(1u << Z_REGULAR)) & sceneZombieMask(currentScene());
    const auto prune = [keep](const QVector<sf::RuleBand> &src) {
        QVector<sf::RuleBand> out = src;
        for (int i = 0; i < out.size(); ++i) {
            sf::WaveCondition &c = out[i].cond;
            c.needMask &= keep;
            c.refuseMask &= keep;
            c.atLeastMask &= keep;
            c.atMostMask &= keep;
            c.sanitize();
        }
        return out;
    };
    m_bands = prune(p.bands);
    m_hard = prune(p.hard);
    refreshBandTable();
    refreshGlobalTable();
    m_status->setText(QString::fromUtf8("已载入预设：%1").arg(p.name));
}

void MainWindow::onSavePreset()
{
    int b = 0, e = 0;
    effectiveRange(&b, &e);
    bool ok = false;
    const QString defName = (e > b)
        ? QString::fromUtf8("%1 · %2 到 %3 F").arg(m_scene->currentText()).arg(b + 1).arg(e)
        : QString::fromUtf8("%1 · 未设条件").arg(m_scene->currentText());
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("保存预设"), QString::fromUtf8("预设名称："),
        QLineEdit::Normal, defName, &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    Preset p = currentPreset();
    p.name = name.trimmed();

    QString error;
    QVector<Preset> list = loadPresets(&error);
    list << p;
    if (!savePresets(list, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("保存失败"), error);
        return;
    }
    m_status->setText(QString::fromUtf8("已保存预设：%1").arg(p.name));
}

void MainWindow::onLoadPreset()
{
    QString error;
    const QVector<Preset> list = loadPresets(&error);
    if (list.isEmpty()) {
        QMessageBox::information(
            this, QString::fromUtf8("没有预设"),
            error.isEmpty() ? QString::fromUtf8("还没有保存过预设。先在“预设 → 保存当前参数”里存一套。")
                            : error);
        return;
    }

    // 载入与删除用的是同一份列表文案 (presetLabels), 两处显示的预设信息才不会走样
    const QStringList names = presetLabels(list);

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, QString::fromUtf8("载入预设"), QString::fromUtf8("选择一套参数："), names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const int idx = names.indexOf(chosen);
    if (idx < 0) return;
    applyPreset(list[idx]);
}

// 删除预设: 之前只能手改 presets.json, 普通用户根本没有这个入口。
// 删之前要确认一次, 并且用完整路径提示删的是哪个文件。
void MainWindow::onDeletePreset()
{
    QString error;
    QVector<Preset> list = loadPresets(&error);
    if (list.isEmpty()) {
        QMessageBox::information(
            this, QString::fromUtf8("没有预设"),
            error.isEmpty() ? QString::fromUtf8("还没有保存过预设，没有可删除的。") : error);
        return;
    }

    const QStringList names = presetLabels(list);

    bool ok = false;
    const QString chosen = QInputDialog::getItem(
        this, QString::fromUtf8("删除预设"), QString::fromUtf8("要删除哪一套参数："),
        names, 0, false, &ok);
    if (!ok || chosen.isEmpty()) return;

    const int idx = names.indexOf(chosen);
    if (idx < 0) return;

    const QString name = list[idx].name;
    const auto answer = QMessageBox::question(
        this, QString::fromUtf8("删除预设"),
        QString::fromUtf8("确定要删除预设「%1」吗？\n\n只会删掉这一套参数，不影响其他预设。").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    list.removeAt(idx);
    if (!savePresets(list, &error)) {
        QMessageBox::warning(this, QString::fromUtf8("删除失败"), error);
        return;
    }
    m_status->setText(QString::fromUtf8("已删除预设：%1").arg(name));
}

// ---------------------------------------------------------------------------
//  自动跑: 跑一次查找 -> 截图 -> 退出 (仅命令行调试用)
// ---------------------------------------------------------------------------
void MainWindow::autoRun(const QString &shotPath)
{
    m_shotPath = shotPath;
    QTimer::singleShot(200, this, &MainWindow::onStart);
    // 命令行自动跑时若弹了模态框(参数/条件校验失败), 定时关掉, 让进程能退出。
    // 模态框弹出后 exec() 会开自己的事件循环, 外层 timer 照常触发, 但 close()
    // 只是隐藏, exec() 不返回——必须 delete 才能让 exec() 返回。
    QTimer::singleShot(1200, this, [this]() {
        const auto boxes = findChildren<QMessageBox *>();
        for (QMessageBox *b : boxes) {
            b->deleteLater();
            b->hide();
        }
        // 关完模态框后若还没开始查找(被拦下), 补一张截图再退出
        if (!m_running && !m_shotPath.isEmpty()) {
            const bool ok = grab().save(m_shotPath);
            std::printf("shot %s -> %s (rejected)\n", m_shotPath.toUtf8().constData(),
                        ok ? "ok" : "failed");
            QCoreApplication::quit();
        }
    });
}
