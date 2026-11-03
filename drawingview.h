#ifndef DRAWINGVIEW_H
#define DRAWINGVIEW_H

#include <QGraphicsView>

class DrawingSettings;
class QGraphicsRectItem;

// Scene positions are CAD mm, never pixels or paper mm.
class DrawingView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit DrawingView(DrawingSettings *settings, QWidget *parent = nullptr);
    double displayZoom() const { return m_displayZoom; }
    bool setDisplayZoom(double zoom);

signals:
    void cursorPositionChanged(QPointF modelMm);
    void cursorLeft();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void updatePaper();
    void updateViewTransform();

    DrawingSettings *m_settings;
    QGraphicsRectItem *m_paper;
    QGraphicsRectItem *m_printableArea;
    double m_displayZoom = 1.0;
};

#endif
