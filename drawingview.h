#ifndef DRAWINGVIEW_H
#define DRAWINGVIEW_H

#include <QGraphicsView>
#include <optional>

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
    enum class DistanceKind { Current, Target };
    Q_ENUM(DistanceKind)
    void startDistancePick(DistanceKind kind, QRectF pdfBounds);
    void clearDistancePick();
    bool isPickingDistance() const { return m_picking; }
    void setContentBounds(QRectF bounds);

signals:
    void cursorPositionChanged(QPointF modelMm);
    void cursorLeft();
    void distancePicked(DrawingView::DistanceKind kind, QPointF firstMm, QPointF secondMm);
    void distancePickCanceled();
    void pickHintChanged(const QString &text);

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
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
    bool m_picking = false;
    bool m_dragged = false;
    DistanceKind m_pickKind = DistanceKind::Current;
    QRectF m_pdfBounds;
    QRectF m_contentBounds;
    QPoint m_pressPosition;
    std::optional<QPointF> m_first;
    std::optional<QPointF> m_second;
    std::optional<QPointF> m_hover;
};

#endif
