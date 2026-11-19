#include "banddialog.h"
#include "conddialog.h"
#include "uikit.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// 把范围框里的文本全部选中: 打开对话框后敲数字是"替换整个旧值", 不是在旧值里插入。
//
// 必须手动选: QAbstractSpinBox::focusInEvent 只在 Tab / Backtab 这类焦点原因下才
// selectAll, 而对话框被激活时给的是 ActiveWindowFocusReason, 它不动光标 —— 实测
// 打开后 "11" 的光标停在两个 1 中间 (=1), 用户想把 11 改成 13 会敲成 "131"。
void selectAllText(QSpinBox *sp)
{
    if (!sp) return;
    if (QLineEdit *le = sp->findChild<QLineEdit *>())
        le->setSelection(0, le->text().size());
}

} // namespace

BandDialog::BandDialog(int scene, int outerBegin, int outerEnd, QWidget *parent)
    : QDialog(parent), m_scene(scene), m_outerBegin(outerBegin), m_outerEnd(outerEnd)
{
    // 内部边界是偶数 F 数: beginFlag 对应"起始轮 - 1", endFlag 对应"终止轮"
    // 范围最早只能从 3 F 开始 (第 1 ~ 2 轮固定是普僵+路障/读报, 没有查询意义)
    if (m_outerBegin < 2) m_outerBegin = 2;
    if (m_outerEnd - m_outerBegin < 2) m_outerEnd = m_outerBegin + 2;

    // 标题与最小宽度都交给 buildUi 里的 ui::prepareDialog, 不再在这里各设一次
    // (以前 setWindowTitle 与 prepareDialog 设的是同一个字符串, 改一处忘一处)。
    buildUi();
    refreshSummary();
}

void BandDialog::buildUi()
{
    // 外壳 (标题 / 去"这是什么?"按钮 / 边距与间距) 统一走 uikit
    QVBoxLayout *root = ui::prepareDialog(this, QString::fromUtf8("编辑出怪条件"));

    m_titleRange = ui::cardTitle(QString::fromUtf8("范围"));
    root->addWidget(m_titleRange);

    // 起始 F 取奇数 / 终止 F 取偶数 —— 与出怪计算器一致。
    // 两个框都放开到完整范围, 输入过程中不改写用户敲的数
    // (以前在 valueChanged 里"顺手纠正"会在敲 4 时把前面的框改成 3,
    //  用户想输 40 就永远输不进去); 先后顺序在点"确定"时统一检查。
    const int firstFlag = m_outerBegin + 1;       // 奇数
    const int lastFlag = m_outerEnd;              // 偶数

    // 最小值是"还没填"的哨兵值, 用 setSpecialValueText 显示成"—"。
    // 新建条件时两个框都停在这里, 点"确定"会要求先填范围 —— 以前这里
    // 自动填 3~12, 用户不留神就带着一个自己没想过的区间去查,
    // 查不出东西也不知道是为什么。
    // 哨兵取 firstFlag 前一档, 这样上下键的步进仍然落在正确奇偶上:
    // 起始框 — → 3 → 5 …, 终止框 — → 4 → 6 …
    QHBoxLayout *range = new QHBoxLayout;
    range->setSpacing(8);
    m_begin = new QSpinBox(this);
    m_begin->setRange(firstFlag - 2, lastFlag);
    m_begin->setSpecialValueText(QString::fromUtf8("—"));
    m_begin->setSingleStep(2);
    range->addWidget(m_begin);
    range->addWidget(new QLabel(QString::fromUtf8("到")));
    m_end = new QSpinBox(this);
    m_end->setRange(firstFlag - 1, lastFlag);
    m_end->setSpecialValueText(QString::fromUtf8("—"));
    m_end->setSingleStep(2);
    range->addWidget(m_end);

    // 宽度按"最长的那个数"量, 不写死: 上限从 10000 提到 1000000 以后,
    // 七位数的 "1000000" 在原来 96px 的框里会被裁掉最后一位, 用户看到的是
    // "100000" —— 明明填对了却像填错了。上下箭头与左右内边距的宽度取自
    // uikit 的公共常量 (与 style.cpp 的 QSS 保持一处), 再多留 4px 余量
    // 给不同 DPI 的字体度量误差。
    for (QSpinBox *box : {m_begin, m_end}) {
        box->ensurePolished();
        const QFontMetrics fm(box->font());
        const int need = fm.width(QString::number(lastFlag))
                         + ui::kSpinArrowWidth + ui::kInputPaddingH * 2 + 4;
        box->setFixedWidth(std::max(96, need));
    }
    range->addWidget(new QLabel(QString::fromUtf8("F")));
    range->addStretch(1);
    root->addLayout(range);

    m_titleCond = ui::cardTitle(QString::fromUtf8("出怪条件"));
    root->addWidget(m_titleCond);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    // 浅底摘要框的外观 (底色/边框/内边距/最小高度) 在 style.cpp 的 QLabel#Summary 里,
    // 不在这里内联 —— 内联样式会盖掉全局表, 是这轮统一要清掉的那类写法。
    m_summary->setObjectName("Summary");
    root->addWidget(m_summary);

    m_editBtn = new QPushButton(QString::fromUtf8("设置出怪条件…"), this);
    connect(m_editBtn, &QPushButton::clicked, this, &BandDialog::onEditCondition);
    QHBoxLayout *mid = new QHBoxLayout;
    mid->addWidget(m_editBtn);
    mid->addStretch(1);
    root->addLayout(mid);

    root->addStretch(1);

    // 取消 / 确定: 默认按钮与 autoDefault 由 uikit 统一
    QPushButton *ok = ui::addOkCancelRow(root, this);
    connect(ok, &QPushButton::clicked, this, &BandDialog::onAccept);

    // 两个框都停在哨兵值 ("—"), 由 setBand() 决定是否填入已有区间
    m_begin->setValue(m_begin->minimum());
    m_end->setValue(m_end->minimum());
}

void BandDialog::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    // 显示之后才选: 这时焦点刚落进第一个范围框, 选中范围才能稳定留下来
    // (在构造函数里选会被随后的 focusInEvent 覆盖掉)。
    selectAllText(m_begin);
    selectAllText(m_end);
}

void BandDialog::setHardMode(bool hard)
{
    m_hard = hard;
    // 除窗口标题外, 其余文案与「出怪条件」完全一致 —— 两张表的条件语义本来就相同,
    // 只是用途不同(全局条件再叠一层"每轮都要满足"的淘汰)。校验提示本身不再带
    // "出怪条件"/"全局条件"前缀, 因为窗口标题就在这里写着。
    setWindowTitle(hard ? QString::fromUtf8("编辑全局条件")
                        : QString::fromUtf8("编辑出怪条件"));
}

void BandDialog::setBand(const sf::RuleBand &band)
{
    m_cond = band.cond;
    m_cond.sanitize();

    const int beginRound = band.beginFlag + 1;
    const int endRound = band.endFlag;
    m_begin->setValue(std::max(m_outerBegin + 1, beginRound | 1));    // 夹到奇数
    m_end->setValue(std::min(m_outerEnd, std::max(m_begin->value() + 1, endRound & ~1)));

    refreshSummary();
}

sf::RuleBand BandDialog::band() const
{
    sf::RuleBand b;
    b.beginFlag = m_begin->value() - 1;   // 奇数轮 -> 偶数 F 边界
    b.endFlag = m_end->value();
    b.cond = m_cond;
    return b;
}

void BandDialog::onEditCondition()
{
    CondDialog dlg(m_scene, this);
    dlg.setCondition(m_cond);
    if (dlg.exec() == QDialog::Accepted) {
        m_cond = dlg.condition();
        refreshSummary();
    }
}

void BandDialog::refreshSummary()
{
    if (m_cond.isEmpty()) {
        m_summary->setText(QString());
        return;
    }
    m_summary->setText(m_cond.summary());
}

bool BandDialog::normalizeRange(int outerBegin, int outerEnd,
                                int rawBegin, int rawEnd,
                                int *fixedBegin, int *fixedEnd)
{
    const int firstFlag = outerBegin + 1;      // 最小可填起始 (奇数)
    const int lastFlag = outerEnd;             // 最大可填终止 (偶数)

    // 起始往小取奇: 4 -> 3, 5 -> 5, 6 -> 5
    int b = rawBegin;
    if ((b & 1) == 0) --b;
    if (b < firstFlag) b = firstFlag;

    // 终止往大取偶: 5 -> 6, 6 -> 6, 7 -> 8
    int e = rawEnd;
    if (e & 1) ++e;
    if (e > lastFlag) e = lastFlag;

    if (fixedBegin) *fixedBegin = b;
    if (fixedEnd) *fixedEnd = e;
    return (b != rawBegin) || (e != rawEnd);
}

void BandDialog::onAccept()
{
    const int sentinelBegin = m_begin->minimum();
    const int sentinelEnd = m_end->minimum();

    // 1) 范围必须先填。新建条件时两个框都是"—", 不留默认值替用户做主。
    if (m_begin->value() == sentinelBegin || m_end->value() == sentinelEnd) {
        QMessageBox::warning(this, QString::fromUtf8("范围未填写"),
                             QString::fromUtf8("请先填写要检查的 F 数范围。"));
        (m_begin->value() == sentinelBegin ? m_begin : m_end)->setFocus();
        return;
    }

    // 2) 奇偶规整。往外取整 (起始往小、终止往大): 用户要的是"这几轮",
    //    少算一轮比多算一轮更容易得到意外结果。
    //    两个框虽然步进 2、起点奇偶固定, 但 QSpinBox 允许手打 —— 手打 5 这种
    //    奇数终止时 setRange 不会拦, 原样存进去后 endFlag = 5 只覆盖到第 2 轮,
    //    界面却写着 "3 到 5 F", 差一轮。
    const int rawBegin = m_begin->value();
    const int rawEnd = m_end->value();
    int fixedBegin = rawBegin;
    int fixedEnd = rawEnd;
    const bool adjusted = normalizeRange(m_outerBegin, m_outerEnd, rawBegin, rawEnd,
                                         &fixedBegin, &fixedEnd);

    // 3) 规整之后仍然不成立: 只报错, 不再提奇偶 —— 先告诉你"改好了"再说"这不行"
    //    是两连击, 刚被改过的数看着更像程序算错了。
    if (fixedEnd <= fixedBegin) {
        if (adjusted) {
            m_begin->setValue(fixedBegin);
            m_end->setValue(fixedEnd);
        }
        QMessageBox::warning(this, QString::fromUtf8("范围无效"),
                             QString::fromUtf8("终止 F 数必须大于起始 F 数。"));
        m_end->setFocus();
        return;
    }

    // 4) 确实改了才提示, 让用户看得见最终存进去的是哪一段。
    if (adjusted) {
        m_begin->setValue(fixedBegin);
        m_end->setValue(fixedEnd);
        QMessageBox::information(
            this, QString::fromUtf8("F 数已调整"),
            QString::fromUtf8("F 数只支持「起始取奇数、终止取偶数」，已改为 %1 到 %2 F。")
                .arg(fixedBegin).arg(fixedEnd));
    }

    // 5) 没设置任何出怪要求就等于不限制, 直接拦下 —— 这种条件留在表里
    //    只会让人以为约束生效了, 实际上什么都没过滤。
    if (m_cond.isEmpty()) {
        QMessageBox::warning(
            this, QString::fromUtf8("没有设置出怪条件"),
            m_hard ? QString::fromUtf8("这条全局条件没有设置任何出怪要求，请先设置条件。")
                   : QString::fromUtf8("这条出怪条件没有设置任何出怪要求，请先设置条件。"));
        m_editBtn->setFocus();
        return;
    }

    // 6) 条件本身能否满足 (与"开始查找"用的是同一套规则, 提前到这里提示)
    const sf::RuleBand b = band();
    const QString kind = m_hard ? QString::fromUtf8("全局条件") : QString::fromUtf8("出怪条件");
    const QString bad = sf::checkBand(b, m_scene, kind, m_siblings);
    if (!bad.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("条件无法满足"), bad);
        return;
    }

    accept();
}

void BandDialog::setSiblings(const QVector<sf::RuleBand> &siblings)
{
    m_siblings = siblings;
}
