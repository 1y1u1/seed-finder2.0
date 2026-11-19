#pragma once

#include <QDialog>

#include "seedengine.h"

class QSpinBox;
class QLabel;
class QPushButton;
class QShowEvent;

// 编辑一条出怪条件 / 全局条件: 轮次区间 + 四组出怪要求
//
// 界面按"轮"输入 (起始轮取奇数, 终止轮取偶数, 与出怪计算器一致),
// 内部 RuleBand 仍用偶数 F 数边界: beginFlag = 起始轮 - 1, endFlag = 终止轮
class BandDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BandDialog(int scene, int outerBegin, int outerEnd, QWidget *parent = nullptr);

    void setBand(const sf::RuleBand &band);
    sf::RuleBand band() const;

    // 硬性条件(全局条件)时换掉标题与按钮文案
    void setHardMode(bool hard);

    // 同一张表里的其他条件: 点"确定"时用来检查跨段矛盾 (例如 A 段禁出小丑、
    // B 段又要求必出小丑且两段重叠)。不设也不会崩, 只是跳过那一条检查。
    void setSiblings(const QVector<sf::RuleBand> &siblings);

    // 把用户填的范围规整成"起始奇数、终止偶数", 且不超出 [outerBegin+1, outerEnd]。
    // 往外取整 (起始往小、终止往大), 因为用户要的是"这几轮", 少算比多算更容易
    // 得到意外结果。返回 true 表示确实动过数值。
    // 抽成静态函数是为了能单独测: 它不弹窗、不碰控件。
    static bool normalizeRange(int outerBegin, int outerEnd,
                               int rawBegin, int rawEnd,
                               int *fixedBegin, int *fixedEnd);

private slots:
    void onEditCondition();
    void onAccept();

protected:
    // 打开窗口时把两个范围框的文本全选 (理由见 .cpp 里的 selectAllText)
    void showEvent(QShowEvent *e) override;

private:
    void buildUi();
    void refreshSummary();

    int m_scene;
    int m_outerBegin;       // 偶数 F 数边界
    int m_outerEnd;         // 偶数 F 数边界
    sf::WaveCondition m_cond;
    bool m_hard = false;
    QVector<sf::RuleBand> m_siblings;   // 同表其他条件, 供跨段矛盾检查

    QSpinBox *m_begin = nullptr;    // 起始轮 (奇数)
    QSpinBox *m_end = nullptr;      // 终止轮 (偶数)
    QLabel *m_titleRange = nullptr;
    QLabel *m_titleCond = nullptr;
    QLabel *m_summary = nullptr;
    QPushButton *m_editBtn = nullptr;
};
