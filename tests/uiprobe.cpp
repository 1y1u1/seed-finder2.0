// 界面布局探针: 在无头平台上把主窗口按几种尺寸摆一遍, 打印关键控件的几何信息。
//
// 用途:
//   1) 确认条件表的竖向滚动条有没有压住表头那一行;
//   2) 确认窗口纵向放大时两张条件卡是不是把多出来的高度平分掉;
//   3) 确认窗口横向放大时卡片宽度不变、多出的宽度只进两侧边距;
//   4) 打印"刚好放下 99 / MGE/AQE"所需的宽度, 给左栏收窄当依据。
//
// 用法: uiprobe.exe [输出文件]
//   不带参数时只打印到 stdout。

#include "mainwindow.h"
#include "style.h"
#include "banddialog.h"
#include "detaildialog.h"

#include "seedengine.h"
#include "zombiedata.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFont>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>

namespace {

QTextStream *g_out = nullptr;

void line(const QString &s)
{
    if (g_out) *g_out << s << "\n";
}

QString rectText(const QRect &r)
{
    return QString("x=%1 y=%2 w=%3 h=%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

// 条件表: 竖向滚动条是否压在表头区域上
void reportTable(const QString &name, QTableWidget *t)
{
    if (!t) {
        line(QString("  %1: <null>").arg(name));
        return;
    }
    QScrollBar *vb = t->verticalScrollBar();
    // geometry() 是相对父控件的, 统一换算到表格自身的坐标系才不会误判
    const QRect vbRect(vb->mapTo(t, QPoint(0, 0)), vb->size());
    const QRect hhRect(t->horizontalHeader()->mapTo(t, QPoint(0, 0)),
                       t->horizontalHeader()->size());
    const QRect vpRect(t->viewport()->mapTo(t, QPoint(0, 0)), t->viewport()->size());
    // 竖条是 1.2 版那种"从表头顶端画到表底"的独占一列, 所以它在纵向上与表头
    // 同行是正常的; 真正要防的是它横向压到表头/数据列上。原生滚动条会自动把
    // 表头与 viewport 的宽度各让出一列, 所以正确状态下是
    //   vbar.left >= header.right  且  vbar.left >= viewport.right
    const bool overlap = vb->isVisible()
                         && (vbRect.left() < hhRect.right() || vbRect.left() < vpRect.right());
    // 可见区高度是不是整行的整数倍: 条件表会把底部余量吃掉, 避免露半行字。
    // (viewportMargins() 是 protected, 只能从 viewport 的几何反推。)
    const int rowH = t->verticalHeader()->defaultSectionSize();
    line(QString("  %1: table=%2 viewport=%3 header=%4 vbar=%5 visible=%6 overlapHeader=%7 rows=%8 exact=%9")
             .arg(name)
             .arg(rectText(t->geometry()))
             .arg(rectText(vpRect))
             .arg(rectText(hhRect))
             .arg(rectText(vbRect))
             .arg(vb->isVisible() ? "yes" : "no")
             .arg(overlap ? "YES" : "no")
             .arg(rowH > 0 ? QString::number(vpRect.height() / rowH) : QString("?"))
             .arg(rowH > 0 && vpRect.height() % rowH == 0 ? "yes" : "no"));

    // 竖向滚动条是 Qt 原生的 (独占表格极右一整列, 从表头顶端画到表底)。
    // 这里量三件事: 它有没有压到表头 (不该, 它就在表头右边那一列里)、
    // 把手长度对不对 (pageStep 必须是"可见行数")、以及可见区是不是整行。
    {
        const int range = vb->maximum() - vb->minimum();
        const int page = vb->pageStep();
        const int visibleRows = rowH > 0 ? vpRect.height() / rowH : 0;
        const double fraction =
            (range + page) > 0 ? double(page) / double(range + page) : 0.0;
        // 原生竖条从表的顶端画到底端, 所以"压住表头"是指它盖在表头那几行的
        // 宽度范围里 —— 那是它的合法位置, 不算压。真正要防的是它盖到数据列上,
        // 这一点由 viewport 宽度里已经不含竖条来保证, 断言见下面 vbarNotInViewport。
        const bool vbarNotInViewport =
            !vb->isVisible() || (vbRect.left() >= vpRect.left() + vpRect.width());
        line(QString("    vbar: visible=%1 rect(table)=%2 range=%3..%4 value=%5 page=%6 "
                     "step=%7 handle=%8% 可见行=%9 行数=%10 不挤数据区=%11")
                 .arg(vb->isVisible() ? "yes" : "no")
                 .arg(rectText(vbRect))
                 .arg(vb->minimum())
                 .arg(vb->maximum())
                 .arg(vb->value())
                 .arg(page)
                 .arg(vb->singleStep())
                 .arg(QString::number(fraction * 100.0, 'f', 1))
                 .arg(visibleRows)
                 .arg(t->rowCount())
                 .arg(vbarNotInViewport ? "ok" : "BAD"));
    }
}

// 原生竖向滚动条的三件事:
//   1) 能滚到底、能滚回顶, 末行真的出现在末尾
//   2) 不横向压到表头/数据列 (它独占极右一整列, viewport 与表头宽度里都不含它)
//   3) 只在滚得动时出现; 不需要滚时整根隐藏, 不白占一列
//
// 早先这里测的是自绘的漂浮条 (objectName "FloatBar"), 会盖在数据区右边缘上,
// 还要额外断言"把手位置不漂"。改回原生后那三件事由 Qt 保证, 不用再测。
void checkVBar(QTableWidget *t, const QString &name)
{
    if (!t) return;
    QScrollBar *src = t->verticalScrollBar();
    if (!src) {
        line(QString("  sync[%1]: 找不到竖向滚动条").arg(name));
        return;
    }

    const QRect hh(t->horizontalHeader()->mapTo(t, QPoint(0, 0)),
                   t->horizontalHeader()->size());
    const QRect vp(t->viewport()->mapTo(t, QPoint(0, 0)), t->viewport()->size());
    const QRect g0(src->mapTo(t, QPoint(0, 0)), src->size());
    // 独占一列: 在表头与 viewport 的右侧之外
    const bool clearOfContent =
        !src->isVisible() || (g0.left() >= vp.left() + vp.width()
                              && g0.left() >= hh.x() + hh.width());
    const bool insideTable = g0.right() <= t->width();

    const bool needed = src->maximum() > 0;
    if (!needed) {
        line(QString("  sync[%1]: 不需要滚动, visible=%2 (应为 no) 不挤内容=%3 在表内=%4")
                 .arg(name)
                 .arg(src->isVisible() ? "yes" : "no")
                 .arg(clearOfContent ? "ok" : "BAD")
                 .arg(insideTable ? "ok" : "BAD"));
        return;
    }

    src->setValue(src->maximum());
    const bool okBottom = (src->value() == src->maximum());
    const QString lastRow = t->item(t->rowCount() - 1, 0)
                                ? t->item(t->rowCount() - 1, 0)->text()
                                : QString();
    src->setValue(src->minimum());
    const bool okTop = (src->value() == src->minimum());
    // 把手长度: pageStep 必须是"可见行数", 否则把手会明显偏长或偏短
    const int rowH = t->verticalHeader()->defaultSectionSize();
    const int visibleRows = rowH > 0 ? vp.height() / rowH : 0;
    const bool pageOk = (src->pageStep() == visibleRows)
                        || qAbs(src->pageStep() - visibleRows) <= 1;
    line(QString("  sync[%1]: 推到底=%2 推回顶=%3 不挤内容=%4 在表内=%5 pageStep=%6/%7%8 末行=%9")
             .arg(name)
             .arg(okBottom ? "ok" : "BAD")
             .arg(okTop ? "ok" : "BAD")
             .arg(clearOfContent ? "ok" : "BAD")
             .arg(insideTable ? "ok" : "BAD")
             .arg(src->pageStep())
             .arg(visibleRows)
             .arg(pageOk ? " ok" : " BAD")
             .arg(lastRow));
}

void reportWindow(MainWindow *w, const QString &tag)
{
    line(QString("--- %1: window %2x%3 ---").arg(tag).arg(w->width()).arg(w->height()));

    const QList<QTableWidget *> tables = w->findChildren<QTableWidget *>();
    for (int i = 0; i < tables.size(); ++i) {
        QTableWidget *t = tables.at(i);
        QString name = t->objectName();
        if (name.isEmpty()) {
            // 用列头文字当名字
            name = t->columnCount() > 0 && t->horizontalHeaderItem(0)
                       ? t->horizontalHeaderItem(0)->text()
                       : QString("table%1").arg(i);
        }
        reportTable(name, t);
    }

    const QList<QScrollArea *> areas = w->findChildren<QScrollArea *>();
    for (int i = 0; i < areas.size(); ++i) {
        line(QString("  scrollArea%1: %2 hbarVisible=%3")
                 .arg(i)
                 .arg(rectText(areas.at(i)->geometry()))
                 .arg(areas.at(i)->horizontalScrollBar()->isVisible() ? "yes" : "no"));
    }

    // 卡片: 用 Card 这个 objectName 找
    const QList<QWidget *> cards = w->findChildren<QWidget *>();
    int cardIdx = 0;
    for (int i = 0; i < cards.size(); ++i) {
        QWidget *c = cards.at(i);
        if (c->objectName() != QLatin1String("Card")) continue;
        line(QString("  card#%1: %2 minHint=%3x%4 hint=%5x%6")
                 .arg(cardIdx++)
                 .arg(rectText(c->geometry()))
                 .arg(c->minimumSizeHint().width())
                 .arg(c->minimumSizeHint().height())
                 .arg(c->sizeHint().width())
                 .arg(c->sizeHint().height()));
    }
}

// 左栏收窄的依据: 各种控件"刚好放下"需要的宽度
void reportWidths(MainWindow *w)
{
    line("--- 控件宽度 ---");
    const QList<QComboBox *> combos = w->findChildren<QComboBox *>();
    for (int i = 0; i < combos.size(); ++i) {
        QComboBox *c = combos.at(i);
        line(QString("  combo#%1: current=%2 sizeHint=%3 minimumSizeHint=%4 geometry=%5")
                 .arg(i)
                 .arg(c->currentText())
                 .arg(c->sizeHint().width())
                 .arg(c->minimumSizeHint().width())
                 .arg(rectText(c->geometry())));
    }
    const QList<QSpinBox *> spins = w->findChildren<QSpinBox *>();
    for (int i = 0; i < spins.size(); ++i) {
        QSpinBox *s = spins.at(i);
        line(QString("  spin#%1: value=%2 sizeHint=%3 minimumSizeHint=%4 geometry=%5")
                 .arg(i)
                 .arg(s->value())
                 .arg(s->sizeHint().width())
                 .arg(s->minimumSizeHint().width())
                 .arg(rectText(s->geometry())));
    }
    // 种子输入框: 用户明确提过这一格不能挤, 单独量出来
    const QList<QLineEdit *> edits = w->findChildren<QLineEdit *>();
    for (int i = 0; i < edits.size(); ++i) {
        QLineEdit *e = edits.at(i);
        line(QString("  lineEdit#%1: text=\"%2\" sizeHint=%3 geometry=%4")
                 .arg(i)
                 .arg(e->text())
                 .arg(e->sizeHint().width())
                 .arg(rectText(e->geometry())));
    }
    // 结果卡标题行的三个控件: 标题 / 输入框 / 按钮, 看右栏够不够宽
    const QList<QPushButton *> btns = w->findChildren<QPushButton *>();
    for (int i = 0; i < btns.size(); ++i) {
        QPushButton *b = btns.at(i);
        line(QString("  button#%1: text=\"%2\" sizeHint=%3 geometry=%4")
                 .arg(i)
                 .arg(b->text())
                 .arg(b->sizeHint().width())
                 .arg(rectText(b->geometry())));
    }
    const QList<QLabel *> labels = w->findChildren<QLabel *>();
    int labelHint = 0;
    for (int i = 0; i < labels.size(); ++i) {
        if (labels.at(i)->text().isEmpty()) continue;
        labelHint = qMax(labelHint, labels.at(i)->sizeHint().width());
    }
    line(QString("  最宽 QLabel sizeHint = %1").arg(labelHint));
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 与 main.cpp 保持一致: 同样的字体和样式表, 否则量出来的宽度没意义
    QFont f(QString::fromUtf8("Microsoft YaHei UI"), 9);
    app.setFont(f);
    app.setStyleSheet(appStyleSheet());

    QFile file;
    QTextStream stream(stdout);
    if (argc > 1) {
        file.setFileName(QString::fromLocal8Bit(argv[1]));
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            stream.setDevice(&file);
        }
    }
    // 控制台/文件统一 UTF-8: 默认按系统代码页写, 中文读出来是乱码
    stream.setCodec("UTF-8");
    g_out = &stream;

    MainWindow w;
    w.loadDemo();
    w.show();

    const QSize sizes[] = { QSize(672, 464), QSize(900, 700), QSize(1400, 1000) };
    for (const QSize &s : sizes) {
        w.resize(s);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        reportWindow(&w, QString("%1x%2").arg(s.width()).arg(s.height()));
    }

    reportWidths(&w);

    // 长条件必须能横向滚: 塞一条 20 种僵尸的"禁出", 然后检查条件列宽 > 视口宽,
    // 且横向滚动条的 range 能滚。这条以前是"一行放下 20 个僵尸"靠加宽左栏实现的,
    // 现在靠滚动 —— 必须确认滚动真的能用, 而不是被压成省略号。
    line("--- 长条件横向滚动 ---");
    {
        sf::RuleBand longBand;
        longBand.beginFlag = 20;
        longBand.endFlag = 22;
        longBand.cond.refuseMask = 0;
        for (int z = 0; z < Z_COUNT; ++z) {
            if (z != Z_CONE && z != Z_NEWS) longBand.cond.refuseMask |= (1u << z);
        }
        longBand.cond.sanitize();
        QVector<sf::RuleBand> many;
        many << longBand;
        // 通过公开钩子把这条塞进界面: onAddBand 需要对话框, 这里直接调测试入口
        w.setBandForTest(many);
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        const QList<QTableWidget *> t2 = w.findChildren<QTableWidget *>();
        for (int i = 0; i < t2.size(); ++i) {
            QTableWidget *t = t2.at(i);
            if (t->rowCount() == 0 || t->columnCount() < 2) continue;
            const int condW = t->columnWidth(1);
            const int viewW = t->viewport()->width();
            QScrollBar *hb = t->horizontalScrollBar();
            line(QString("  %1: condCol=%2 viewport=%3 hbarRange=%4..%5 横向可滚=%6")
                     .arg(t->horizontalHeaderItem(0)->text())
                     .arg(condW)
                     .arg(viewW)
                     .arg(hb->minimum())
                     .arg(hb->maximum())
                     .arg(hb->maximum() > 0 ? "yes" : "no"));
        }
    }

    // 用户报的两件事, 各留一条判定:
    //   1) "拉动窗口高度时条件表的行高会变化" —— 行高必须恒为 ui::kTableRowHeight。
    //      曾经有个 fitRowHeight() 在 20~22 之间微调, 想让可见区正好放下整数行;
    //      可 minimumSectionSize 默认是 28 时那些微调根本落不下去, 结果只是让行高
    //      随着窗口高度变来变去, 而"不露半行"一次都没达成。
    //   2) "条件卡下面的横向滚动条长度不对" —— Qt 默认横向按列滚, 两列的表 range
    //      只有 0..1, 把手永远显示 50%, 跟内容宽出多少毫无关系。改成 ScrollPerPixel
    //      后 range 才是像素, 把手长度应该 ≈ 视口宽 / 内容宽。
    line("--- 行高恒定 / 横向把手比例 ---");
    {
        int refRowH = -1;
        bool stable = true;
        QString heightNote;
        const int hs[] = { 500, 560, 620, 700, 780 };
        for (int hi : hs) {
            w.resize(QSize(672, hi));
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            const QList<QTableWidget *> ts = w.findChildren<QTableWidget *>();
            for (QTableWidget *t : ts) {
                if (t->rowCount() == 0 || t->columnCount() < 2) continue;
                if (!t->horizontalHeaderItem(0)
                    || t->horizontalHeaderItem(0)->text() != QString::fromUtf8("范围")) {
                    continue;
                }
                const int rh = t->verticalHeader()->defaultSectionSize();
                if (refRowH < 0) {
                    refRowH = rh;
                } else if (rh != refRowH && stable) {
                    stable = false;
                    heightNote = QString::fromUtf8("高度 %1 时行高从 %2 变成了 %3")
                                     .arg(hi).arg(refRowH).arg(rh);
                }
            }
        }
        line(QString::fromUtf8("  条件表行高: 基准 %1px, 高度 500/560/620/700/780 下一致 = %2  %3")
                 .arg(refRowH).arg(stable ? "yes" : "no").arg(heightNote));
    }
    {
        // 上面那段塞的正是"一条禁出 18 种"的长条件, 条件列宽出视口, 横向滚动条可见。
        const QList<QTableWidget *> ts = w.findChildren<QTableWidget *>();
        for (QTableWidget *t : ts) {
            if (t->rowCount() == 0 || t->columnCount() < 2) continue;
            if (!t->horizontalHeaderItem(0)
                || t->horizontalHeaderItem(0)->text() != QString::fromUtf8("范围")) {
                continue;
            }
            QScrollBar *hb = t->horizontalScrollBar();
            if (hb->maximum() <= 0 || !hb->isVisible()) continue;
            int content = 0;
            for (int c = 0; c < t->columnCount(); ++c) content += t->columnWidth(c);
            const int vw = t->viewport()->width();
            const double range = hb->maximum() - hb->minimum();
            const double page = hb->pageStep();
            const double handle = (range + page) > 0 ? page / (range + page) : 0.0;
            const double expect = content > 0 ? double(vw) / double(content) : 0.0;
            const double diff = handle - expect;
            line(QString::fromUtf8("  横向把手: 内容 %1px / 视口 %2px range=0..%3 page=%4 "
                                   "把手 %5% 期望 %6%  %7")
                     .arg(content).arg(vw).arg(hb->maximum()).arg(hb->pageStep())
                     .arg(QString::number(handle * 100.0, 'f', 1))
                     .arg(QString::number(expect * 100.0, 'f', 1))
                     .arg(diff < 0.05 && diff > -0.05 ? "OK" : "比例不对"));
        }
    }

    // 用户报"两张条件卡显示不全 / 滚动条呢"时顺带抓到的一个真 bug:
    // 竖条刚出现的那一刻, viewport 从 296 窄成 282, Qt 在 updateGeometries() 里
    // 按**旧**列宽(122+174=296)判定"放不下"并把横条显示出来; 紧接着我们把条件列
    // 收窄到 160(总长 282, 其实正好放下), 但改列宽不会触发 updateGeometries(),
    // Qt 不会再算一遍 —— 横条就白占着一行(range=0..0 却 visible=yes)。
    // 这里守两条不变量:
    //   1) 横条可见 ⇒ 内容真的比视口宽;
    //   2) 行全部放得下 ⇒ 竖条不出现("按需显示", 用户已确认要这个行为)。
    line("--- 滚动条按需显示 (横条不白占一行) ---");
    {
        QVector<sf::RuleBand> many;
        for (int i = 0; i < 20; ++i) {
            sf::RuleBand b;
            b.beginFlag = (i + 1) * 2;
            b.endFlag = (i + 1) * 2 + 2;
            b.cond.refuseMask = (1u << Z_CONE) | (1u << Z_NEWS);
            b.cond.sanitize();
            many << b;
        }
        w.setBandForTest(many);
        const int hs[] = { 464, 718, 900 };
        for (int hi : hs) {
            w.resize(QSize(672, hi));
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            const QList<QTableWidget *> ts = w.findChildren<QTableWidget *>();
            for (QTableWidget *t : ts) {
                if (t->rowCount() == 0 || t->columnCount() < 2) continue;
                if (!t->horizontalHeaderItem(0)
                    || t->horizontalHeaderItem(0)->text() != QString::fromUtf8("范围")) {
                    continue;
                }
                int content = 0;
                for (int c = 0; c < t->columnCount(); ++c) content += t->columnWidth(c);
                const int vw = t->viewport()->width();
                QScrollBar *hb = t->horizontalScrollBar();
                QScrollBar *vb = t->verticalScrollBar();
                const bool hbarBad = hb->isVisible() && content <= vw;
                // 竖条: 内容高 = 行数 * 行高, 视口高 = viewport 高(没竖条时)
                const int needH = t->rowCount() * t->verticalHeader()->defaultSectionSize();
                const int haveH = t->viewport()->height();
                const bool vbarBad = !vb->isVisible() && needH > haveH;
                line(QString::fromUtf8("  h=%1 %2(rows=%3) 内容宽=%4 视口宽=%5 行高合计=%6 "
                                       "视口高=%7 hbar=%8 vbar=%9  %10")
                         .arg(hi)
                         .arg(t->horizontalHeaderItem(0)->text())
                         .arg(t->rowCount())
                         .arg(content)
                         .arg(vw)
                         .arg(needH)
                         .arg(haveH)
                         .arg(hb->isVisible() ? "yes" : "no")
                         .arg(vb->isVisible() ? "yes" : "no")
                         .arg((hbarBad || vbarBad) ? "不对" : "OK"));
            }
        }
        w.loadDemo();
    }

    // 滚动条同步: demo 数据是 4 条出怪条件 + 1 条全局条件, 出怪表超过 3 行
    // 应该能滚, 全局表只有 1 行应该不显示竖条。
    // 先缩回最小尺寸并恢复 demo 数据 —— 上面那段塞的是单条长条件, 只有 1 行,
    // 根本滚不动, 量不出同步对不对。
    w.loadDemo();
    w.resize(QSize(672, 464));
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    line("--- 竖向滚动条 (672x464) ---");
    const QList<QTableWidget *> tables = w.findChildren<QTableWidget *>();
    for (int i = 0; i < tables.size(); ++i) {
        QTableWidget *t = tables.at(i);
        const QString name = t->columnCount() > 0 && t->horizontalHeaderItem(0)
                                 ? QString("%1(%2行)")
                                       .arg(t->horizontalHeaderItem(0)->text())
                                       .arg(t->rowCount())
                                 : QString("table%1").arg(i);
        if (t->rowCount() == 0) continue;
        checkVBar(t, name);
    }

    // 结果表: 用和左侧条件卡同一套原生竖条。它平时是空的 (没跑过查找),
    // 这里直接塞 40 行表格数据, 才量得出"滚得动"的那种状态。
    line("--- 结果表竖条 ---");
    {
        QTableWidget *result = nullptr;
        const QList<QTableWidget *> all = w.findChildren<QTableWidget *>();
        for (QTableWidget *t : all) {
            if (t->columnCount() == 2 && t->horizontalHeaderItem(0)
                && t->horizontalHeaderItem(0)->text() == QString::fromUtf8("种子")) {
                result = t;
                break;
            }
        }
        if (!result) {
            line("  (找不到结果表)");
        } else {
            result->setRowCount(40);
            for (int r = 0; r < 40; ++r) {
                QTableWidgetItem *a = new QTableWidgetItem(QString::number(0x1000 + r, 16).toUpper());
                QTableWidgetItem *b = new QTableWidgetItem(QString("3 / 3"));
                a->setTextAlignment(Qt::AlignCenter);
                b->setTextAlignment(Qt::AlignCenter);
                result->setItem(r, 0, a);
                result->setItem(r, 1, b);
            }
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
            reportTable(QString::fromUtf8("种子(40行)"), result);
            checkVBar(result, QString::fromUtf8("种子(40行)"));
            line(QString("  列宽: 种子=%1 满足轮数=%2 viewport=%3")
                     .arg(result->columnWidth(0))
                     .arg(result->columnWidth(1))
                     .arg(result->viewport()->width()));
            result->setRowCount(0);
        }
    }

    // 查找中的状态行必须能一行放下 —— 一换行卡片高度就跳, 进度条和按钮跟着抖。
    // 用最长的真实文案量: 三位数百分比 + "约剩 N 分 N 秒" + "已找到 x 个满足"。
    // 命中数取上限量级 (42.9 亿) 与中量级各一档, 覆盖 "亿" 与 "万" 两种缩写。
    line("--- 状态行宽度 (不换行才算过) ---");
    w.resize(QSize(672, 464));
    QCoreApplication::processEvents();
    {
        const struct { int pct; quint64 qualified; bool paused; const char *what; } cases[] = {
            {   9, 12345678ULL,    false, "查找中 9% (千万级命中)" },
            {  99, 4294967296ULL,  false, "查找中 99% (上限量级)" },
            {   9, 12345678ULL,    true,  "已暂停 9%" },
            {   9, 37ULL,          false, "查找中 9% (两位数命中)" },
        };
        for (const auto &c : cases) {
            w.showRunStateForTest(c.pct, c.qualified, c.paused);
            QCoreApplication::processEvents();
            QLabel *stat = nullptr;
            const QList<QLabel *> labels = w.findChildren<QLabel *>();
            for (QLabel *l : labels) {
                if (l->objectName() == QLatin1String("Stat")) { stat = l; break; }
            }
            if (!stat) { line("  (找不到状态标签)"); break; }
            const QString text = stat->text();
            const QFontMetrics fm(stat->font());
            const int need = fm.width(text);
            const int have = stat->width();
            // 文案本身也要核: 括号里的"已检查种子数"已经去掉了, 改成"已找到 x 个满足"。
            // 这里多守一道, 免得以后有人把种子数加回来又没人发现。
            const bool hasFound = text.contains(QString::fromUtf8("已找到"))
                                  && text.contains(QString::fromUtf8("个满足"));
            const bool noCount = !text.contains(QString::fromUtf8("亿）"))
                                 && !text.contains(QString::fromUtf8("万）"));
            line(QString("  %1: 需要 %2px / 可用 %3px  wordWrap=%4  %5  %6")
                     .arg(QString::fromUtf8(c.what))
                     .arg(need)
                     .arg(have)
                     .arg(stat->wordWrap() ? "on" : "off")
                     .arg(need <= have && !stat->wordWrap() ? "OK" : "OVERFLOW")
                     .arg(hasFound && noCount ? "文案OK" : "文案不符"));
            line(QString("      文案: %1").arg(text));
        }
        w.showRunStateForTest(0, 0, false);
    }

    // 范围框必须放得下上限 1000000: 宽度原来是写死的 96px, 七位数会被裁掉最后一位,
    // 用户填对了也像填错了 —— 这类问题只有真的把值设成最大值才量得出来。
    line("--- 编辑条件对话框的范围框宽度 ---");
    {
        BandDialog dlg(2, 2, 1000000);
        dlg.show();
        QCoreApplication::processEvents();
        const QList<QSpinBox *> boxes = dlg.findChildren<QSpinBox *>();
        if (boxes.isEmpty()) line("  (找不到范围框)");
        for (QSpinBox *b : boxes) {
            b->setValue(b->maximum());
            QCoreApplication::processEvents();
            const QString text = b->text();
            const QFontMetrics fm(b->font());
            const int need = fm.width(text) + 22 + 12;   // 上下箭头 + 左右内边距
            line(QString("  range: range=%1..%2 value=%3 text=\"%4\" 文字需要 %5px / 框宽 %6px  %7")
                     .arg(b->minimum()).arg(b->maximum()).arg(b->value()).arg(text)
                     .arg(need).arg(b->width())
                     .arg(need <= b->width() ? "OK" : "裁切"));
        }
    }

    // 奇偶规整: 手打 5 这种奇数终止时 setRange 不会拦, 原样存进去会让
    // endFlag=5 只覆盖到第 2 轮, 而界面写着 "3 到 5 F" —— 差一轮。
    // 这里把各种输入组合都过一遍, 确认取整方向是"往外"。
    line("--- 范围奇偶规整 (起始取奇、终止取偶, 一律往外) ---");
    {
        struct Case { int rb; int re; int eb; int ee; bool moved; };
        const Case cases[] = {
            { 3, 4, 3, 4, false },   // 已经合规
            { 3, 5, 3, 6, true },    // 奇数终止 -> 往上取偶
            { 3, 6, 3, 6, false },
            { 4, 6, 3, 6, true },    // 偶数起始 -> 往下取奇
            { 4, 5, 3, 6, true },    // 两个都不合规
            { 5, 7, 5, 8, true },
            { 1, 2, 3, 2, true },    // 起始低于下界 -> 夹到 3
            { 3, 1000001, 3, 1000000, true },   // 终止超上限 -> 夹到 1000000
        };
        int bad = 0;
        for (const Case &c : cases) {
            int fb = 0, fe = 0;
            const bool moved = BandDialog::normalizeRange(2, 1000000, c.rb, c.re, &fb, &fe);
            const bool ok = (fb == c.eb) && (fe == c.ee) && (moved == c.moved);
            if (!ok) ++bad;
            line(QString("  %1 到 %2 -> %3 到 %4 (moved=%5)  %6")
                     .arg(c.rb).arg(c.re).arg(fb).arg(fe)
                     .arg(moved ? "yes" : "no").arg(ok ? "OK" : "不符合预期"));
        }
        line(QString("  规整用例: %1 条, 不符合预期 %2 条").arg(int(sizeof(cases) / sizeof(cases[0]))).arg(bad));
    }

    // 打开对话框时焦点落在范围框上, 光标位置决定"用户敲的数字插在哪"。
    // 实测过光标停在两个 1 之间 (截图里显示成 1|1): 用户想把 11 改成 13,
    // 敲下 "3" 会得到 "131" 而不是 "13" —— 一打开就踩坑。要求聚焦的框
    // 要么全选 (敲字直接替换), 要么光标在末尾 (敲字往后追加)。
    line("--- 打开对话框时范围框的焦点与光标 ---");
    {
        BandDialog dlg(2, 2, 1000000);
        sf::RuleBand b;
        b.beginFlag = 10;
        b.endFlag = 22;
        dlg.setBand(b);
        dlg.show();
        QCoreApplication::processEvents();
        QLineEdit *le = dlg.findChild<QLineEdit *>();
        if (!le) {
            line("  (找不到范围框的输入行)");
        } else {
            const QString sel = le->selectedText();
            const int pos = le->cursorPosition();
            const bool ok = !sel.isEmpty() || pos == le->text().size();
            line(QString("  text=\"%1\" cursor=%2 selected=\"%3\"  %4")
                     .arg(le->text()).arg(pos).arg(sel)
                     .arg(ok ? "OK" : "光标停在中间, 敲数字会插进旧值里"));
        }
    }

    // 详情窗口: 表格各列定宽, 窗口必须收到"刚好放得下", 右边不能留白。
    // 留白的成因是表格被布局横向拉宽后自己画白底 —— 表格宽度 > 列宽和 就说明有白。
    line("--- 出怪详情窗口的右侧留白 ---");
    {
        sf::RuleBand b1;
        b1.beginFlag = 10;
        b1.endFlag = 12;
        b1.cond.needMask = (1u << Z_GIGA);
        b1.cond.sanitize();
        QVector<sf::RuleBand> demoBands;
        demoBands << b1;
        sf::RuleTable rankTable;
        rankTable.build(demoBands, 1, 26);
        sf::RuleTable hardTable;
        hardTable.build(QVector<sf::RuleBand>(), 1, 26);

        const std::vector<sf::DetailRow> rows =
            sf::viewDetail(0x1234ABCDu, 1, 13, 2, 1, 26, rankTable, hardTable);

        DetailDialog dd;
        dd.setData(0x1234ABCDu, 2, rows, rankTable, hardTable);
        dd.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTableWidget *t = dd.findChild<QTableWidget *>();
        if (!t) {
            line("  (找不到详情表)");
        } else {
            int colsW = 0;
            for (int i = 0; i < t->columnCount(); ++i) colsW += t->columnWidth(i);
            const int content = colsW + t->frameWidth() * 2;
            const int extra = t->width() - content;
            // 原生竖条独占极右一整列: 表格宽度 = 列宽和 + 边框 + 一条竖条,
            // 竖条可见时多出来的正好是它; 不需要滚动时它整根隐藏, 多出应是 0。
            QScrollBar *bar = t->verticalScrollBar();
            const bool barShown = bar && bar->isVisible();
            const int barW = barShown ? bar->width() : 0;
            const int want = content + barW;
            const bool widthOk = (t->width() == want)
                                 || (qAbs(t->width() - want) <= 2);
            line(QString("  表宽 %1px / 列宽和 %2px / 竖条 %3 / 期望 %4px  %5")
                     .arg(t->width()).arg(content)
                     .arg(barShown ? QString::number(barW) + "px" : QString("隐藏"))
                     .arg(want)
                     .arg(widthOk ? "OK" : (extra > 0 ? "表格被拉宽了, 右侧会有一片白"
                                                      : "表格比内容还窄")));

            // 竖条不能盖住最后一列的字形: 详情表的僵尸列只有 24px。
            // 原生滚动条是独占一列, 所以竖条的左缘应当 >= viewport 右缘。
            if (!barShown) {
                line("  竖条没有出现, 无需避让  OK");
            } else {
                const QRect vp = t->viewport()->geometry();
                const QRect bg(bar->mapTo(t, QPoint(0, 0)), bar->size());
                const bool clear = (bg.left() >= vp.left() + vp.width());
                line(QString("  数据区右缘 %1px / 竖条 %2  %3")
                         .arg(vp.left() + vp.width())
                         .arg(rectText(bg))
                         .arg(clear ? "OK (独占一列, 没有压住)" : "竖条压住了最后一列"));
            }

            // 窗口宽度上下限都该等于"刚好放得下表格":
            // 只设下限用户还能拖宽 (右边一片白), 只 resize 不设下限用户能拖窄
            // (右边几列直接被裁掉, 横向滚动条是关的, 裁掉就再也看不到)。
            {
                const int minW = dd.minimumWidth();
                const int maxW = dd.maximumWidth();
                const bool pinned = (minW == maxW) && (minW >= t->width());
                line(QString("  窗口宽 %1px / 下限 %2 / 上限 %3  %4")
                         .arg(dd.width()).arg(minW).arg(maxW)
                         .arg(pinned ? "OK (横向不可拖)"
                                     : (minW != maxW ? "上下限不等, 还能拖出白边或裁掉列"
                                                     : "下限比表格窄, 会裁列")));
                // 真去拖一下: 缩到 100px 宽也不该窄于下限
                dd.resize(100, dd.height());
                QCoreApplication::processEvents();
                const bool noShrink = dd.width() >= minW;
                line(QString("  强行拖到 100px 后实际 %1px  %2")
                         .arg(dd.width())
                         .arg(noShrink ? "OK (缩不窄)" : "FAIL (被拖窄了)"));
                dd.resize(minW, dd.height());
                QCoreApplication::processEvents();
            }
        }
    }

    // 详情表的字形跟随"这一轮实际出没出":
    //   ● 要求出现且出了   ✕ 要求不出现但出了   ○ 出了但没要求   空白 没出
    // 没出的格子一律留空 —— 不管条件怎么要求它。这里用两条互补的条件验证:
    // 全局条件"必出 投篮"、出怪条件"禁出 白眼" 落在同一轮里。
    line("--- 详情表字形 (● / ✕ / ○ / 空白) ---");
    {
        const int scene = 2;
        sf::RuleBand hardBand;
        hardBand.beginFlag = 10;
        hardBand.endFlag = 12;                       // 只覆盖 level 5
        hardBand.cond.needMask = (1u << Z_CATAPULT); // 全局条件: 必出 投篮
        hardBand.cond.sanitize();

        sf::RuleBand rankBand;
        rankBand.beginFlag = 10;
        rankBand.endFlag = 12;
        rankBand.cond.refuseMask = (1u << Z_GARG);   // 出怪条件: 禁出 白眼
        rankBand.cond.sanitize();

        QVector<sf::RuleBand> rankBands;
        rankBands << rankBand;
        QVector<sf::RuleBand> hardBands;
        hardBands << hardBand;

        sf::RuleTable rankTable;
        rankTable.build(rankBands, 1, 26);
        sf::RuleTable hardTable;
        hardTable.build(hardBands, 1, 26);

        const std::vector<sf::DetailRow> rows =
            sf::viewDetail(0x1234ABCDu, 1, 13, scene, 1, 26, rankTable, hardTable);

        DetailDialog dd;
        dd.setData(0x1234ABCDu, scene, rows, rankTable, hardTable);
        dd.show();
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();

        QTableWidget *t = dd.findChild<QTableWidget *>();
        const QVector<int> cols = sceneZombieListForUi(scene);
        int row5 = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].level == 5) row5 = int(i);

        if (!t || row5 < 0) {
            line(QString("  (构造失败 t=%1 row5=%2)").arg(t ? 1 : 0).arg(row5));
        } else {
            const QString dot = QString::fromUtf8("\u25cf");
            const QString cross = QString::fromUtf8("\u2715");
            const QString ring = QString::fromUtf8("\u25cb");
            const quint32 bits = rows[size_t(row5)].bits;

            // 1) 逐格核对: 没出的格子必须留空, 出了的格子必须有字形。
            int badBlank = 0;   // 没出却有字形
            int badGlyph = 0;   // 出了却没字形
            for (int c = 0; c < cols.size(); ++c) {
                const QTableWidgetItem *it = t->item(row5, 1 + c);
                const QString g = it ? it->text() : QString();
                const bool present = (bits & (1u << cols[c])) != 0;
                if (!present && !g.isEmpty()) ++badBlank;
                if (present && g.isEmpty()) ++badGlyph;
            }
            line(QString("  没出的格子留空: %1 处不符合").arg(badBlank));
            line(QString("  出了的格子有字形: %1 处不符合").arg(badGlyph));

            // 2) 单点核对: 「必出」且出了 → ●;「禁出」且没出 → 空白。
            const int cCat = cols.indexOf(Z_CATAPULT);
            const int cGarg = cols.indexOf(Z_GARG);
            const bool catPresent = (bits & (1u << Z_CATAPULT)) != 0;
            const bool gargPresent = (bits & (1u << Z_GARG)) != 0;
            const QString gotCat = (cCat >= 0 && t->item(row5, 1 + cCat))
                                       ? t->item(row5, 1 + cCat)->text() : QString("<无此列>");
            const QString gotGarg = (cGarg >= 0 && t->item(row5, 1 + cGarg))
                                        ? t->item(row5, 1 + cGarg)->text() : QString("<无此列>");
            // 白眼这一轮没出 → 即使条件禁出它, 也该是空白, 而不是 ✕。
            const bool okGarg = gargPresent ? (gotGarg == cross) : gotGarg.isEmpty();
            line(QString("  全局条件「必出 投篮」→ 投篮格「%1」(这一轮%2出) %3")
                     .arg(gotCat, catPresent ? "有" : "没", catPresent ? "OK" : "应为 ●"));
            line(QString("  出怪条件「禁出 白眼」→ 白眼格「%1」(这一轮%2出) %3")
                     .arg(gotGarg, gargPresent ? "有" : "没",
                          okGarg ? "OK (没出就留空)"
                                 : (gargPresent ? "应为 ✕" : "没出却画了字形")));
        }
    }

    // 完成态的文案是最容易被改坏又没人测的地方: 它只在跑完一次查找之后才出现,
    // 而全量查找要跑很久。抽成静态纯函数之后可以直接核对措辞与边界。
    // 重点: 不再出现"4,294,967,296 种子"这种固定的种子总数, 只保留比例。
    line("--- 完成态文案 (状态行 / 无满足弹窗) ---");
    {
        struct Case {
            int best; int active; double sec; bool gpu; bool stopped; bool early;
            const char *expect;
        };
        const Case cases[] = {
            { 29, 29, 9.48, true,  false, false, "GPU · 最好 29/29，9.48 秒" },
            { 20, 29, 9.48, true,  false, false, "GPU · 最好 20/29，9.48 秒" },
            { 20, 29, 9.48, false, false, false, "最好 20/29，9.48 秒" },
            { 20, 29, 9.48, false, true,  false, "最好 20/29，9.48 秒，已中断" },
            { 29, 29, 1.00, false, false, true,  "最好 29/29，1.00 秒，早停" },
            { -1, 29, 2.50, false, false, false, "无结果，2.50 秒" },
        };
        int bad = 0;
        for (const Case &c : cases) {
            const QString got = MainWindow::statusLine(c.best, c.active, c.sec,
                                                       c.gpu, c.stopped, c.early);
            const QString want = QString::fromUtf8(c.expect);
            const bool ok = (got == want);
            if (!ok) ++bad;
            line(QString("  %1  %2").arg(ok ? "OK  " : "FAIL").arg(got));
            if (!ok) line(QString("        期望: %1").arg(want));
            // 文案里不许再出现种子总数: 那是整个区间的固定值, 既长又没信息量
            if (got.contains(QLatin1Char(',')) || got.contains(QString::fromUtf8("种子"))) {
                ++bad;
                line("        文案里仍有种子数量");
            }
        }
        // 无完全满足的弹窗正文
        const QString msg = MainWindow::noFullMatchMessage(20, 29);
        const QString msgWant = QString::fromUtf8("已检查全部种子，最好的一档为 20 / 29 轮。");
        const bool msgOk = (msg == msgWant);
        if (!msgOk) ++bad;
        line(QString("  %1 弹窗: %2").arg(msgOk ? "OK  " : "FAIL").arg(msg));
        if (!msgOk) line(QString("        期望: %1").arg(msgWant));
        line(QString("  文案用例: %1 条, 不符合预期 %2 条")
                 .arg(int(sizeof(cases) / sizeof(cases[0])) + 1).arg(bad));
    }

    // 查找期间所有"会改到查找参数或条件"的控件都该变灰, 只有三个运行按钮按状态
    // 各自处理 (开始查找/暂停在跑的时候本来就该灰, 停止必须能用)。这条规则散在
    // setRunning 里靠 findChildren 扫, 很容易被后加的控件漏掉 —— 这里逐个点名核。
    line("--- 查找期间控件禁用 ---");
    {
        MainWindow w;
        w.loadDemo();
        w.show();
        QCoreApplication::processEvents();

        // 三个运行按钮的文案会随状态变 ("开始查找" 跑起来变 "查找中…"),
        // 所以按文案认人不可靠 —— 用指针认。主界面里其它同类控件都要跟着灰。
        QList<QWidget *> watched;
        const QList<QWidget *> all = w.findChildren<QWidget *>();
        for (QWidget *c : all) {
            if (!c || c->window() != &w) continue;
            if (qobject_cast<QPushButton *>(c) || qobject_cast<QSpinBox *>(c)
                || qobject_cast<QComboBox *>(c) || qobject_cast<QCheckBox *>(c)
                || qobject_cast<QLineEdit *>(c)) {
                watched << c;
            }
        }

        // 按文案挑出三个运行按钮, 只在"查找中"这一轮里跳过它们分别判定
        QPushButton *startBtn = nullptr;
        QPushButton *pauseBtn = nullptr;
        QPushButton *stopBtn = nullptr;
        for (QWidget *c : watched) {
            QPushButton *b = qobject_cast<QPushButton *>(c);
            if (!b) continue;
            const QString s = b->text();
            if (s == QString::fromUtf8("开始查找") || s == QString::fromUtf8("查找中…")) startBtn = b;
            else if (s == QString::fromUtf8("暂停") || s == QString::fromUtf8("继续")) pauseBtn = b;
            else if (s == QString::fromUtf8("停止")) stopBtn = b;
        }

        w.setRunningForTest(true);
        QCoreApplication::processEvents();

        int stillOn = 0;                 // 该灰却还亮着的
        QStringList onNames;
        for (QWidget *c : watched) {
            if (c == startBtn || c == pauseBtn || c == stopBtn) continue;
            QCheckBox *cb = qobject_cast<QCheckBox *>(c);
            if (cb && cb->text() == QString::fromUtf8("用 GPU 加速")) continue;  // 另算
            if (c->isEnabled()) {
                ++stillOn;
                if (onNames.size() < 6) {
                    QPushButton *b = qobject_cast<QPushButton *>(c);
                    onNames << (b ? b->text() : QString(c->metaObject()->className()));
                }
            }
        }
        line(QString("  参数与条件控件都已灰: 仍可用的 %1 个 (%2)  %3")
                 .arg(stillOn)
                 .arg(onNames.isEmpty() ? QString::fromUtf8("无") : onNames.join(QStringLiteral(", ")))
                 .arg(stillOn == 0 ? "OK" : "有控件没灰"));

        line(QString("  「开始查找」已灰: %1")
                 .arg(!startBtn ? QString("(找不到开始按钮)")
                                : (startBtn->isEnabled() ? "FAIL (还亮着)" : "OK")));
        line(QString("  「暂停」可用: %1")
                 .arg(!pauseBtn ? QString("(找不到暂停按钮)")
                                : (pauseBtn->isEnabled() ? "OK" : "FAIL (被灰掉了)")));
        line(QString("  「停止」可用: %1")
                 .arg(!stopBtn ? QString("(找不到停止按钮)")
                               : (stopBtn->isEnabled() ? "OK" : "FAIL (被灰掉了)")));

        // 退出查找状态后必须全部恢复 (GPU 那个跟随后端可用性, 单独算)
        w.setRunningForTest(false);
        QCoreApplication::processEvents();
        int backOff = 0;
        QStringList offNames;
        for (QWidget *c : watched) {
            if (c == pauseBtn || c == stopBtn) continue;    // 闲着时本就该灰
            QCheckBox *cb = qobject_cast<QCheckBox *>(c);
            if (cb && cb->text() == QString::fromUtf8("用 GPU 加速")) continue;
            if (!c->isEnabled()) {
                ++backOff;
                if (offNames.size() < 6) {
                    QPushButton *b = qobject_cast<QPushButton *>(c);
                    offNames << (b ? b->text() : QString(c->metaObject()->className()));
                }
            }
        }
        line(QString("  查找结束后恢复: 仍灰着 %1 个 (%2)  %3")
                 .arg(backOff)
                 .arg(offNames.isEmpty() ? QString::fromUtf8("无") : offNames.join(QStringLiteral(", ")))
                 .arg(backOff == 0 ? "OK" : "有控件没恢复"));
    }

    g_out = nullptr;
    if (file.isOpen()) file.close();
    return 0;
}
