// 跨条件审计: 专门检查 checkBand 第 7 条(多条重叠条件的"必出"合起来算名额)。
//
// 用 1.2 原版 (tests/ref12/worker.cpp) 做判据, 两个方向分开查, 判据强度不同:
//
//   误报 (说无解, 其实有解):
//     只要 1.2 的 d_page 里**观察到过一个**同时出齐 need 的位模式, 就证明确实有解
//     (r_val 到 seed 是双射, 观察到即存在)。这个方向是**可靠**的, 且很便宜。
//
//   漏报 (必真无解, 却放行):
//     "没观察到"不等于"不可能" —— 罕见组合 (概率 1e-5 量级) 采样碰不到。
//     所以只对"校验器放行、且名额已经用满"的组合, 用 1.2 的 single_seed 做
//     针对性的连续大样本复核; 样本里一个都没有才报漏报。
//
// 组合空间是 C(19,5) 量级, 全枚举跑不完, 所以按场景逐轮做有界随机抽样。
#include "ref12/worker.cpp"

#include "seedengine.h"
#include "zombiedata.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

const int kMaxLv = 10;
const int kTableSeeds = 12000;     // 建模式表的样本数 (只为误报方向服务)
const int kSamplesPerLevel = 120;      // 每个 (场景, 轮次) 抽多少个 need 组合
const int kVerifySeeds = 600000;       // 漏报方向的针对性复核样本数
const int kMaxVerify = 400;            // 全局复核次数上限, 控制总耗时

QString abbrs(quint32 bits)
{
    QString s;
    for (int z = 0; z < Z_COUNT; ++z)
        if (bits & (1u << z)) s += zombieAbbr(z);
    return s;
}

struct Shape
{
    std::vector<quint32> patterns[kMaxLv];
};

Shape buildShape(int scene)
{
    Shape s;
    d_page p(scene);
    QSet<quint32> seen[kMaxLv];
    for (int i = 0; i < kTableSeeds; ++i) {
        const quint32 rVal = quint32(0x40000000u + quint32(i));
        for (int lv = 1; lv < kMaxLv; ++lv) {
            const quint32 b = quint32(p.get(rVal + quint32(lv), lv));
            if (!seen[lv].contains(b)) {
                seen[lv].insert(b);
                s.patterns[lv].push_back(b);
            }
        }
    }
    return s;
}

bool tableHasSolution(const Shape &s, int lv, quint32 need)
{
    for (quint32 b : s.patterns[lv]) {
        if ((b & need) == need) return true;
    }
    return false;
}

int verifyWithRef12(int scene, int lv, quint32 need, int seeds)
{
    d_page p(scene);
    int hit = 0;
    for (int i = 0; i < seeds; ++i) {
        const quint32 rVal = quint32(0x40000000u + quint32(i));
        if (single_seed(rVal, p, int(need), 0, 0, 0, 0, 0, lv, lv + 1) == 1) ++hit;
    }
    return hit;
}

// 把"这一轮要出齐 need"拆成每僵尸一条单必出条件 (用户就是这么填的),
// 跑界面用的完整校验 checkBand(带 siblings)。
QString checkCombined(int scene, int lv, quint32 need)
{
    QVector<sf::RuleBand> bands;
    for (int z = 0; z < Z_COUNT; ++z) {
        if (!(need & (1u << z))) continue;
        sf::RuleBand b;
        b.beginFlag = lv * 2;
        b.endFlag = lv * 2 + 2;
        b.cond.needMask = (1u << z);
        b.cond.sanitize();
        bands.push_back(b);
    }
    for (int i = 0; i < bands.size(); ++i) {
        const QString e = sf::checkBand(bands[i], scene, QString::fromUtf8("出怪条件"), bands);
        if (!e.isEmpty()) return e;
    }
    return QString();
}

// 与 checkBand 第 7 条同口径的"名额占用": 只为判断"紧不紧", 不作判据。
int slotsNeeded(quint32 need)
{
    const quint32 fixedPair = (1u << Z_CONE) | (1u << Z_NEWS);
    const quint32 req = need & ~(1u << Z_REGULAR);
    const int others = sf::popcnt(req & ~fixedPair);
    const int pair = sf::popcnt(req & fixedPair);
    return others + std::max(0, pair - 1);
}

// 确定性伪随机, 保证每次跑的都是同一批组合。
quint32 nextRand(quint32 &state)
{
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

struct Stats
{
    int checked = 0;
    int falseReject = 0;
    int missed = 0;
    int verified = 0;
};

void checkOne(int scene, int lv, quint32 need, const Shape &shape, Stats &st, int &budget)
{
    const QString err = checkCombined(scene, lv, need);
    const bool inTable = tableHasSolution(shape, lv, need);
    ++st.checked;

    if (!err.isEmpty()) {
        if (inTable) {
            ++st.falseReject;
            std::printf("  [误报] scene=%d 第 %d 轮 (F%d~%d) 必出 %s : 1.2 观察到有解,"
                        " 校验器却报「%s」\n",
                        scene, lv, 2 * lv + 1, 2 * lv + 2,
                        abbrs(need).toUtf8().constData(), err.toUtf8().constData());
        }
        return;
    }

    // 放行了: 只有"名额已用满"的组合才值得花大样本复核
    const int slotCount = std::min(lv + 1, 9);
    if (slotsNeeded(need) < slotCount || budget <= 0) return;

    --budget;
    ++st.verified;
    const int hit = verifyWithRef12(scene, lv, need, kVerifySeeds);
    if (hit == 0) {
        ++st.missed;
        std::printf("  [漏报] scene=%d 第 %d 轮 (F%d~%d) 必出 %s : 1.2 扫 %d 个种子 0 解,"
                    " 校验器放行\n",
                    scene, lv, 2 * lv + 1, 2 * lv + 2,
                    abbrs(need).toUtf8().constData(), kVerifySeeds);
    }
}

} // namespace

// 深挖单组: 给足够大的样本数, 判断"0 解"是结构上不可能还是概率太低。
void deepVerify(int scene, int lv, quint32 need, int seeds)
{
    std::printf("深挖 scene=%d 第 %d 轮 (F%d~%d) 必出 %s:\n", scene, lv, 2 * lv + 1,
                2 * lv + 2, abbrs(need).toUtf8().constData());

    // 结构判据: 这一轮的总种类数 = 普僵 + 路障/读报之一 + 其余,
    // 上限 min(轮次+3, 11); 红眼第 6 轮起、冰车/白眼第 3 轮起。
    // need 里通常不含普僵, 所以"总种类"是 need 的位数再加普僵那一只。
    const int totalNeeded = sf::popcnt(need | (1u << Z_REGULAR));
    const int maxTotal = std::min(lv + 3, 11);
    std::printf("  结构: 需要 %d 种 (含普僵), 这一轮上限 %d 种 -> %s\n", totalNeeded, maxTotal,
                totalNeeded <= maxTotal ? "结构上可行" : "结构上不可能");

    // 稀有度估算: 用一个非常大的样本, 看命中率量级
    const int hit = verifyWithRef12(scene, lv, need, seeds);
    std::printf("  1.2 扫 %d 个种子: 命中 %d 个", seeds, hit);
    if (hit > 0)
        std::printf(" -> 确实有解 (约 1/%.0f)\n", double(seeds) / double(hit));
    else
        std::printf(" -> 样本内无解\n");

    // 校验器怎么说
    const QString err = checkCombined(scene, lv, need);
    std::printf("  校验器: %s\n",
                err.isEmpty() ? "放行" : err.toUtf8().constData());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // 用法: combinedaudit deep <scene> <level> <need-mask-hex> [seeds]
    if (argc >= 5 && QString::fromUtf8(argv[1]) == QString::fromUtf8("deep")) {
        const int scene = QString::fromUtf8(argv[2]).toInt();
        const int lv = QString::fromUtf8(argv[3]).toInt();
        const quint32 need = QString::fromUtf8(argv[4]).toUInt(nullptr, 16);
        const int seeds = (argc >= 6) ? QString::fromUtf8(argv[5]).toInt() : 2000000;
        deepVerify(scene, lv, need, seeds);
        return 0;
    }

    Stats total;
    int budget = kMaxVerify;
    quint32 rng = 12345u;

    const QVector<int> scenes = sceneIds();
    for (int si = 0; si < scenes.size(); ++si) {
        const int scene = scenes.at(si);
        const Shape shape = buildShape(scene);
        Stats st;

        std::vector<int> pool;
        for (int z : sceneZombieList(scene))
            if (z != Z_REGULAR) pool.push_back(z);

        const int n = int(pool.size());
        if (n < 2) continue;

        for (int lv = 1; lv < kMaxLv - 1; ++lv) {
            for (int s = 0; s < kSamplesPerLevel; ++s) {
                // 组合大小 2..5, 随机不重复地抽
                const int want = 2 + int(nextRand(rng) % 4u);
                std::vector<int> idx(pool.begin(), pool.end());
                quint32 need = 0;
                int picked = 0;
                for (int k = 0; k < want && k < n; ++k) {
                    const int at = int(nextRand(rng) % quint32(idx.size()));
                    need |= (1u << idx[at]);
                    idx.erase(idx.begin() + at);
                    ++picked;
                }
                if (picked < 2) continue;
                checkOne(scene, lv, need, shape, st, budget);
            }
        }

        std::printf("scene %d: 检查 %d 组 (大样本复核 %d 组), 误报 %d, 漏报 %d\n", scene,
                    st.checked, st.verified, st.falseReject, st.missed);
        total.checked += st.checked;
        total.verified += st.verified;
        total.falseReject += st.falseReject;
        total.missed += st.missed;
    }

    std::printf("\n==== 汇总: 检查 %d 组, 大样本复核 %d 组, 误报无解 %d 条, 漏报 %d 条 ====\n",
                total.checked, total.verified, total.falseReject, total.missed);
    return total.falseReject == 0 && total.missed == 0 ? 0 : 1;
}
