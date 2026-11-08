#ifndef TOOLPALETTE_H
#define TOOLPALETTE_H

#include "drawingtool.h"
#include <QWidget>
#include <QList>

class QAction;
class QActionGroup;
class QGridLayout;
class QScrollArea;
class QToolButton;
class QLabel;

class ToolPalette : public QWidget
{
    Q_OBJECT
public:
    explicit ToolPalette(QWidget *parent = nullptr);
    DrawingTool currentTool() const { return m_currentTool; }
    bool setCurrentTool(DrawingTool tool);
    QAction *toolAction(DrawingTool tool) const;
    static QString toolName(DrawingTool tool);
    static QString modeText(DrawingTool tool);
    void setToolHint(const QString &text);

signals:
    void toolChanged(DrawingTool tool);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void scheduleLayout();
    void reflow();
    QScrollArea *m_scroll;
    QWidget *m_content;
    QGridLayout *m_grid = nullptr;
    QActionGroup *m_group;
    QList<QToolButton*> m_buttons;
    DrawingTool m_currentTool = DrawingTool::Selection;
    int m_columns = 0;
    bool m_layoutPending = false;
    QLabel *m_hint;
};

#endif
