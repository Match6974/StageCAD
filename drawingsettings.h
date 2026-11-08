/**
 * @file drawingsettings.h
 * @brief 作業領域の縮尺、用紙寸法、実寸mmと用紙mmの換算を扱う。
 */

#ifndef DRAWINGSETTINGS_H
#define DRAWINGSETTINGS_H

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

// STEP 1: settings for one drawing space. A future layer can own one instance.
// Model coordinates are real-world mm, with +X right and +Y up.
/// @brief 1つの作業領域の縮尺と用紙設定を保持する。図形の実寸座標は変更しない。
class DrawingSettings : public QObject
{
    Q_OBJECT
public:
    /// @brief 親QObjectを設定し、実寸1:1の作業領域設定を作る。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit DrawingSettings(QObject *parent = nullptr);

    /// @brief 現在選択されている縮尺の分母を返す。
    double scaleDenominator() const { return m_scaleDenominator; }
    /// @brief 有効な縮尺だけを反映し、値が変わった場合に通知する。不正値ではfalse。
    /// @param denominator 縮尺1:Nの分母N。
    bool setScaleDenominator(double denominator);
    /// @brief 縮尺の分母が有限で、描画可能な上下限内にあるかを返す。
    /// @param denominator 縮尺1:Nの分母N。
    static bool isValidScale(double denominator);

    /// @brief A4用紙の物理寸法をmmで返す。
    QSizeF paperSizeMm() const { return {210.0, 297.0}; }
    /// @brief 印刷可能範囲の物理寸法をmmで返す。
    QSizeF printableSizeMm() const { return {201.4, 287.9}; }
    /// @brief 用紙中心を原点とする用紙矩形を、現在の縮尺に応じた実寸mmで返す。
    QRectF paperRectInModel() const;
    /// @brief 印刷可能範囲を、現在の縮尺に応じた実寸mmで返す。
    QRectF printableRectInModel() const;

    // Paper coordinates are physical mm measured from the top-left of the sheet.
    /// @brief 実寸mmを用紙左上基準の物理mmへ換算し、Y軸の向きも変換する。
    /// @param modelMm 用紙中心基準の実寸座標（mm）。
    QPointF modelToPaperMm(QPointF modelMm) const;
    /// @brief 用紙左上基準の物理mmを、用紙中心基準の実寸mmへ逆変換する。
    /// @param paperMm 用紙左上基準の物理座標（mm）。
    QPointF paperToModelMm(QPointF paperMm) const;

    // Bounds avoid unusable transforms while permitting fractional/custom scales.
    // minimumScale: 描画変換を安定して扱える縮尺分母の下限。
    static constexpr double minimumScale = 0.000001;
    // maximumScale: 描画変換を安定して扱える縮尺分母の上限。
    static constexpr double maximumScale = 10000.0;

signals:
    /// @brief 有効な縮尺変更後の分母を通知する。
    /// @param denominator 縮尺1:Nの分母N。
    void scaleChanged(double denominator);

private:
    // m_scaleDenominator: 縮尺1:Nの分母N。実寸座標を用紙mmへ換算する倍率。
    double m_scaleDenominator = 1.0;
};

#endif
