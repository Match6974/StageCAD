#include "drawingview.h"
#include "drawingsettings.h"

#include <QGraphicsRectItem>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QApplication>
#include <QKeyEvent>
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
    m_displayZoom = zoom;
    updateViewTransform();
    return true;
}

void DrawingView::updatePaper()
{
    // Only the paper's real-world coverage changes; model items are untouched.
    m_paper->setRect(m_settings->paperRectInModel());
    m_printableArea->setRect(m_settings->printableRectInModel());
    const double margin = 10 * m_settings->scaleDenominator();
    const QRectF paperBounds = m_paper->rect().adjusted(-margin, -margin, margin, margin);
    scene()->setSceneRect(m_contentBounds.isEmpty() ? paperBounds : paperBounds.united(m_contentBounds));
    updateViewTransform();
    emit cursorLeft();
}

void DrawingView::setContentBounds(QRectF bounds)
{
    m_contentBounds = bounds;
    updatePaper();
}

void DrawingView::updateViewTransform()
{
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
    centerOn(0, 0);
}

void DrawingView::mouseMoveEvent(QMouseEvent *event)
{
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
    viewport()->setCursor(Qt::CrossCursor);
    setFocus();
}

void DrawingView::clearDistancePick()
{
    m_picking = false;
    m_first.reset(); m_second.reset(); m_hover.reset();
    viewport()->unsetCursor();
    viewport()->update();
}

void DrawingView::mousePressEvent(QMouseEvent *event)
{
    if (m_picking) {
        m_pressPosition = event->pos(); m_dragged = false;
        event->accept(); return;
    }
    QGraphicsView::mousePressEvent(event);
}

void DrawingView::mouseReleaseEvent(QMouseEvent *event)
{
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
        viewport()->unsetCursor();
        emit distancePicked(m_pickKind, *m_first, *m_second);
    }
    viewport()->update();
}

void DrawingView::keyPressEvent(QKeyEvent *event)
{
    if (m_picking && event->key() == Qt::Key_Escape) {
        clearDistancePick(); emit distancePickCanceled(); event->accept(); return;
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
    QGraphicsView::resizeEvent(event);
    updateViewTransform();
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
