#include "drawingview.h"
#include "drawingsettings.h"

#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>

namespace {
bool validPoint(QPointF point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y())
        && std::abs(point.x()) <= 10000000 && std::abs(point.y()) <= 10000000;
}

bool validPolygon(const QPolygonF &points)
{
    if (points.size() < 3) return false;
    const QPointF first = points[0];
    for (qsizetype i = 1; i + 1 < points.size(); ++i) {
        const QPointF a = points[i] - first, b = points[i + 1] - first;
        const long double lhs = static_cast<long double>(a.x()) * b.y();
        const long double rhs = static_cast<long double>(a.y()) * b.x();
        const long double tolerance = 64 * std::numeric_limits<double>::epsilon()
            * (std::abs(lhs) + std::abs(rhs));
        if (std::abs(lhs - rhs) > tolerance) return true;
    }
    return false;
}

QPointF constrainedLinePoint(QPointF first, QPointF point)
{
    constexpr double pi = 3.14159265358979323846;
    const QPointF delta = point - first;
    if (delta.isNull()) return point;
    const double angle = std::atan2(delta.y(), delta.x());
    const std::array<int, 16> angles = {0, 30, 45, 60, 90, 120, 135, 150,
                                      180, 210, 225, 240, 270, 300, 315, 330};
    double nearest = 0, difference = std::numeric_limits<double>::max();
    for (const int degrees : angles) {
        const double candidate = degrees * pi / 180;
        const double distance = std::abs(std::remainder(angle - candidate, 2 * pi));
        if (distance < difference) { nearest = candidate; difference = distance; }
    }
    const QPointF direction(std::cos(nearest), std::sin(nearest));
    const double projected = delta.x() * direction.x() + delta.y() * direction.y();
    return first + direction * projected;
}

QPen shapePen()
{
    QPen pen(QColor("#182b3d"), 1.5); pen.setCosmetic(true); return pen;
}

QBrush shapeBrush() { return QColor(95, 155, 215, 65); }
}

DrawingView::DrawingView(DrawingSettings *settings, QWidget *parent)
    : QGraphicsView(parent), m_settings(settings)
{
    setObjectName("drawingView");
    setScene(new QGraphicsScene(this));
    setBackgroundBrush(QColor("#51565e"));
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    auto rememberScroll = [this] {
        if (!m_updatingView) m_viewCenter = mapToScene(viewport()->rect().center());
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, rememberScroll);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, rememberScroll);
    setToolTip(tr("CAD座標は実寸mmです。原点は用紙中心、Xは右向き、Yは上向きです。"));

    QPen border(QColor("#252a30"));
    border.setCosmetic(true);
    m_paper = scene()->addRect({}, border, Qt::white);
    m_paper->setZValue(-1000);
    QPen printable(QColor("#888888"), 1, Qt::DashLine);
    printable.setCosmetic(true);
    m_printableArea = scene()->addRect({}, printable, Qt::NoBrush);
    m_printableArea->setZValue(-999);

    connect(settings, &DrawingSettings::scaleChanged, this, &DrawingView::updatePaper);
    updatePaper();
}

bool DrawingView::setDisplayZoom(double zoom)
{
    if (!std::isfinite(zoom) || zoom < 0.1 || zoom > 10.0) return false;
    const QPointF center = m_viewCenter;
    m_displayZoom = zoom;
    updateViewTransform(center);
    return true;
}

void DrawingView::resetView()
{
    m_displayZoom = 1;
    updateViewTransform({});
}

bool DrawingView::setActiveTool(DrawingTool tool)
{
    if (m_picking || (tool != DrawingTool::Selection && tool != DrawingTool::Pan
        && tool != DrawingTool::Line && tool != DrawingTool::Rectangle
        && tool != DrawingTool::Circle && tool != DrawingTool::Polygon)) return false;
    if (m_activeTool != tool) {
        cancelDrawing();
        m_panning = false;
        m_activeTool = tool;
        updateInteractionCursor();
        emit activeToolChanged(tool);
        notifyDrawingState();
    }
    return true;
}

bool DrawingView::isDrawingTool() const
{
    return m_activeTool == DrawingTool::Line || m_activeTool == DrawingTool::Rectangle
        || m_activeTool == DrawingTool::Circle || m_activeTool == DrawingTool::Polygon;
}

QString DrawingView::drawingHint() const
{
    if (!m_drawingEnabled) return tr("作業領域で描画ツールを選択してください。");
    if (m_activeTool == DrawingTool::Selection) return tr("選択操作は未対応です。");
    if (m_activeTool == DrawingTool::Pan) return tr("左ドラッグで画面を移動します。");
    if (m_activeTool == DrawingTool::Polygon)
        return m_draftPoints.isEmpty() ? tr("最初の頂点を指定してください。")
            : tr("頂点を追加します。始点か終点のダブルクリックで完了。Delで1点戻り、Escで中止します。");
    if (m_draftPoints.isEmpty())
        return m_activeTool == DrawingTool::Circle ? tr("円の中心を指定してください。") : tr("始点を指定してください。");
    if (m_activeTool == DrawingTool::Circle) return tr("円周上の点を指定してください。Escで中止します。");
    if (m_activeTool == DrawingTool::Rectangle) return tr("対角の頂点を指定してください。Escで中止します。");
    return tr("終点を指定してください。Shiftで角度を固定、Escで中止します。");
}

void DrawingView::notifyDrawingState()
{
    viewport()->update();
    emit drawingStateChanged();
    emit drawingHintChanged(drawingHint());
}

void DrawingView::cancelDrawing()
{
    m_draftPoints.clear(); m_draftHover.reset(); m_drawPressed = false; m_drawShift = false;
    notifyDrawingState();
}

void DrawingView::setDrawingEnabled(bool enabled)
{
    if (m_drawingEnabled == enabled) return;
    m_drawingEnabled = enabled;
    if (!enabled) cancelDrawing();
    else notifyDrawingState();
}

bool DrawingView::nearDraftVertex(QPointF point, QPointF vertex) const
{
    return QLineF(viewportTransform().map(point), viewportTransform().map(vertex)).length() <= 6;
}

bool DrawingView::submitDrawingPoint(QPointF point, bool constrainLine)
{
    if (!m_drawingEnabled || m_picking || m_panning || !isDrawingTool()) return false;
    if (!validPoint(point)) {
        emit drawingHintChanged(tr("座標は±10000000mm以内の有限の数値で指定してください。"));
        return false;
    }
    if (m_activeTool == DrawingTool::Line && constrainLine && !m_draftPoints.isEmpty())
        point = constrainedLinePoint(m_draftPoints.first(), point);
    if (!validPoint(point)) return false;
    if (m_draftPoints.isEmpty()) {
        m_draftPoints.append(point); m_draftHover = point;
        notifyDrawingState(); return true;
    }
    if (m_activeTool == DrawingTool::Polygon) {
        if (point == m_draftPoints.last() || point == m_draftPoints.first()) {
            emit drawingHintChanged(tr("多角形は始点か終点のダブルクリック、または「多角形を完了」で閉じます。"));
            return false;
        }
        m_draftPoints.append(point); m_draftHover = point;
        notifyDrawingState(); return true;
    }
    return commitShape(point);
}

bool DrawingView::commitShape(QPointF second)
{
    const QPointF first = m_draftPoints.first();
    QGraphicsItem *item = nullptr;
    const QPen pen = shapePen();
    const QBrush brush = shapeBrush();
    if (m_activeTool == DrawingTool::Line && QLineF(first, second).length() > 0)
        item = scene()->addLine(QLineF(first, second), pen);
    else if (m_activeTool == DrawingTool::Rectangle) {
        const QRectF rectangle = QRectF(first, second).normalized();
        if (rectangle.width() > 0 && rectangle.height() > 0)
            item = scene()->addRect(rectangle, pen, brush);
    } else if (m_activeTool == DrawingTool::Circle) {
        const double radius = QLineF(first, second).length();
        const QRectF circle(first.x() - radius, first.y() - radius, 2 * radius, 2 * radius);
        if (radius > 0 && validPoint(circle.topLeft()) && validPoint(circle.bottomRight()))
            item = scene()->addEllipse(circle, pen, brush);
    }
    if (!item) {
        emit drawingHintChanged(tr("大きさが0の図形や、範囲外の図形は作成できません。終点を指定し直してください。"));
        return false;
    }
    item->setData(ShapeToolRole, QVariant::fromValue(m_activeTool));
    m_shapes.append(item);
    updateSceneBounds(); setViewCenter(m_viewCenter);
    const DrawingTool created = m_activeTool;
    cancelDrawing(); emit shapeCreated(created);
    return true;
}

bool DrawingView::canFinishPolygon() const
{
    return m_drawingEnabled && !m_picking && !m_panning && m_activeTool == DrawingTool::Polygon && validPolygon(m_draftPoints);
}

bool DrawingView::finishPolygon()
{
    if (!canFinishPolygon()) {
        emit drawingHintChanged(tr("多角形には同一直線上にない3点以上が必要です。"));
        return false;
    }
    auto *item = scene()->addPolygon(m_draftPoints, shapePen(), shapeBrush());
    item->setData(ShapeToolRole, QVariant::fromValue(DrawingTool::Polygon));
    m_shapes.append(item);
    updateSceneBounds(); setViewCenter(m_viewCenter);
    cancelDrawing(); emit shapeCreated(DrawingTool::Polygon);
    return true;
}

void DrawingView::updateInteractionCursor()
{
    if (m_panning) viewport()->setCursor(Qt::ClosedHandCursor);
    else if (m_picking) viewport()->setCursor(Qt::CrossCursor);
    else if (m_activeTool == DrawingTool::Pan) viewport()->setCursor(Qt::OpenHandCursor);
    else if (m_activeTool == DrawingTool::Selection) viewport()->setCursor(Qt::ArrowCursor);
    else viewport()->setCursor(Qt::CrossCursor);
}

void DrawingView::updatePaper()
{
    // Only the paper's real-world coverage changes; model items are untouched.
    m_paper->setRect(m_settings->paperRectInModel());
    m_printableArea->setRect(m_settings->printableRectInModel());
    updateSceneBounds();
    updateViewTransform({});
    emit cursorLeft();
}

void DrawingView::setContentBounds(QRectF bounds)
{
    m_contentBounds = bounds;
    updateSceneBounds();
    setViewCenter(m_viewCenter);
}

void DrawingView::updateSceneBounds()
{
    QScopedValueRollback<bool> updating(m_updatingView, true);
    const double margin = 10 * m_settings->scaleDenominator();
    const QRectF paperBounds = m_paper->rect().adjusted(-margin, -margin, margin, margin);
    QRectF bounds = m_contentBounds.isEmpty() ? paperBounds : paperBounds.united(m_contentBounds);
    for (auto *shape : m_shapes) bounds = bounds.united(shape->sceneBoundingRect());
    scene()->setSceneRect(bounds);
}

void DrawingView::updateViewTransform(QPointF center)
{
    if (m_updatingView) return;
    QScopedValueRollback<bool> updating(m_updatingView, true);
    const QSizeF paper = m_settings->paperSizeMm();
    // Scroll bars can appear during zoom. Their appearance must not redefine
    // the base fit scale, otherwise zoom and resize recursively change it.
    const QSize available = maximumViewportSize();
    // The minimum also keeps Qt's inverse transform numerically usable at the
    // largest supported denominator and smallest display zoom.
    const double pixelsPerPaperMm = std::max(0.2, std::min(
        available.width() / (paper.width() + 20),
        available.height() / (paper.height() + 20)));
    const double pixelsPerModelMm = pixelsPerPaperMm * m_displayZoom
        / m_settings->scaleDenominator();
    // Invert only the view's Y axis. Geometry in the scene remains in real mm.
    setTransform(QTransform::fromScale(pixelsPerModelMm, -pixelsPerModelMm));
    setViewCenter(center);
}

void DrawingView::setViewCenter(QPointF center)
{
    QScopedValueRollback<bool> updating(m_updatingView, true);
    m_viewCenter = center;
    const QSizeF visible(std::max(1, viewport()->width()) / std::abs(transform().m11()),
                         std::max(1, viewport()->height()) / std::abs(transform().m22()));
    // Navigation margins are view-only. Panning and cursor-centered zoom must
    // also work in the gray area, without changing model items or paper bounds.
    const QRectF navigation(center.x() - visible.width() * 1.5,
                            center.y() - visible.height() * 1.5,
                            visible.width() * 3, visible.height() * 3);
    setSceneRect(scene()->sceneRect().united(navigation));
    centerOn(center);
}

void DrawingView::wheelEvent(QWheelEvent *event)
{
    const double delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
    if (delta == 0) { event->ignore(); return; }
    const QPoint position = event->position().toPoint();
    const QPointF fixedPoint = mapToScene(position);
    const double zoom = std::clamp(m_displayZoom * std::pow(1.25, delta / 120.0), 0.1, 10.0);
    setDisplayZoom(zoom);
    setViewCenter(m_viewCenter + fixedPoint - mapToScene(position));
    // Qt scroll bars use integer device positions. Correct the final device
    // offset directly rather than accumulating rounded scene-center errors.
    const QPointF error = viewportTransform().map(fixedPoint) - event->position();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(error.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(error.y()));
    viewport()->update();
    event->accept();
}

void DrawingView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPanPosition;
        setViewCenter(mapToScene(viewport()->rect().center() - delta));
        m_lastPanPosition = event->pos();
        emit cursorPositionChanged(mapToScene(event->pos()));
        event->accept(); return;
    }
    const QPointF point = mapToScene(event->pos());
    emit cursorPositionChanged(point);
    if (m_picking) {
        if (event->buttons().testFlag(Qt::LeftButton)
            && (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
            m_dragged = true;
        if (m_pickKind == DistanceKind::Target || m_pdfBounds.contains(point)) m_hover = point;
        else m_hover.reset();
        viewport()->update();
    } else if (m_drawingEnabled && isDrawingTool()) {
        if (m_drawPressed && (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
            m_dragged = true;
        m_draftHover = validPoint(point) ? std::optional<QPointF>(point) : std::nullopt;
        m_drawShift = event->modifiers().testFlag(Qt::ShiftModifier);
        viewport()->update();
    }
    QGraphicsView::mouseMoveEvent(event);
}

void DrawingView::startDistancePick(DistanceKind kind, QRectF pdfBounds)
{
    cancelDrawing();
    clearDistancePick();
    m_pickKind = kind;
    m_pdfBounds = pdfBounds;
    m_picking = true;
    updateInteractionCursor();
    setFocus();
}

void DrawingView::clearDistancePick()
{
    m_picking = false;
    m_panning = false;
    m_first.reset(); m_second.reset(); m_hover.reset();
    updateInteractionCursor();
    viewport()->update();
}

void DrawingView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_activeTool == DrawingTool::Pan && !m_picking)) {
        m_drawPressed = false;
        m_panning = true; m_panButton = event->button(); m_lastPanPosition = event->pos();
        setFocus(); updateInteractionCursor(); event->accept(); return;
    }
    if (m_picking) {
        m_pressPosition = event->pos(); m_dragged = false;
        event->accept(); return;
    }
    if (m_drawingEnabled && isDrawingTool() && event->button() == Qt::LeftButton) {
        m_ignoreDrawingRelease = false;
        m_pressPosition = event->pos(); m_dragged = false; m_drawPressed = true;
        setFocus(); event->accept(); return;
    }
    QGraphicsView::mousePressEvent(event);
}

void DrawingView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        if (event->button() == m_panButton) { m_panning = false; updateInteractionCursor(); }
        event->accept(); return;
    }
    if (!m_picking) {
        if (event->button() == Qt::LeftButton && m_ignoreDrawingRelease) {
            m_ignoreDrawingRelease = false; m_drawPressed = false; event->accept(); return;
        }
        if (m_drawingEnabled && isDrawingTool() && event->button() == Qt::LeftButton) {
            const bool pressed = m_drawPressed; m_drawPressed = false;
            event->accept();
            if (!pressed || m_dragged) return;
            const QPointF point = mapToScene(event->pos());
            if (m_activeTool == DrawingTool::Polygon && !m_draftPoints.isEmpty()
                && (nearDraftVertex(point, m_draftPoints.last()) || nearDraftVertex(point, m_draftPoints.first()))) {
                emit drawingHintChanged(tr("始点か終点をダブルクリックすると多角形を完了します。"));
                return;
            }
            submitDrawingPoint(point, event->modifiers().testFlag(Qt::ShiftModifier));
            return;
        }
        QGraphicsView::mouseReleaseEvent(event); return;
    }
    event->accept();
    if (event->button() != Qt::LeftButton || m_dragged) return;
    const QPointF point = mapToScene(event->pos());
    if (m_pickKind == DistanceKind::Current && !m_pdfBounds.contains(point)) {
        emit pickHintChanged(tr("現在の距離はPDFの内側の2点を指定してください。Escで戻ります。"));
        return;
    }
    if (!m_first) {
        m_first = point;
        emit pickHintChanged(tr("終点をクリックしてください。Escで測定を中止します。"));
    } else {
        const double distance = QLineF(*m_first, point).length();
        if (!std::isfinite(distance) || distance <= 0) {
            emit pickHintChanged(tr("始点とは異なる点を指定してください。"));
            return;
        }
        m_second = point;
        m_picking = false;
        updateInteractionCursor();
        emit distancePicked(m_pickKind, *m_first, *m_second);
    }
    viewport()->update();
}

void DrawingView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_picking && m_drawingEnabled && isDrawingTool() && event->button() == Qt::LeftButton) {
        m_drawPressed = false; m_ignoreDrawingRelease = true;
        if (m_activeTool == DrawingTool::Polygon && !m_draftPoints.isEmpty()) {
            const QPointF point = mapToScene(event->pos());
            if (nearDraftVertex(point, m_draftPoints.first()) || nearDraftVertex(point, m_draftPoints.last()))
                finishPolygon();
        }
        event->accept(); return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void DrawingView::keyPressEvent(QKeyEvent *event)
{
    if (m_picking && event->key() == Qt::Key_Escape) {
        clearDistancePick(); emit distancePickCanceled(); event->accept(); return;
    }
    if (m_panning && event->key() == Qt::Key_Escape) {
        m_panning = false; updateInteractionCursor(); event->accept(); return;
    }
    if (m_drawingEnabled && !m_draftPoints.isEmpty()) {
        if (event->key() == Qt::Key_Escape) {
            cancelDrawing(); event->accept(); return;
        }
        if (event->key() == Qt::Key_Delete && m_activeTool == DrawingTool::Polygon) {
            m_draftPoints.removeLast(); notifyDrawingState(); event->accept(); return;
        }
        if (event->key() == Qt::Key_Shift) { m_drawShift = true; viewport()->update(); }
    }
    QGraphicsView::keyPressEvent(event);
}

void DrawingView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift) { m_drawShift = false; viewport()->update(); }
    QGraphicsView::keyReleaseEvent(event);
}

void DrawingView::leaveEvent(QEvent *event)
{
    emit cursorLeft();
    m_hover.reset();
    m_draftHover.reset();
    viewport()->update();
    QGraphicsView::leaveEvent(event);
}

void DrawingView::resizeEvent(QResizeEvent *event)
{
    const bool alreadyUpdating = m_updatingView;
    {
        QScopedValueRollback<bool> updating(m_updatingView, true);
        QGraphicsView::resizeEvent(event);
    }
    if (!alreadyUpdating) updateViewTransform(m_viewCenter);
}

void DrawingView::drawForeground(QPainter *painter, const QRectF &)
{
    painter->save();
    QPen pen(QColor("#b52c39"), 1);
    pen.setCosmetic(true);
    painter->setPen(pen);
    const double radius = 6.0 / std::abs(transform().m11());
    painter->drawLine(QPointF(-radius, 0), QPointF(radius, 0));
    painter->drawLine(QPointF(0, -radius), QPointF(0, radius));
    if (m_first) {
        pen.setWidth(2);
        pen.setColor(m_pickKind == DistanceKind::Current ? QColor("#d52d41") : QColor("#11804b"));
        painter->setPen(pen);
        painter->setBrush(pen.color());
        const double marker = 4.0 / std::abs(transform().m11());
        painter->drawEllipse(*m_first, marker, marker);
        if (m_second) {
            painter->drawLine(*m_first, *m_second);
            painter->drawEllipse(*m_second, marker, marker);
        } else if (m_hover) {
            pen.setStyle(Qt::DashLine); painter->setPen(pen);
            painter->drawLine(*m_first, *m_hover);
        }
    }
    if (m_drawingEnabled && !m_draftPoints.isEmpty()) {
        QPen draftPen(QColor("#1768b3"), 1.5, Qt::DashLine); draftPen.setCosmetic(true);
        painter->setPen(draftPen); painter->setBrush(shapeBrush());
        const QPointF first = m_draftPoints.first();
        if (m_activeTool == DrawingTool::Polygon) {
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(m_draftPoints);
            if (m_draftHover) painter->drawLine(m_draftPoints.last(), *m_draftHover);
        } else if (m_draftHover) {
            const QPointF second = m_activeTool == DrawingTool::Line && m_drawShift
                ? constrainedLinePoint(first, *m_draftHover) : *m_draftHover;
            if (m_activeTool == DrawingTool::Line) painter->drawLine(first, second);
            else if (m_activeTool == DrawingTool::Rectangle) painter->drawRect(QRectF(first, second).normalized());
            else if (m_activeTool == DrawingTool::Circle) {
                const double radius = QLineF(first, second).length();
                painter->drawEllipse(first, radius, radius);
            }
        }
        painter->setBrush(QColor("#1768b3"));
        const double marker = 3.0 / std::abs(transform().m11());
        for (const QPointF point : m_draftPoints) painter->drawEllipse(point, marker, marker);
    }
    painter->restore();
}
