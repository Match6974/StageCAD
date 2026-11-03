#include "pdfpageitem.h"
#include "pdfplacement.h"

#include <QPainter>

PdfPageItem::PdfPageItem(PdfPlacement *placement, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_placement(placement)
{
    setZValue(-500);
    connect(placement, &PdfPlacement::pageChanged, this, &PdfPageItem::updateGeometry);
    connect(placement, &PdfPlacement::placementChanged, this, &PdfPageItem::updateGeometry);
    updateGeometry();
}

void PdfPageItem::updateGeometry()
{
    prepareGeometryChange();
    const QSizeF size = m_placement->sizeMm();
    m_bounds = m_placement->hasPage()
        ? QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()) : QRectF();
    setPos(m_placement->centerMm());
    setVisible(m_placement->hasPage());
    update();
}

QRectF PdfPageItem::boundingRect() const { return m_bounds; }

void PdfPageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // CAD +Y is up, but the image's first scanline belongs at the page's top.
    painter->scale(1, -1);
    painter->fillRect(m_bounds, Qt::white);
    painter->drawImage(m_bounds, m_placement->image());
    painter->restore();
}
