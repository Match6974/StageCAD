/**
 * @file pdfpageitem.cpp
 * @brief PDF画像を実寸mmの矩形へ描画し、配置モデルの変更に追従する。
 */

#include "pdfpageitem.h"
#include "pdfplacement.h"

#include <QPainter>

/// @brief 参照先のPDF配置モデルへ接続し、シーン上の表示位置と寸法を同期する。
/// @param placement PDFの実寸寸法と中心を保持する配置モデル。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
PdfPageItem::PdfPageItem(PdfPlacement *placement, QGraphicsItem *parent)
    : QGraphicsObject(parent), m_placement(placement)
{
    setZValue(-500);
    connect(placement, &PdfPlacement::pageChanged, this, &PdfPageItem::updateGeometry);
    connect(placement, &PdfPlacement::placementChanged, this, &PdfPageItem::updateGeometry);
    updateGeometry();
}

/// @brief 配置モデルの寸法・中心・ページ有無を表示アイテムへ反映する。
void PdfPageItem::updateGeometry()
{
    prepareGeometryChange();
    // size: 配置モデルが保持するPDFの実寸幅・高さ（mm）。
    const QSizeF size = m_placement->sizeMm();
    m_bounds = m_placement->hasPage()
        ? QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()) : QRectF();
    setPos(m_placement->centerMm());
    setVisible(m_placement->hasPage());
    update();
}

/// @brief このアイテム内でPDFが描画される実寸mmの矩形を返す。
QRectF PdfPageItem::boundingRect() const { return m_bounds; }

/// @brief Y軸を補正してPDF画像を描画し、画像の上下とCAD座標の向きを一致させる。
/// @param painter 呼び出し元から受け取る描画先。保存・復元で描画状態を保つ。
void PdfPageItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *)
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // CAD +Y is up, but the image's first scanline belongs at the page's top.
    painter->scale(1, -1);
    painter->fillRect(m_bounds, Qt::white);
    painter->drawImage(m_bounds, m_placement->image());
    painter->restore();
}
