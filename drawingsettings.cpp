/**
 * @file drawingsettings.cpp
 * @brief 作業領域の縮尺、用紙寸法、実寸mmと用紙mmの換算を扱う。
 */

#include "drawingsettings.h"

#include <cmath>

/// @brief 親QObjectを設定し、実寸1:1の作業領域設定を作る。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
DrawingSettings::DrawingSettings(QObject *parent) : QObject(parent) {}

/// @brief 縮尺の分母が有限で、描画可能な上下限内にあるかを返す。
/// @param denominator 縮尺1:Nの分母N。
bool DrawingSettings::isValidScale(double denominator)
{
    return std::isfinite(denominator) && denominator >= minimumScale
        && denominator <= maximumScale;
}

/// @brief 有効な縮尺だけを反映し、値が変わった場合に通知する。不正値ではfalse。
/// @param denominator 縮尺1:Nの分母N。
bool DrawingSettings::setScaleDenominator(double denominator)
{
    if (!isValidScale(denominator)) return false;
    if (m_scaleDenominator != denominator) {
        m_scaleDenominator = denominator;
        emit scaleChanged(denominator);
    }
    return true;
}

/// @brief 用紙中心を原点とする用紙矩形を、現在の縮尺に応じた実寸mmで返す。
QRectF DrawingSettings::paperRectInModel() const
{
    // size: 用紙または印刷可能範囲を縮尺で換算した実寸幅・高さ（mm）。
    const QSizeF size = paperSizeMm() * m_scaleDenominator;
    return {-size.width() / 2, -size.height() / 2, size.width(), size.height()};
}

/// @brief 印刷可能範囲を、現在の縮尺に応じた実寸mmで返す。
QRectF DrawingSettings::printableRectInModel() const
{
    // size: 用紙または印刷可能範囲を縮尺で換算した実寸幅・高さ（mm）。
    const QSizeF size = printableSizeMm() * m_scaleDenominator;
    return {-size.width() / 2, -size.height() / 2, size.width(), size.height()};
}

/// @brief 実寸mmを用紙左上基準の物理mmへ換算し、Y軸の向きも変換する。
/// @param modelMm 用紙中心基準の実寸座標（mm）。
QPointF DrawingSettings::modelToPaperMm(QPointF modelMm) const
{
    // paper: 用紙の物理寸法（mm）。
    const QSizeF paper = paperSizeMm();
    return {paper.width() / 2 + modelMm.x() / m_scaleDenominator,
            paper.height() / 2 - modelMm.y() / m_scaleDenominator};
}

/// @brief 用紙左上基準の物理mmを、用紙中心基準の実寸mmへ逆変換する。
/// @param paperMm 用紙左上基準の物理座標（mm）。
QPointF DrawingSettings::paperToModelMm(QPointF paperMm) const
{
    // paper: 用紙の物理寸法（mm）。
    const QSizeF paper = paperSizeMm();
    return {(paperMm.x() - paper.width() / 2) * m_scaleDenominator,
            (paper.height() / 2 - paperMm.y()) * m_scaleDenominator};
}
