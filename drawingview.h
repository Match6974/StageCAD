#ifndef DRAWINGVIEW_H
#define DRAWINGVIEW_H

#include <QGraphicsView>
#include <optional>
#include "drawingtool.h"
#include <QPolygonF>
#include <QHash>
#include <QUuid>

class DrawingSettings;
class DrawingDocument;
class QMenu;
class QGraphicsRectItem;

// Scene positions are CAD mm, never pixels or paper mm.
class DrawingView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit DrawingView(DrawingSettings *settings, QWidget *parent = nullptr);
    double displayZoom() const { return m_displayZoom; }
    bool setDisplayZoom(double zoom);
    void resetView();
    DrawingTool activeTool() const { return m_activeTool; }
    bool setActiveTool(DrawingTool tool);
    bool submitDrawingPoint(QPointF pointMm, bool constrainLine = false);
    bool finishPolygon();
    void cancelDrawing();
    void setDrawingEnabled(bool enabled);
    bool drawingEnabled() const { return m_drawingEnabled; }
    int draftPointCount() const { return m_draftPoints.size(); }
    bool canFinishPolygon() const;
    QString drawingHint() const;
    const QList<QGraphicsItem*> &shapeItems() const { return m_shapes; }
    QList<QGraphicsItem*> selectedShapeItems() const;
    void clearShapeSelection();
    bool deleteSelectedShapes();
    DrawingDocument *document() const { return m_document; }
    QUuid shapeId(const QGraphicsItem *item) const;
    static constexpr int ShapeIdRole = 0x5344;
    static constexpr int ShapeToolRole = 0x5343;
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
    void activeToolChanged(DrawingTool tool);
    void drawingStateChanged();
    void drawingHintChanged(const QString &text);
    void shapeCreated(DrawingTool tool);
    void shapeSelectionChanged();

protected:
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    void updatePaper();
    void updateViewTransform(QPointF centerMm);
    void setViewCenter(QPointF centerMm);
    void updateInteractionCursor();
    void updateSceneBounds();
    void notifyDrawingState();
    bool isDrawingTool() const;
    bool commitShape(QPointF secondMm);
    bool nearDraftVertex(QPointF pointMm, QPointF vertexMm) const;

    QGraphicsItem *shapeAt(QPoint viewportPoint) const;
    void notifySelection();
    void endShapeMove(bool commit);
    void syncShape(const QUuid &id);
    void removeShapeItem(const QUuid &id);

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
    DrawingTool m_activeTool = DrawingTool::Selection;
    bool m_panning = false;
    bool m_updatingView = false;
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPoint m_lastPanPosition;
    QPointF m_viewCenter;
    bool m_drawingEnabled = true;
    bool m_drawPressed = false;
    bool m_ignoreDrawingRelease = false;
    bool m_drawShift = false;
    QPolygonF m_draftPoints;
    std::optional<QPointF> m_draftHover;
    DrawingDocument *m_document;
    QHash<QUuid, QGraphicsItem*> m_shapeGraphics;
    QList<QGraphicsItem*> m_shapes; // Display order, not the authoritative geometry.
    QPointF m_moveDelta;
    QMenu *m_selectionMenu;
    bool m_selectionPressed = false;
    bool m_movingShapes = false;
    QPoint m_selectionPressPixel;
    QPointF m_selectionPressMm;
    QList<QPair<QGraphicsItem*, QPointF>> m_moveOrigins;
};

#endif
