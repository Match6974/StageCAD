/**
 * @file pdfplacement.cpp
 * @brief 取り込んだPDFページの画像、実寸寸法、中心位置、距離補正を管理する。
 */

#include "pdfplacement.h"
#include "drawingsettings.h"

#include <cmath>

/// @brief 親QObjectを設定して、PDF未読込の配置モデルを作る。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
PdfPlacement::PdfPlacement(QObject *parent) : QObject(parent) {}

/// @brief PDFの寸法・中心が有限で、描画可能な実寸mmの範囲内かを確認する。
/// @param size 検証・適用するPDFの実寸幅・高さ（mm）。
/// @param center 用紙中心基準の実寸座標（mm）。
bool PdfPlacement::validGeometry(QSizeF size, QPointF center)
{
    // Keep scene geometry finite and within a range usable by Qt's transforms.
    // smallest: PDFの実寸幅・高さの下限（mm）。
    constexpr double smallest = 0.000001;
    // largest: PDFの寸法と外接座標に使う上限（mm）。
    constexpr double largest = 10000000;
    return std::isfinite(size.width()) && std::isfinite(size.height())
        && std::isfinite(center.x()) && std::isfinite(center.y())
        && size.width() >= smallest && size.height() >= smallest
        && size.width() <= largest && size.height() <= largest
        && std::abs(center.x()) + size.width() / 2 <= largest
        && std::abs(center.y()) + size.height() / 2 <= largest;
}

/// @brief PDF画像とページ寸法を検証して配置する。保存済み配置があれば復元し、失敗時は保持する。
/// @param image 描画または取り込みに使用するラスタ画像。
/// @param pagePoints PDFページの寸法（1pt=1/72インチ）。
/// @param importDenominator PDFのページ寸法を実寸へ換算する取り込み時の縮尺分母。
/// @param saved 復元するページ配置。未指定なら取り込み時の縮尺から初期配置を計算する。
bool PdfPlacement::setPage(const QImage &image, QSizeF pagePoints, double importDenominator,
                          std::optional<PdfGeometry> saved)
{
    if (image.isNull() || !DrawingSettings::isValidScale(importDenominator)) return false;
    if (!std::isfinite(pagePoints.width()) || !std::isfinite(pagePoints.height())
        || pagePoints.width() <= 0 || pagePoints.height() <= 0) return false;
    // size: 検証・適用するPDFの実寸幅・高さ（mm）。
    const QSizeF size = saved ? saved->sizeMm : pagePoints * (25.4 / 72.0 * importDenominator);
    // center: 用紙中心基準の実寸座標（mm）。
    const QPointF center = saved ? saved->centerMm : QPointF();
    if (!validGeometry(size, center)) return false;
    m_image = image;
    m_sizeMm = size;
    m_centerMm = center;
    emit pageChanged();
    emit placementChanged();
    return true;
}

/// @brief PDF中心と寸法から実寸mmの外接矩形を返す。未読込なら空の矩形。
QRectF PdfPlacement::boundsMm() const
{
    if (!hasPage()) return {};
    return {m_centerMm - QPointF(m_sizeMm.width() / 2, m_sizeMm.height() / 2), m_sizeMm};
}

/// @brief 指定点を固定してPDFを距離比で伸縮する。不正な距離や配置範囲では変更しない。
/// @param current 補正前に測定した現在距離（実寸mm）。
/// @param target 補正後に合わせる新しい距離（実寸mm）。
/// @param fixedPoint 用紙中心基準の実寸座標（mm）。
bool PdfPlacement::calibrate(double current, double target, QPointF fixedPoint)
{
    if (!hasPage() || !std::isfinite(current) || !std::isfinite(target)
        || current <= 0 || target <= 0 || !std::isfinite(fixedPoint.x())
        || !std::isfinite(fixedPoint.y())) return false;
    // factor: 新しい距離を現在距離で割った縦横共通の補正倍率。
    const double factor = target / current;
    // size: 検証・適用するPDFの実寸幅・高さ（mm）。
    const QSizeF size = m_sizeMm * factor;
    // center: 用紙中心基準の実寸座標（mm）。
    const QPointF center = fixedPoint + (m_centerMm - fixedPoint) * factor;
    if (!std::isfinite(factor) || factor <= 0 || !validGeometry(size, center)) return false;
    m_sizeMm = size;
    m_centerMm = center;
    emit placementChanged();
    return true;
}
