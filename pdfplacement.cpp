#include "pdfplacement.h"
#include "drawingsettings.h"

#include <cmath>

PdfPlacement::PdfPlacement(QObject *parent) : QObject(parent) {}

bool PdfPlacement::validGeometry(QSizeF size, QPointF center)
{
    // Keep scene geometry finite and within a range usable by Qt's transforms.
    constexpr double smallest = 0.000001;
    constexpr double largest = 10000000;
    return std::isfinite(size.width()) && std::isfinite(size.height())
        && std::isfinite(center.x()) && std::isfinite(center.y())
        && size.width() >= smallest && size.height() >= smallest
        && size.width() <= largest && size.height() <= largest
        && std::abs(center.x()) + size.width() / 2 <= largest
        && std::abs(center.y()) + size.height() / 2 <= largest;
}

bool PdfPlacement::setPage(const QImage &image, QSizeF pagePoints, double importDenominator,
                          std::optional<PdfGeometry> saved)
{
    if (image.isNull() || !DrawingSettings::isValidScale(importDenominator)) return false;
    if (!std::isfinite(pagePoints.width()) || !std::isfinite(pagePoints.height())
        || pagePoints.width() <= 0 || pagePoints.height() <= 0) return false;
    const QSizeF size = saved ? saved->sizeMm : pagePoints * (25.4 / 72.0 * importDenominator);
    const QPointF center = saved ? saved->centerMm : QPointF();
    if (!validGeometry(size, center)) return false;
    m_image = image;
    m_sizeMm = size;
    m_centerMm = center;
    emit pageChanged();
    emit placementChanged();
    return true;
}

QRectF PdfPlacement::boundsMm() const
{
    if (!hasPage()) return {};
    return {m_centerMm - QPointF(m_sizeMm.width() / 2, m_sizeMm.height() / 2), m_sizeMm};
}

bool PdfPlacement::calibrate(double current, double target, QPointF fixedPoint)
{
    if (!hasPage() || !std::isfinite(current) || !std::isfinite(target)
        || current <= 0 || target <= 0 || !std::isfinite(fixedPoint.x())
        || !std::isfinite(fixedPoint.y())) return false;
    const double factor = target / current;
    const QSizeF size = m_sizeMm * factor;
    const QPointF center = fixedPoint + (m_centerMm - fixedPoint) * factor;
    if (!std::isfinite(factor) || factor <= 0 || !validGeometry(size, center)) return false;
    m_sizeMm = size;
    m_centerMm = center;
    emit placementChanged();
    return true;
}
