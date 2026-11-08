/**
 * @file shapeselectionmodel.cpp
 * @brief 図形IDの選択変更を検証し、削除後の選択と変更通知を整合させる。
 */
#include "shapeselectionmodel.h"
#include "drawingdocument.h"

/// @brief 文書の削除と破棄を監視し、選択IDの寿命を文書に合わせる。
/// @param document 所有しない選択対象文書。
/// @param parent 選択モデルを所有するQtの親。
ShapeSelectionModel::ShapeSelectionModel(DrawingDocument *document, QObject *parent)
    : QObject(parent), m_document(document)
{
    if (!document) return;
    connect(document, &DrawingDocument::shapeRemoved, this, &ShapeSelectionModel::pruneSelection);
    // コールバック: 文書が破棄されたら弱い参照と残りの選択を解除する。
    connect(document, &QObject::destroyed, this, [this] { m_document = nullptr; clear(); });
}

/// @brief 文書の描画順を保った、有効な選択図形IDの一覧を返す。
QList<QUuid> ShapeSelectionModel::selectedIds() const
{
    // ids: 呼び出し元へ値で返す選択UUID一覧。
    QList<QUuid> ids;
    if (m_document) {
        // shape: 文書が保持する確定済み図形。表示順に走査する。
        for (const auto &shape : m_document->shapes())
            if (m_selectedIds.contains(shape.id)) ids.append(shape.id);
    }
    return ids;
}

/// @brief 指定UUIDが選択中で、現在の文書に存在するかを確認する。
/// @param id 確認する図形UUID。
bool ShapeSelectionModel::contains(const QUuid &id) const
{
    return m_selectedIds.contains(id) && m_document && m_document->shape(id).has_value();
}

/// @brief 単独選択の図形データをコピーして返す。選択状態を変更しない。
std::optional<ShapeData> ShapeSelectionModel::singleShape() const
{
    // ids: 描画順で取得した有効な選択UUID。
    const auto ids = selectedIds();
    return ids.size() == 1 ? m_document->shape(ids.first()) : std::nullopt;
}

/// @brief 全IDを検証して選択集合を置き換える。同じ集合では通知しない。
/// @param ids 新しく選択する図形UUID一覧。
bool ShapeSelectionModel::setSelectedIds(const QList<QUuid> &ids)
{
    // next: 全件が有効な場合だけ正本へ適用する選択集合の下書き。
    QSet<QUuid> next;
    // id: 呼び出し元が選択を要求した図形UUID。
    for (const auto &id : ids) {
        if (!m_document || !m_document->shape(id)) return false;
        next.insert(id);
    }
    if (next == m_selectedIds) return true;
    m_selectedIds = next; emit selectionChanged();
    return true;
}

/// @brief 指定した有効な図形だけを選ぶ。
/// @param id 単独選択する図形UUID。
bool ShapeSelectionModel::selectOnly(const QUuid &id) { return setSelectedIds({id}); }

/// @brief 1図形の選択を切り替え、他の選択図形を維持する。
/// @param id 追加または解除する図形UUID。
bool ShapeSelectionModel::toggle(const QUuid &id)
{
    if (!m_document || !m_document->shape(id)) return false;
    // next: 現在選択へ1件の追加・解除を適用する下書き。
    auto next = m_selectedIds;
    if (next.contains(id)) next.remove(id); else next.insert(id);
    return setSelectedIds(next.values());
}

/// @brief 有効な選択集合を空にし、状態が変わる場合だけ通知する。
void ShapeSelectionModel::clear() { setSelectedIds({}); }

/// @brief 一括削除済みの文書に残るIDだけを保持し、選択変更を1回だけ通知する。
void ShapeSelectionModel::pruneSelection() { setSelectedIds(selectedIds()); }
