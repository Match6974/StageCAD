#ifndef PDFPAGEITEM_H
#define PDFPAGEITEM_H

#include <QGraphicsObject>

class PdfPlacement;

// Drawing the raster into an mm rectangle avoids storing CAD geometry in pixels.
class PdfPageItem : public QGraphicsObject
{
    Q_OBJECT
public:
    explicit PdfPageItem(PdfPlacement *placement, QGraphicsItem *parent = nullptr);
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    void updateGeometry();
    PdfPlacement *m_placement;
    QRectF m_bounds;
};

#endif
