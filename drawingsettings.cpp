#include "drawingsettings.h"

#include <cmath>

DrawingSettings::DrawingSettings(QObject *parent) : QObject(parent) {}

bool DrawingSettings::isValidScale(double denominator)
{
    return std::isfinite(denominator) && denominator >= minimumScale
        && denominator <= maximumScale;
}

bool DrawingSettings::setScaleDenominator(double denominator)
{
    if (!isValidScale(denominator)) return false;
    if (m_scaleDenominator != denominator) {
        m_scaleDenominator = denominator;
        emit scaleChanged(denominator);
    }
    return true;
}

QRectF DrawingSettings::paperRectInModel() const
{
    const QSizeF size = paperSizeMm() * m_scaleDenominator;
    return {-size.width() / 2, -size.height() / 2, size.width(), size.height()};
}

QRectF DrawingSettings::printableRectInModel() const
{
    const QSizeF size = printableSizeMm() * m_scaleDenominator;
    return {-size.width() / 2, -size.height() / 2, size.width(), size.height()};
}

QPointF DrawingSettings::modelToPaperMm(QPointF modelMm) const
{
    const QSizeF paper = paperSizeMm();
    return {paper.width() / 2 + modelMm.x() / m_scaleDenominator,
            paper.height() / 2 - modelMm.y() / m_scaleDenominator};
}

QPointF DrawingSettings::paperToModelMm(QPointF paperMm) const
{
    const QSizeF paper = paperSizeMm();
    return {(paperMm.x() - paper.width() / 2) * m_scaleDenominator,
            (paper.height() / 2 - paperMm.y()) * m_scaleDenominator};
}
