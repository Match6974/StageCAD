/**
 * @file drawingdocument.cpp
 * @brief 確定済み図形のID・描画順と、追加・形状更新・移動・削除の変更通知を管理する。
 */

#include "drawingdocument.h"
#include <QScopedValueRollback>
#include <QSet>
#include <algorithm>
#include <cmath>

/// @brief IDに対応する図形のコピーを返す。存在しない場合は空のoptionalを返す。
/// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
std::optional<ShapeData> DrawingDocument::shape(const QUuid &id) const
{
    // shape: 描画順に処理する確定済み図形データ。
    for (const auto &shape : m_shapes) if (shape.id == id) return shape;
    return std::nullopt;
}

/// @brief 有効な形状へ新しいUUIDを付けて追加する。不正値や再入時は空のUUIDを返す。
/// @param geometryMm 配置前の形状データ（実寸mm）。
QUuid DrawingDocument::addShape(ShapeGeometry geometryMm)
{
    if (m_mutating) return {};
    // data: 検証・追加・表示同期に使う図形データ。
    ShapeData data{QUuid::createUuid(), std::move(geometryMm), {}};
    if (!data.isValid()) return {};
    // mutating: モデル更新中の再入防止フラグをスコープ終了時に復元するガード。
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    m_shapes.append(data); emit shapeAdded(data.id);
    return data.id;
}

/// @brief 対象IDと移動後の形状を全件検証する。モデルは変更しない。
/// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
/// @param delta 開始位置から加える移動量（実寸mm）。
bool DrawingDocument::canTranslateShapes(const QList<QUuid> &ids, QPointF delta) const
{
    if (ids.isEmpty() || !std::isfinite(delta.x()) || !std::isfinite(delta.y())) return false;
    // id: 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    for (const auto &id : ids) {
        // data: 検証・追加・表示同期に使う図形データ。
        auto data = shape(id);
        if (!data) return false;
        data->positionMm += delta;
        if (!data->isValid()) return false;
    }
    return true;
}

/// @brief 全件を検証して移動量を一括反映する。失敗時は全図形を変更しない。
/// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
/// @param delta 開始位置から加える移動量（実寸mm）。
bool DrawingDocument::translateShapes(const QList<QUuid> &ids, QPointF delta)
{
    if (m_mutating || !canTranslateShapes(ids, delta)) return false;
    // unique: 重複を取り除いた対象UUIDの集合。
    const QSet<QUuid> unique(ids.cbegin(), ids.cend());
    // mutating: モデル更新中の再入防止フラグをスコープ終了時に復元するガード。
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    // Validate the whole operation first, then update all data before notifying views.
    // data: 検証・追加・表示同期に使う図形データ。
    for (auto &data : m_shapes) if (unique.contains(data.id)) data.positionMm += delta;
    // id: 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    for (const auto &id : unique) emit shapeChanged(id);
    return true;
}

/// @brief 存在を全件確認して図形を削除する。重複IDは1回だけ処理する。
/// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
bool DrawingDocument::removeShapes(const QList<QUuid> &ids)
{
    if (m_mutating || ids.isEmpty()) return false;
    // id: 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    for (const auto &id : ids) if (!shape(id)) return false;
    // unique: 重複を取り除いた対象UUIDの集合。
    const QSet<QUuid> unique(ids.cbegin(), ids.cend());
    // mutating: モデル更新中の再入防止フラグをスコープ終了時に復元するガード。
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    m_shapes.erase(std::remove_if(m_shapes.begin(), m_shapes.end(),
        // data: 検証・追加・表示同期に使う図形データ。
        // コールバック: 対象UUIDの集合に含まれる図形だけを削除対象として判定する。
        [&](const ShapeData &data) { return unique.contains(data.id); }), m_shapes.end());
    // id: 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    for (const auto &id : unique) emit shapeRemoved(id);
    return true;
}

/// @brief 実寸の端点を検証して直線を更新する。座標やサイズが不正な場合は形状を維持する。
/// @param id 変更する直線のUUID。
/// @param lineMm 配置移動量を反映済みの始点・終点（実寸mm）。
bool DrawingDocument::setLineEndpoints(const QUuid &id, const QLineF &lineMm)
{
    if (m_mutating) return false;
    // original: 現在の確定データのコピー。種類と変更前の実寸形状を確認する。
    const auto original = shape(id);
    if (!original || original->kind() != ShapeKind::Line) return false;
    // next: 実寸座標に統合した候補形状。配置を0にし、ローカル座標の範囲制限にも合わせる。
    const ShapeData next{id, lineMm, {}};
    if (!next.isValid()) return false;
    // current: 比較用の変更前の両端座標（実寸mm）。
    const QLineF current = std::get<QLineF>(original->geometryMm).translated(original->positionMm);
    // 微小な有効変更を失わないよう、座標ごとに厳密比較して無変更だけを除く。
    if (current.x1() == lineMm.x1() && current.y1() == lineMm.y1()
        && current.x2() == lineMm.x2() && current.y2() == lineMm.y2()) return true;
    // mutating: 検証後から通知終了まで、文書の入れ子変更を拒否するガード。
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    // data: ID・描画順を保って形状と配置だけを置き換える文書内の図形。
    for (auto &data : m_shapes) if (data.id == next.id) { data = next; break; }
    // 呼び出し元のIDが通知処理中に変化しても、検証済みの対象UUIDを維持する。
    emit shapeChanged(next.id);
    return true;
}
