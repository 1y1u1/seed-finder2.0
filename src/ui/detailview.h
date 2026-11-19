#pragma once

#include <QWidget>

#include "seedengine.h"

class QTableWidget;
class QLabel;

namespace ui {
class FloatScrollTable;
}

// 单种子的逐轮详情表 (放在独立的"出怪详情"对话框里)
class DetailView : public QWidget
{
    Q_OBJECT

public:
    explicit DetailView(QWidget *parent = nullptr);

    void clear();
    void setData(quint32 seed, int scene, const std::vector<sf::DetailRow> &rows,
                 const sf::RuleTable &rank, const sf::RuleTable &hard);

    // 表格需要的宽度 (各列定宽之和 + 边框 + 竖条可见时它独占的那一列)。
    // 窗口用它把自己收到刚好放得下, 不留右侧那片空白。算法与 CondDialog
    // 共用 uikit 里的同一份实现。
    int preferredWidth() const;

signals:
    // 需要的宽度变了 (通常是竖条出现/消失, 或者是换了个列数不同的场景)。
    // 窗口跟着收放, 否则多出来的宽度又会变成表格右侧的白底。
    void preferredWidthChanged(int width);

private:
    void rebuild();

    // 表格各列都是定宽, 一旦被横向拉宽, 多出来的宽度会变成表格自己画的白色区域,
    // 看上去就是"右边一片留白"。这里把表格的宽度钉在"列宽和"上, 让它永远不可能
    // 比内容更宽; 窗口这边再按 preferredWidth() 收到刚好放得下。
    //
    // 竖条那一列由 Qt 自己从控件宽度里扣 (它独占极右一整列, 不压在列上),
    // 所以这里不用再额外加宽度。
    void applyWidthLimit();

    ui::FloatScrollTable *m_table = nullptr;
    QLabel *m_summary = nullptr;

    std::vector<sf::DetailRow> m_rows;
    sf::RuleTable m_rankTable;
    sf::RuleTable m_hardTable;
    QVector<int> m_cols;
};
