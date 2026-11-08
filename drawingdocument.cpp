/**
 * @file drawingdocument.cpp
 * @brief 確定済み図形のID・描画順と、追加・移動・削除の変更通知を管理する。
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
