// 复现用户的操作路径: 在界面上一条条添加 5 条"必出", 然后点开始查找。
// 直接调 MainWindow 的公开钩子, 走的是和按钮完全相同的代码路径。
#include "mainwindow.h"
#include "seedengine.h"
#include "zombiedata.h"

#include <QApplication>
#include <QTextStream>

#include <cstdio>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    MainWindow w;
    w.loadDemo();                  // 先放点东西, 确认清空逻辑也对
    w.setBandForTest(QVector<sf::RuleBand>());

    QVector<sf::RuleBand> many;
    const int picks[] = { Z_CONE, Z_DOOR, Z_POGO, Z_CATAPULT, Z_DIGGER };
    for (int z : picks) {
        sf::RuleBand one;
        one.beginFlag = 2;         // F 3 到 4
        one.endFlag = 4;
        one.cond.needMask = (1u << z);
        one.cond.sanitize();
        many << one;
    }
    w.setBandForTest(many);

    // 1) 界面上点"开始查找"走的就是这个校验
    QString err;
    const QString blocked = w.checkConditionsForTest(&err);
    std::printf("界面校验: %s\n",
                blocked.isEmpty() ? "(NOT REJECTED —— 这就是 bug)"
                                  : blocked.toUtf8().constData());

    // 2) 单条逐个装进去看会不会被拦 (确认不是哪一条自己有问题)
    for (int i = 0; i < many.size(); ++i) {
        w.setBandForTest(QVector<sf::RuleBand>() << many.at(i));
        QString one = w.checkConditionsForTest(&err);
        std::printf("  只留第 %d 条 (%s): %s\n", i + 1,
                    zombieAbbr(picks[i]).toUtf8().constData(),
                    one.isEmpty() ? "放行" : one.toUtf8().constData());
    }

    // 3) 禁出方向的同类情形: 几条各禁几只, 单条都合规, 合起来把 F 5~6
    //    禁到只剩 普僵 + 路障 (2 种), 而这一轮至少还要出 3 种。
    QVector<sf::RuleBand> bans;
    {
        static const int groups[4][5] = {
            { Z_POLE, Z_BUCKET, Z_NEWS, Z_DOOR, Z_FOOTBALL },
            { Z_DANCE, Z_SNORKEL, Z_ZOMBONI, Z_DOLPHIN, Z_JACK },
            { Z_BALLOON, Z_DIGGER, Z_POGO, Z_BUNGEE, Z_LADDER },
            { Z_CATAPULT, Z_GARG, Z_GIGA, -1, -1 },
        };
        static const int sizes[4] = { 5, 5, 5, 3 };
        for (int g = 0; g < 4; ++g) {
            sf::RuleBand one;
            one.beginFlag = 4;      // F 5 到 6
            one.endFlag = 6;
            for (int k = 0; k < sizes[g]; ++k)
                one.cond.refuseMask |= (1u << groups[g][k]);
            one.cond.sanitize();
            bans << one;
        }
    }
    w.setBandForTest(bans);
    const QString blockedRefuse = w.checkConditionsForTest(&err);
    std::printf("界面校验(合并禁出): %s\n",
                blockedRefuse.isEmpty() ? "(NOT REJECTED —— 这就是 bug)"
                                        : blockedRefuse.toUtf8().constData());
    for (int i = 0; i < bans.size(); ++i) {
        w.setBandForTest(QVector<sf::RuleBand>() << bans.at(i));
        QString one = w.checkConditionsForTest(&err);
        std::printf("  只留第 %d 条禁出: %s\n", i + 1,
                    one.isEmpty() ? "放行" : one.toUtf8().constData());
    }

    return (blocked.isEmpty() || blockedRefuse.isEmpty()) ? 1 : 0;
}
