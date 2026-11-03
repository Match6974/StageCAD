#ifndef DRAWINGSETTINGS_H
#define DRAWINGSETTINGS_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

// STEP 1: settings for one drawing space. A future layer can own one instance.
// Model coordinates are real-world mm, with +X right and +Y up.
class DrawingSettings : public QObject
{
    Q_OBJECT
public:
    explicit DrawingSettings(QObject *parent = nullptr);

    double scaleDenominator() const { return m_scaleDenominator; }
    bool setScaleDenominator(double denominator);
    static bool isValidScale(double denominator);

    QSizeF paperSizeMm() const { return {210.0, 297.0}; }
    QSizeF printableSizeMm() const { return {201.4, 287.9}; }
    QRectF paperRectInModel() const;
    QRectF printableRectInModel() const;

    // Paper coordinates are physical mm measured from the top-left of the sheet.
    QPointF modelToPaperMm(QPointF modelMm) const;
    QPointF paperToModelMm(QPointF paperMm) const;

    // Bounds avoid unusable transforms while permitting fractional/custom scales.
    static constexpr double minimumScale = 0.000001;
    static constexpr double maximumScale = 10000.0;

signals:
    void scaleChanged(double denominator);

private:
    double m_scaleDenominator = 1.0;
};

#endif
