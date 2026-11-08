/**
 * @file drawingdocument.cpp
 * @brief 確定済み図形のID・描画順と、追加・形状更新・移動・削除の変更通知を管理する。
 */

#include "drawingdocument.h"
#include <QScopedValueRollback>
#include <QSet>
#include <QHash>
#include <QUndoStack>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>

namespace {
/// @brief 微小な有効変更を失わないよう、実寸点を成分ごとに厳密比較する。
/// @param a 比較する第1の実寸点（mm）。
/// @param b 比較する第2の実寸点（mm）。
bool samePoint(QPointF a,QPointF b) { return a.x() == b.x() && a.y() == b.y(); }
/// @brief 配置移動量の表現が異なっても、実寸形状・属性が同じかを厳密比較する。
/// @param a 比較元の図形。
/// @param b 比較先の図形。
bool sameShape(const ShapeData &a,const ShapeData &b)
{
    if (a.id != b.id || a.kind() != b.kind() || a.rectangleAngleDeg != b.rectangleAngleDeg
        || a.polygonClosed != b.polygonClosed) return false;
    if (const auto *line = std::get_if<QLineF>(&a.geometryMm)) {
        // other: 配置を加えて端点比較する第2の線分。
        const auto &other = std::get<QLineF>(b.geometryMm);
        return samePoint(line->p1()+a.positionMm,other.p1()+b.positionMm)
            && samePoint(line->p2()+a.positionMm,other.p2()+b.positionMm);
    }
    if (const auto *rect = std::get_if<QRectF>(&a.geometryMm)) {
        // other: 回転角が同じ矩形の辺長と実寸左下を比較する相手。
        const auto &other = std::get<QRectF>(b.geometryMm);
        return samePoint(rect->topLeft()+a.positionMm,other.topLeft()+b.positionMm)
            && rect->width() == other.width() && rect->height() == other.height();
    }
    if (const auto *circle = std::get_if<CircleGeometry>(&a.geometryMm)) {
        // other: 中心・半径・円弧属性を比較する円。
        const auto &other = std::get<CircleGeometry>(b.geometryMm);
        return samePoint(circle->centerMm+a.positionMm,other.centerMm+b.positionMm)
            && circle->radiusMm == other.radiusMm && circle->startAngleDeg == other.startAngleDeg && circle->sweepAngleDeg == other.sweepAngleDeg;
    }
    // points: 第1のローカル頂点列。other: 同じ順序で比較する第2の頂点列。
    const auto &points = std::get<QPolygonF>(a.geometryMm), &other = std::get<QPolygonF>(b.geometryMm);
    if (points.size() != other.size()) return false;
    // index: 配置込み実寸値を比較する頂点番号。
    for (qsizetype index=0; index<points.size(); ++index)
        if (!samePoint(points[index]+a.positionMm,other[index]+b.positionMm)) return false;
    return true;
}
}

/// @brief 変更された図形だけを保持し、文書へ原子的に復元する履歴コマンド。文書の履歴が所有する。
class DocumentEditCommand : public QUndoCommand {
public:
    /// @brief 前後リストから追加・削除・更新の差分と元の描画位置を保存する。
    /// @param document 復元先の文書。履歴より長く存在する。
    /// @param next 検証済みの変更後リスト。
    /// @param text 操作の表示名。
    DocumentEditCommand(DrawingDocument *document,const QList<ShapeData> &next,const QString &text)
        : QUndoCommand(text),m_document(document) {
        // afterById: 変更後のUUID検索表。全図形を変更する移動でも二重走査を避ける。
        QHash<QUuid,const ShapeData*> afterById;
        // data: 検証済み候補リスト内で生存する各図形への参照。
        for (const auto &data : next) afterById.insert(data.id,&data);
        // beforeIds: 変更前に存在したUUIDの集合。追加分の判定に使う。
        QSet<QUuid> beforeIds;
        // index: 変更前の描画順。削除を取り消すときに同じ位置へ挿入する。
        for (qsizetype index=0; index<document->m_shapes.size(); ++index) {
            // before: 追加・削除・更新を分類する変更前の図形。
            const auto &before = document->m_shapes[index]; beforeIds.insert(before.id);
            // after: 同じUUIDの変更後の図形。見つからなければ削除。
            const auto *after = afterById.value(before.id,nullptr);
            if (!after) m_changes.append({before,std::nullopt,index});
            else if (!sameShape(before,*after)) m_changes.append({before,*after,index});
        }
        // index: 新しい図形を追加する変更後の描画位置。
        for (qsizetype index=0; index<next.size(); ++index)
            if (!beforeIds.contains(next[index].id)) m_changes.append({std::nullopt,next[index],index});
    }
    /// @brief 実質的な変更が1件もない場合は履歴へ追加しない。
    bool empty() const { return m_changes.isEmpty(); }
    /// @brief 変更前の値・UUID・描画順へ戻す。
    void undo() override { apply(false); }
    /// @brief 検証済みの変更後の値・UUID・描画順を再現する。
    void redo() override { apply(true); }
private:
    /// @brief 1件の図形変更。未設定のbeforeは追加、未設定のafterは削除を表す。
    struct Change {
        // before: 変更前の正確な形状・属性。
        std::optional<ShapeData> before;
        // after: 変更後の正確な形状・属性。
        std::optional<ShapeData> after;
        // index: 追加・削除する図形の元の描画位置。
        qsizetype index;
    };
    /// @brief 全差分を反映してから通知し、受信側へ中間状態を見せない。
    /// @param forward 変更後へ進むならtrue、変更前へ戻すならfalse。
    void apply(bool forward) {
        // byId: 差分の検索表。複数移動・削除でも図形数に比例した走査にする。
        QHash<QUuid,const Change*> byId;
        // inserts: 元の描画順で挿入する追加差分への参照。コマンド自身が所有する。
        QList<const Change*> inserts;
        // change: 更新・削除・追加の各差分。
        for (const auto &change : m_changes) {
            // value: 復元先。source: 復元元の値。
            const auto &value = forward ? change.after : change.before, &source = forward ? change.before : change.after;
            if (source) byId.insert(source->id,&change); else inserts.append(&change);
        }
        // remaining: 削除を除き、変更値を反映済みの残存図形。
        QList<ShapeData> remaining;
        // data: 現在の描画順に並ぶ図形。
        for (const auto &data : m_document->m_shapes) {
            // change: このUUIDに対する差分。なければ元の値を残す。
            const auto *change = byId.value(data.id,nullptr);
            if (!change) remaining.append(data);
            else {
                // value: この復元方向で存在する図形の値。
                const auto &value = forward ? change->after : change->before;
                if (value) remaining.append(*value);
            }
        }
        // a/b: 元の描画位置で昇順に比較する追加差分。
        std::sort(inserts.begin(),inserts.end(),[](const Change *a,const Change *b) { return a->index < b->index; });
        // next: 完全な復元後リスト。中間位置への反復挿入を避け、順番にマージする。
        QList<ShapeData> next; next.reserve(remaining.size()+inserts.size());
        // insertion: 次に復元する追加差分番号。existing: 次に出力する残存図形番号。
        qsizetype insertion=0, existing=0;
        // index: 復元後の描画順。
        for (qsizetype index=0; index<remaining.size()+inserts.size(); ++index) {
            if (insertion<inserts.size() && inserts[insertion]->index == index) {
                // change: 元の位置へ復元する図形の差分。
                const auto *change = inserts[insertion++]; next.append(*(forward ? change->after : change->before));
            } else next.append(remaining[existing++]);
        }
        emit m_document->aboutToChange(); m_document->m_shapes = next;
        // change: モデル全体の確定後に種類別通知を出す差分。
        for (const auto &change : m_changes) {
            // value: 復元後の値。source: 復元前の値。
            const auto &value = forward ? change.after : change.before, &source = forward ? change.before : change.after;
            if (!value) emit m_document->shapeRemoved(source->id);
            else if (!source) emit m_document->shapeAdded(value->id);
            else emit m_document->shapeChanged(value->id);
        }
    }
    // m_document: このコマンドを所有する文書。コマンド破棄後まで生存する。
    DrawingDocument *m_document;
    // m_changes: 変更図形だけの前後値と描画位置。
    QList<Change> m_changes;
};

/// @brief 文書内で共通に使うQt標準の履歴スタックを作る。
/// @param parent 文書を所有するQtの親。
DrawingDocument::DrawingDocument(QObject *parent) : QObject(parent),m_history(new QUndoStack(this))
{
    connect(m_history,&QUndoStack::indexChanged,this,&DrawingDocument::historyChanged);
}
/// @brief コマンドが文書メンバーより先に破棄されるようにする。
DrawingDocument::~DrawingDocument() { disconnect(m_history,nullptr,this,nullptr); delete m_history; }
/// @brief 戻せる確定操作があるかどうかを返す。
bool DrawingDocument::canUndo() const { return m_history->canUndo(); }
/// @brief やり直せる確定操作があるかどうかを返す。
bool DrawingDocument::canRedo() const { return m_history->canRedo(); }
/// @brief 通知中の入れ子Undoを拒否して1操作だけ復元する。
bool DrawingDocument::undo() {
    if (m_mutating || !canUndo()) return false;
    // guard: 復元通知中に別の変更・Undoを割り込ませないガード。
    QScopedValueRollback<bool> guard(m_mutating,true);
    emit historyAboutToRestore(); m_history->undo(); return true;
}
/// @brief 通知中の入れ子Redoを拒否して1操作だけ復元する。
bool DrawingDocument::redo() {
    if (m_mutating || !canRedo()) return false;
    // guard: 復元通知中に別の変更・Redoを割り込ませないガード。
    QScopedValueRollback<bool> guard(m_mutating,true);
    emit historyAboutToRestore(); m_history->redo(); return true;
}
/// @brief 有効な差分だけを履歴へ登録し、その場でredoを適用する。
/// @param next 検証済みの更新候補。
/// @param text 操作名。
bool DrawingDocument::commit(const QList<ShapeData> &next,const QString &text) {
    if (m_mutating) return false;
    // command: 履歴へ所有権を移す差分コマンド。
    auto *command = new DocumentEditCommand(this,next,text);
    if (command->empty()) { delete command; return true; }
    // guard: push時のredoと各通知を含めて再入を拒否する。
    QScopedValueRollback<bool> guard(m_mutating,true); m_history->push(command); return true;
}

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
    // next: 追加後の描画順を保持する一時候補。
    auto next = m_shapes; next.append(data);
    if (!commit(next,tr("図形を作成"))) return {};
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
    // next: 全件を検証した移動を1操作へまとめる候補。
    auto next = m_shapes;
    // data: 対象UUIDだけの配置を更新する図形。
    for (auto &data : next) if (unique.contains(data.id)) data.positionMm += delta;
    return commit(next,tr("図形を移動"));
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
    // next: 指定した図形だけを除いた変更候補。
    auto next = m_shapes;
    // data: 削除対象のUUIDを判定する各図形。
    next.erase(std::remove_if(next.begin(),next.end(),[&](const ShapeData &data) { return unique.contains(data.id); }),next.end());
    return commit(next,tr("図形を削除"));
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
    // shapes: 同じUUIDと描画位置を維持した変更候補。
    auto shapes = m_shapes;
    // data: 対象の形状だけを置き換える図形。
    for (auto &data : shapes) if (data.id == next.id) { data = next; break; }
    return commit(shapes,tr("図形を編集"));
}

/// @brief 幅・高さが正の実寸矩形を検証して置き換える。無変更・不正値では通知しない。
/// @param id 変更する四角形UUID。
/// @param boundsMm 呼び出し側が基準位置を計算済みの矩形（実寸mm）。
bool DrawingDocument::setRectangleBounds(const QUuid &id, const QRectF &boundsMm)
{
    if (m_mutating) return false;
    // original: 種類と現在の配置を確認する、変更前の図形データのコピー。
    const auto original = shape(id);
    if (!original || original->kind() != ShapeKind::Rectangle) return false;
    // next: 配置を実寸形状へ統合して検証する候補。通知中もUUIDを固定して保持する。
    ShapeData next = *original; next.geometryMm = boundsMm; next.positionMm = {};
    if (!next.isValid()) return false;
    // current: 配置移動量を反映した変更前の矩形（実寸mm）。
    const QRectF current = std::get<QRectF>(original->geometryMm).translated(original->positionMm);
    if (current.x() == boundsMm.x() && current.y() == boundsMm.y()
        && current.width() == boundsMm.width() && current.height() == boundsMm.height()) return true;
    // shapes: 同じUUIDと描画位置を維持した変更候補。
    auto shapes = m_shapes;
    // data: 対象の形状だけを置き換える図形。
    for (auto &data : shapes) if (data.id == next.id) { data = next; break; }
    return commit(shapes,tr("図形を編集"));
}

/// @brief 中心の実寸座標を変えずに、有限の正半径で円を更新する。
/// @param id 対象の円UUID。
/// @param radiusMm 新しい半径（実寸mm）。外接範囲が描画可能な座標内にあることも検証する。
bool DrawingDocument::setCircleRadius(const QUuid &id, double radiusMm)
{
    if (m_mutating) return false;
    // original: 種類・中心・現在の半径を確認する、変更前の図形データのコピー。
    const auto original = shape(id);
    if (!original || original->kind() != ShapeKind::Circle) return false;
    // circle: 現在のローカル中心・半径。配置移動量は中心へ加えて正本を更新する。
    const auto circle = std::get<CircleGeometry>(original->geometryMm);
    // next: 中心の実寸座標を保持した候補。配置移動量は0へ統合する。
    ShapeData next = *original;
    next.positionMm = {}; next.geometryMm = CircleGeometry{circle.centerMm + original->positionMm, radiusMm,
                                                          circle.startAngleDeg,circle.sweepAngleDeg};
    if (!next.isValid()) return false;
    if (circle.radiusMm == radiusMm) return true;
    // shapes: 同じUUIDと描画位置を維持した変更候補。
    auto shapes = m_shapes;
    // data: 対象の形状だけを置き換える図形。
    for (auto &data : shapes) if (data.id == next.id) { data = next; break; }
    return commit(shapes,tr("図形を編集"));
}

/// @brief 全属性を検証して更新し、通知中は候補値をコピーで保持して再入を拒否する。
/// @param candidate 同じUUID・種類で更新する候補形状。座標は実寸mm。
bool DrawingDocument::updateShape(const ShapeData &candidate)
{
    // next: 通知コールバックから独立させた更新候補。
    const ShapeData next = candidate;
    // original: 同じ図形種類とUUIDを確認する変更前の値。
    const auto original = shape(next.id);
    if (m_mutating || !original || original->kind() != next.kind() || !next.isValid()) return false;
    // shapes: 同じUUIDと描画位置を維持した変更候補。
    auto shapes = m_shapes;
    // data: 対象の形状だけを置き換える図形。
    for (auto &data : shapes) if (data.id == next.id) { data = next; break; }
    return commit(shapes,tr("図形を編集"));
}
