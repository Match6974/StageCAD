#include "drawingview.h"
#include "drawingsettings.h"

#include <QGraphicsRectItem>
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
        m_panning = false;
        m_activeTool = tool;
        updateInteractionCursor();
        emit activeToolChanged(tool);
    }
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
    const double margin = 10 * m_settings->scaleDenominator();
    const QRectF paperBounds = m_paper->rect().adjusted(-margin, -margin, margin, margin);
    scene()->setSceneRect(m_contentBounds.isEmpty() ? paperBounds : paperBounds.united(m_contentBounds));
    updateViewTransform({});
    emit cursorLeft();
}

void DrawingView::setContentBounds(QRectF bounds)
{
    m_contentBounds = bounds;
    const double margin = 10 * m_settings->scaleDenominator();
    const QRectF paperBounds = m_paper->rect().adjusted(-margin, -margin, margin, margin);
    scene()->setSceneRect(bounds.isEmpty() ? paperBounds : paperBounds.united(bounds));
    setViewCenter(m_viewCenter);
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
    }
    QGraphicsView::mouseMoveEvent(event);
}

void DrawingView::startDistancePick(DistanceKind kind, QRectF pdfBounds)
{
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
        m_panning = true; m_panButton = event->button(); m_lastPanPosition = event->pos();
        setFocus(); updateInteractionCursor(); event->accept(); return;
    }
    if (m_picking) {
        m_pressPosition = event->pos(); m_dragged = false;
        event->accept(); return;
    }
    QGraphicsView::mousePressEvent(event);
}

void DrawingView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        if (event->button() == m_panButton) { m_panning = false; updateInteractionCursor(); }
        event->accept(); return;
    }
    if (!m_picking) { QGraphicsView::mouseReleaseEvent(event); return; }
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

void DrawingView::keyPressEvent(QKeyEvent *event)
{
    if (m_picking && event->key() == Qt::Key_Escape) {
        clearDistancePick(); emit distancePickCanceled(); event->accept(); return;
    }
    if (m_panning && event->key() == Qt::Key_Escape) {
        m_panning = false; updateInteractionCursor(); event->accept(); return;
    }
    QGraphicsView::keyPressEvent(event);
}

void DrawingView::leaveEvent(QEvent *event)
{
    emit cursorLeft();
    m_hover.reset();
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
    painter->restore();
}
