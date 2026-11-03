#ifndef PDFPLACEMENT_H
#define PDFPLACEMENT_H

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <optional>

struct PdfGeometry {
    QSizeF sizeMm;
    QPointF centerMm;
};

// The placed page has its own physical size. Drawing scale never mutates it.
class PdfPlacement : public QObject
{
    Q_OBJECT
public:
    explicit PdfPlacement(QObject *parent = nullptr);
    bool setPage(const QImage &image, QSizeF pagePoints, double importDenominator,
                 std::optional<PdfGeometry> saved = std::nullopt);
    bool hasPage() const { return !m_image.isNull(); }
    const QImage &image() const { return m_image; }
    QSizeF sizeMm() const { return m_sizeMm; }
    QPointF centerMm() const { return m_centerMm; }
    QRectF boundsMm() const;
    PdfGeometry geometry() const { return {m_sizeMm, m_centerMm}; }

    // Use a caller-selected fixed point; this is independent of drawing scale.
    bool calibrate(double currentDistanceMm, double newDistanceMm, QPointF fixedPointMm);

signals:
    void pageChanged();
    void placementChanged();

private:
    static bool validGeometry(QSizeF sizeMm, QPointF centerMm);
    QImage m_image;
    QSizeF m_sizeMm;
    QPointF m_centerMm;
};

#endif
