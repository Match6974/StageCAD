/**
 * @file drawingdocument.h
 * @brief 確定済み図形のID・描画順と、追加・形状更新・移動・削除の変更通知を管理する。
 */

#ifndef DRAWINGDOCUMENT_H
#define DRAWINGDOCUMENT_H

#include "shapedata.h"
#include <QObject>
#include <QList>
#include <optional>

// Owns committed CAD data without depending on a scene or widget. DrawingView
// owns only its rendering items and temporary interaction previews.
/// @brief 確定済み図形を所有するモデル。シーンやウィジェットを持たず、変更を通知する。
class DrawingDocument : public QObject {
    Q_OBJECT
public:
    /// @brief 親QObjectを設定して、空の図形モデルを作る。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit DrawingDocument(QObject *parent = nullptr) : QObject(parent) {}
    /// @brief 描画順に並んだ確定済み図形を読み取り専用で参照する。
    const QList<ShapeData> &shapes() const { return m_shapes; }
    /// @brief IDに対応する図形のコピーを返す。存在しない場合は空のoptionalを返す。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    std::optional<ShapeData> shape(const QUuid &id) const;
    /// @brief 有効な形状へ新しいUUIDを付けて追加する。不正値や再入時は空のUUIDを返す。
    /// @param geometryMm 配置前の形状データ（実寸mm）。
    QUuid addShape(ShapeGeometry geometryMm);
    /// @brief 対象IDと移動後の形状を全件検証する。モデルは変更しない。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    /// @param deltaMm 図形全体へ加える移動量（実寸mm）。
    bool canTranslateShapes(const QList<QUuid> &ids, QPointF deltaMm) const;
    /// @brief 全件を検証して移動量を一括反映する。失敗時は全図形を変更しない。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    /// @param deltaMm 図形全体へ加える移動量（実寸mm）。
    bool translateShapes(const QList<QUuid> &ids, QPointF deltaMm);
    /// @brief 存在を全件確認して図形を削除する。重複IDは1回だけ処理する。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    bool removeShapes(const QList<QUuid> &ids);
    /// @brief 直線の両端を実寸座標で更新する。ID・描画順は保ち、不正値では変更しない。
    /// 同じ実寸形状なら通知しない。更新時は形状を実寸座標へ統合し、配置移動量を0にする。
    /// @param id 更新対象の直線UUID。他の種類や存在しないIDは拒否する。
    /// @param lineMm 始点・終点を用紙中心基準の実寸mmで表した線分。
    bool setLineEndpoints(const QUuid &id, const QLineF &lineMm);
    /// @brief 四角形を実寸の矩形へ更新する。固定位置の計算は呼び出し側が決める。
    /// ID・描画順を保持し、不正形状は拒否する。更新時の配置移動量は0へ統合する。
    /// @param id 更新対象の四角形UUID。他の種類は拒否する。
    /// @param boundsMm 配置移動量を反映済みの実寸矩形（mm）。幅・高さは正の値が必要。
    bool setRectangleBounds(const QUuid &id, const QRectF &boundsMm);
    /// @brief 円の中心を保って半径だけを更新する。真円とUUIDを維持する。
    /// @param id 更新対象の円UUID。他の種類や存在しないIDは拒否する。
    /// @param radiusMm 新しい半径（実寸mm）。有限の正値で、外接範囲も有効な値に限る。
    bool setCircleRadius(const QUuid &id, double radiusMm);

signals:
    /// @brief 有効な変更の直前を通知し、ビューの未確定操作を取り消せるようにする。
    void aboutToChange();
    /// @brief 追加された図形IDを通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeAdded(const QUuid &id);
    /// @brief 形状・配置を反映済みの図形IDを通知する。一括処理では全データの更新後に通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeChanged(const QUuid &id);
    /// @brief 削除された図形IDを通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeRemoved(const QUuid &id);

private:
    // m_shapes: モデルが値として所有する確定図形。描画・重なり順を保持する。
    QList<ShapeData> m_shapes; // In drawing/stacking order.
    // m_mutating: モデルの変更通知中に入れ子の変更を受け付けないための再入防止フラグ。
    bool m_mutating = false;
};

#endif
