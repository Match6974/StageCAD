/**
 * @file pdfplacement.h
 * @brief 取り込んだPDFページの画像、実寸寸法、中心位置、距離補正を管理する。
 */

#ifndef PDFPLACEMENT_H
#define PDFPLACEMENT_H

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <optional>

/// @brief PDFページごとに復元する実寸寸法と中心座標の値データ。
struct PdfGeometry {
    // sizeMm: 実寸の幅と高さ（mm）。
    QSizeF sizeMm;
    // centerMm: 中心の実寸座標（mm）。
    QPointF centerMm;
};

// The placed page has its own physical size. Drawing scale never mutates it.
/// @brief PDF画像と実寸の配置を保持し、指定固定点を保った縦横同比率の補正を行う。
class PdfPlacement : public QObject
{
    Q_OBJECT
public:
    /// @brief 親QObjectを設定して、PDF未読込の配置モデルを作る。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit PdfPlacement(QObject *parent = nullptr);
    /// @brief PDF画像とページ寸法を検証して配置する。保存済み配置があれば復元し、失敗時は保持する。
    /// @param image 描画または取り込みに使用するラスタ画像。
    /// @param pagePoints PDFページの寸法（1pt=1/72インチ）。
    /// @param importDenominator PDFのページ寸法を実寸へ換算する取り込み時の縮尺分母。
    /// @param saved 復元するページ配置。未指定なら取り込み時の縮尺から初期配置を計算する。
    bool setPage(const QImage &image, QSizeF pagePoints, double importDenominator,
                 std::optional<PdfGeometry> saved = std::nullopt);
    /// @brief 取り込み済みのPDFページ画像が存在するかを返す。
    bool hasPage() const { return !m_image.isNull(); }
    /// @brief 現在のPDFページ画像を読み取り専用で返す。
    const QImage &image() const { return m_image; }
    /// @brief PDFページの実寸幅・高さをmmで返す。
    QSizeF sizeMm() const { return m_sizeMm; }
    /// @brief PDFページ中心の実寸座標をmmで返す。
    QPointF centerMm() const { return m_centerMm; }
    /// @brief PDF中心と寸法から実寸mmの外接矩形を返す。未読込なら空の矩形。
    QRectF boundsMm() const;
    /// @brief PDFページの寸法と中心を、ページ別の保存・復元に使う値として返す。
    PdfGeometry geometry() const { return {m_sizeMm, m_centerMm}; }

    // Use a caller-selected fixed point; this is independent of drawing scale.
    /// @brief 指定点を固定してPDFを距離比で伸縮する。不正な距離や配置範囲では変更しない。
    /// @param currentDistanceMm 補正前の現在距離（実寸mm）。
    /// @param newDistanceMm 補正後に合わせる新しい距離（実寸mm）。
    /// @param fixedPointMm 補正時に動かさない固定点（実寸mm）。
    bool calibrate(double currentDistanceMm, double newDistanceMm, QPointF fixedPointMm);

signals:
    /// @brief 取り込まれたPDFページ画像の更新を通知する。
    void pageChanged();
    /// @brief PDFの実寸寸法または中心位置の更新を通知する。
    void placementChanged();

private:
    /// @brief PDFの寸法・中心が有限で、描画可能な実寸mmの範囲内かを確認する。
    /// @param sizeMm 実寸の幅と高さ（mm）。
    /// @param centerMm 中心の実寸座標（mm）。
    static bool validGeometry(QSizeF sizeMm, QPointF centerMm);
    // m_image: 取り込み済みページのラスタ画像。配置モデルが値として保持する。
    QImage m_image;
    // m_sizeMm: PDFページの実寸幅・高さ（mm）。
    QSizeF m_sizeMm;
    // m_centerMm: PDFページ中心の実寸座標（mm）。
    QPointF m_centerMm;
};

#endif
