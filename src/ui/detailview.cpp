#include "detailview.h"
#include "uikit.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

// 黑白配色下靠"字形 + 粗细"区分, 不只靠颜色。
// 语义: ● = 这一轮必须出某种僵尸; ✕ = 这一轮不能出; ○ = 出但没要求; 空白 = 不出。
QTableWidgetItem *markItem(const QString &glyph, bool bold, const QColor &color)
{
    QTableWidgetItem *it = new QTableWidgetItem(glyph);
    it->setTextAlignment(Qt::AlignCenter);
    if (!glyph.isEmpty()) {
        QFont f = it->font();
        f.setBold(bold);
        it->setFont(f);
        it->setForeground(color);
    }
    return it;
}

} // namespace

DetailView::DetailView(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    QHBoxLayout *bar = new QHBoxLayout;
    m_summary = new QLabel(this);
    m_summary->setObjectName("Stat");
    bar->addWidget(m_summary);
    bar->addStretch(1);

    root->addLayout(bar);

    m_table = new ui::FloatScrollTable(this);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    // 不画交替行色: 只有"这一轮没满足"的行需要灰底, 两套条纹并存会互相干扰
    ui::setupTable(m_table, false);
    // 各列都是定宽, 表格一旦被横向拉宽, 多出来的宽度就是它自己画的一片白底
    // (看起来像"右边留白", 其实是表格内部)。所以宽度钉死在"内容需要的宽度"
    // (见 applyWidthLimit), 竖向照旧跟着窗口拉伸。
    m_table->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // 不需要横向滚动: 列数为"1 个数字列 + 僵尸列", 宽度就是它们的和 (+ 竖条)
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    root->addWidget(m_table, 1);

    // 竖条出现/消失会改变"需要的宽度", 重算一次。
    // 用回调而不是继承: 表格只是本控件的成员, 不是基类。
    m_table->setBarStateCallback([this]() { applyWidthLimit(); });

    clear();
}

void DetailView::clear()
{
    m_rows.clear();
    m_cols.clear();
    m_table->clear();
    m_table->setRowCount(0);
    m_table->setColumnCount(1);
    m_table->setColumnWidth(0, 96);
    // 表头文案要和 setData()/rebuild() 保持一致, 否则"先清再填"时会闪一下旧文案
    m_table->setHorizontalHeaderLabels(QStringList()
        << QString::fromUtf8("已完成 F 数"));
    m_summary->setText(QString());
    applyWidthLimit();
}

void DetailView::setData(quint32 seed, int scene, const std::vector<sf::DetailRow> &rows,
                         const sf::RuleTable &rank, const sf::RuleTable &hard)
{
    m_rows = rows;
    m_rankTable = rank;
    m_hardTable = hard;

    // 普僵每一轮都会出现, 列出来没有信息量, 参考 1.2 的做法直接不显示。
    // 与「编辑出怪条件」表共用同一份清单 (sceneZombieListForUi)。
    m_cols = sceneZombieListForUi(scene);
    m_table->clear();
    m_table->setColumnCount(1 + m_cols.size());
    QStringList headers;
    // 参考 1.2 版: 只留一列"已完成 F 数" (轮次 = F 数 / 2 + 1, 与它线性对应)
    headers << QString::fromUtf8("已完成 F 数");
    // 僵尸列用单字简称: 20 列挤在一张表里, 双字会把表撑宽并截断数字列
    for (int i = 0; i < m_cols.size(); ++i) headers << zombieAbbr(m_cols[i]);
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 96);
    // 僵尸列定宽: 单字最宽也就 20px 上下, 定宽后整张表宽度不随场景变化
    m_table->horizontalHeader()->setMinimumSectionSize(16);
    for (int i = 1; i < headers.size(); ++i) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Fixed);
        m_table->setColumnWidth(i, 24);
    }

    rebuild();
}

void DetailView::rebuild()
{
    // 不再单独给"没有数据"留一个分支和一句提示: 查找一开始所有按钮就会变灰,
    // 那时条件不会被改掉, 点进来的必定是有数据的种子。真空数据也走下面同一条
    // 路径 —— setRowCount(0) 会把旧行清掉, 不会有上一个种子的残留。
    int ruled = 0, matched = 0, hardRuled = 0;
    for (int i = 0; i < int(m_rows.size()); ++i) {
        const sf::DetailRow &r = m_rows[size_t(i)];
        if (r.hasRule) {
            ++ruled;
            if (r.matched) ++matched;
        }
        if (r.hasHard) ++hardRuled;
    }

    m_table->setRowCount(m_rows.size());
    for (int k = 0; k < int(m_rows.size()); ++k) {
        const sf::DetailRow &r = m_rows[size_t(k)];

        // 只留一列"已完成 F 数"。原来的"轮次"列承担两个视觉信号, 现在搬到这里:
        // 这一轮有条件就加粗, 有条件但没满足就变灰 —— 一眼能看出哪些轮要检查、哪轮没过。
        QTableWidgetItem *flagItem = new QTableWidgetItem(QString::number(r.flag));
        flagItem->setTextAlignment(Qt::AlignCenter);
        if (r.hasRule || r.hasHard) {
            QFont ff = flagItem->font();
            ff.setBold(true);
            flagItem->setFont(ff);
            const bool ok = (!r.hasRule || r.matched) && (!r.hasHard || r.hardOk);
            flagItem->setForeground(ok ? QColor(0x11, 0x11, 0x11) : QColor(0x9a, 0x9a, 0x9a));
        } else {
            flagItem->setForeground(QColor(0x8a, 0x8a, 0x8a));
        }
        m_table->setItem(k, 0, flagItem);

        // 同一轮可能被多条规则带覆盖, 条件取"与"。字形显示这些条件的并集:
        // 只要有任一条要求它出现就是 ●, 否则只要有任一条要求它不出现就是 ✕。
        int rc = 0, hc = 0;
        const sf::WaveCondition *rankCs = r.hasRule ? m_rankTable.condsAt(r.level, &rc) : nullptr;
        const sf::WaveCondition *hardCs = r.hasHard ? m_hardTable.condsAt(r.level, &hc) : nullptr;

        for (int cidx = 0; cidx < m_cols.size(); ++cidx) {
            const int z = m_cols[cidx];
            const bool present = (r.bits & (1u << z)) != 0;

            // 语义化字形: ●=要求出(必出/至少出) ✕=要求不出(禁出/至多出) ○=出了但没要求
            QString glyph;
            bool want = false;      // 有任一条件要求它出现
            bool reject = false;    // 有任一条件要求它不出现
            const quint32 bit = (1u << z);
            for (int pass = 0; pass < 2 && !want; ++pass) {
                const sf::WaveCondition *src = (pass == 0) ? hardCs : rankCs;
                const int n = (pass == 0) ? hc : rc;
                for (int k = 0; k < n; ++k) {
                    if ((src[k].needMask & bit) || (src[k].atLeastMask & bit)) { want = true; break; }
                    if ((src[k].refuseMask & bit) || (src[k].atMostMask & bit)) reject = true;
                }
            }
            bool bold = false;
            if (want) {
                glyph = QString::fromUtf8("\u25cf");   // ● 要求出现
                bold = true;
            } else if (reject) {
                glyph = QString::fromUtf8("\u2715");   // ✕ 要求不出现
                bold = true;
            }

            if (!present) {
                glyph.clear();                 // 这一轮没出, 又不该出现 → 留空
            } else if (glyph.isEmpty()) {
                glyph = QString::fromUtf8("\u25cb");   // ○ 出了, 但没有任何要求
            }
            QTableWidgetItem *it = markItem(
                glyph, bold,
                present && !bold ? QColor(0x8a, 0x8a, 0x8a) : QColor(0x11, 0x11, 0x11));
            m_table->setItem(k, 1 + cidx, it);
        }

        const bool failed = (r.hasRule && !r.matched) || (r.hasHard && !r.hardOk);
        if (failed) {
            for (int cidx = 0; cidx < m_table->columnCount(); ++cidx) {
                QTableWidgetItem *it = m_table->item(k, cidx);
                if (it) it->setBackground(QColor(0xf2, 0xf2, 0xf2));
            }
        }
    }

    const int waves = int(m_rows.size());
    // 摘要只报"多少轮满足条件"; 全局条件那条不再列出 (标题行已经写明种子与场景)
    // 一轮都没有时干脆不写 —— "共 0 轮（没有设置条件）"这种话没有任何信息量。
    if (waves <= 0) {
        m_summary->setText(QString());
    } else if (ruled > 0 || hardRuled > 0) {
        m_summary->setText(QString::fromUtf8("共 %1 轮，其中 %2 轮满足条件")
                               .arg(waves)
                               .arg(matched));
    } else {
        m_summary->setText(QString::fromUtf8("共 %1 轮（没有设置条件）").arg(waves));
    }
    applyWidthLimit();
}

// 表格需要的宽度 (含竖条)。
// 窗口用这个值把自己收到刚好放得下, 否则右边会留一条白边。
// ui::preferredTableWidth() 已经算上了"竖条可见时它占的那一列", 这里不能再加
// 一次 —— 加了就多留一条 14px 的白缝。算法与 CondDialog 共用, 不各写一份。
int DetailView::preferredWidth() const
{
    return ui::preferredTableWidth(m_table);
}

// 只设 preferredWidth() 还不够: 布局会把表格拉到可用宽度, 而列是定宽的,
// 拉宽后多出来的部分就是表格自己画的白色背景 —— 看上去就是右侧留白。
// 把 max 钉死在"需要的宽度"上, 布局再想拉也没有空间可拉。
//
// 竖条那一列由 Qt 自己从控件宽度里扣 (原生滚动条就是这么工作的),
// 所以这里直接用 preferredTableWidth(), 不要再单独加一次竖条宽度。
void DetailView::applyWidthLimit()
{
    const int need = preferredWidth();
    const bool changed = (m_table->maximumWidth() != need) || (m_table->minimumWidth() != need);
    m_table->setMaximumWidth(need);
    m_table->setMinimumWidth(need);
    if (changed) emit preferredWidthChanged(need);
}
