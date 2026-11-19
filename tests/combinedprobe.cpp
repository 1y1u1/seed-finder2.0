// 验证"同一轮被多条条件覆盖时, 合起来的名额是否会超" —— 用 1.2 原版实现判定。
//
// 背景: 用户报的一组条件是这样填的 (5 条, 都是 3 到 4 F):
//     必出 障 / 必出 门 / 必出 跳 / 必出 篮 / 必出 矿
// 每条单独看都合规 (1 种必出), 但 F3~4 (level 1) 一共只有 2~4 个僵尸,
// 扣掉必然出现的 普僵 + 路障/读报 之后只剩 2 个格位 —— 5 个"非固定"僵尸不可能同时出。
//
// 判定只用 ref12 的 single_seed (把 5 条的 needMask 并成一条传进去),
// 不在这里另写一套枚举逻辑。
#include "ref12/worker.cpp"

#include "seedengine.h"
#include "zombiedata.h"

#include <QCoreApplication>

#include <cstdio>

namespace {

QString abbrs(quint32 bits)
{
    QString s;
    for (int z = 0; z < Z_COUNT; ++z)
        if (bits & (1u << z)) s += zombieAbbr(z);
    return s;
}

// 把几条条件的 needMask 并成一条, 数真实解。返回命中数; total 置为样本数。
quint64 countMerged(int scene, quint32 need, int lvBegin, int lvEnd, int samples)
{
    d_page p(scene);
    quint64 hit = 0;
    for (int i = 0; i < samples; ++i) {
        const quint32 rVal = quint32(0x40000000u + quint32(i));
        if (single_seed(rVal, p, int(need), 0, 0, 0, 0, 0, lvBegin, lvEnd)
            == lvEnd - lvBegin) {
            ++hit;
        }
    }
    return hit;
}

void report(const char *title, quint32 need, int lvBegin, int lvEnd, int samples)
{
    std::printf("%s\n", title);
    std::printf("  合并后的必出 = %s (共 %d 种), level %d..%d\n",
                abbrs(need).toUtf8().constData(), __popcnt(unsigned(need)),
                lvBegin, lvEnd);
    for (int scene : sceneIds()) {
        const quint64 hit = countMerged(scene, need, lvBegin, lvEnd, samples);
        std::printf("  scene %d: %d 个种子里有 %llu 个满足%s\n", scene, samples,
                    static_cast<unsigned long long>(hit), hit == 0 ? "  <-- 无解" : "");
    }
    std::printf("\n");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const int samples = (argc > 1) ? std::max(1, QString::fromUtf8(argv[1]).toInt()) : 20000;

    // 用户报的那一组: 5 条都要求 3~4 F (level 1)
    const quint32 userCase = (1u << Z_CONE) | (1u << Z_DOOR) | (1u << Z_POGO)
                             | (1u << Z_CATAPULT) | (1u << Z_DIGGER);
    report("== 用户报的那组: 3~4 F 必出 障/门/跳/篮/矿 (5 种) ==", userCase, 1, 2, samples);

    // 对照 1: 只留 2 种非固定 (应该可行, level 1 有 2 个新增格位)
    report("== 对照: 3~4 F 必出 障/门 (2 种, 应当有解) ==",
           (1u << Z_CONE) | (1u << Z_DOOR), 1, 2, samples);

    // 对照 2: 3 种非固定 (超出 2 个格位, 应当无解)
    report("== 对照: 3~4 F 必出 障/门/跳 (3 种, 应当无解) ==",
           (1u << Z_CONE) | (1u << Z_DOOR) | (1u << Z_POGO), 1, 2, samples);

    // 对照 2b: 3 种, 但其中一种跟固定的重复 (路障), 实际只要 2 个额外格位 → 应当有解
    report("== 对照: 3~4 F 必出 障/门/跳 但含固定僵尸路障 (2 种非固定, 应当有解) ==",
           (1u << Z_CONE) | (1u << Z_DOOR) | (1u << Z_NEWS), 1, 2, samples);

    // 对照 3: 同样 5 种, 但放到 11~12 F (level 5, 格位够)
    report("== 对照: 11~12 F 必出 障/门/跳/篮/矿 (5 种, level 5 应当有解) ==",
           userCase, 5, 6, samples);

    // 用户第二次报的那一组: 4 条都是 5~6 F (level 2), 分别必出 杆/车/偷/梯
    //   level 2 总种类数上限 min(2+3, 11) = 5, 扣掉必有 普僵 + 路障/读报 只剩 3 种,
    //   这 4 种一个都不是 路障/读报, 所以放不下 —— 应当无解。
    const quint32 userCase2 = (1u << Z_POLE) | (1u << Z_ZOMBONI) | (1u << Z_BUNGEE)
                              | (1u << Z_LADDER);
    report("== 用户第二次报的那组: 5~6 F 必出 杆/车/偷/梯 (4 种, level 2) ==",
           userCase2, 2, 3, samples);

    // 对照 4: 同样 4 种, 放到 7~8 F (level 3, 只剩 4 种非固定名额) —— 应当有解
    report("== 对照: 7~8 F 必出 杆/车/偷/梯 (4 种, level 3 应当有解) ==",
           userCase2, 3, 4, samples);

    // 对照 5: 3 种放到 level 2 (正好 3 个非固定名额) —— 应当有解
    report("== 对照: 5~6 F 必出 杆/车/偷 (3 种, level 2 应当有解) ==",
           (1u << Z_POLE) | (1u << Z_ZOMBONI) | (1u << Z_BUNGEE), 2, 3, samples);

    // 对照 6: 4 种但含一个固定僵尸 (读报), 实际只要 3 个非固定名额 —— 应当有解
    report("== 对照: 5~6 F 必出 杆/车/偷/报 (含固定僵尸, level 2 应当有解) ==",
           (1u << Z_POLE) | (1u << Z_ZOMBONI) | (1u << Z_BUNGEE) | (1u << Z_NEWS),
           2, 3, samples);

    return 0;
}
