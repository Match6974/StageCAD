/**
 * @file pdfpageitem.h
 * @brief PDF画像を実寸mmの矩形へ描画し、配置モデルの変更に追従する。
 */

#ifndef PDFPAGEITEM_H
#define PDFPAGEITEM_H

#include <QGraphicsObject>

class PdfPlacement;

// Drawing the raster into an mm rectangle avoids storing CAD geometry in pixels.
/// @brief 配置モデルを参照してPDFを描画するアイテム。参照先モデル自体は所有しない。
class PdfPageItem : public QGraphicsObject
{
    Q_OBJECT
public:
    /// @brief 参照先のPDF配置モデルへ接続し、シーン上の表示位置と寸法を同期する。
    /// @param placement PDFの実寸寸法と中心を保持する配置モデル。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit PdfPageItem(PdfPlacement *placement, QGraphicsItem *parent = nullptr);
    /// @brief このアイテム内でPDFが描画される実寸mmの矩形を返す。
    QRectF boundingRect() const override;
    /// @brief Y軸を補正してPDF画像を描画し、画像の上下とCAD座標の向きを一致させる。
    /// @param painter 呼び出し元から受け取る描画先。保存・復元で描画状態を保つ。
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    /// @brief 配置モデルの寸法・中心・ページ有無を表示アイテムへ反映する。
    void updateGeometry();
    // m_placement: 参照先のPDF配置モデル。描画アイテムはモデルを所有しない。
    PdfPlacement *m_placement;
    // m_bounds: PDFアイテム内の描画範囲（実寸mm）。中心移動はアイテムの位置で表す。
    QRectF m_bounds;
};

#endif
