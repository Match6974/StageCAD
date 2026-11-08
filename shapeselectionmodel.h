/**
 * @file shapeselectionmodel.h
 * @brief 描画アイテムに依存せず、図形UUIDによる選択状態を管理する。
 */
#ifndef SHAPESELECTIONMODEL_H
#define SHAPESELECTIONMODEL_H

#include "shapedata.h"
#include <QObject>
#include <QPointer>
#include <QSet>
#include <optional>

class DrawingDocument;

/// @brief 文書内で有効な図形IDの選択集合を保持し、実際に変化したときだけ通知する。
/// 文書は所有しない。文書の図形削除・破棄に合わせて選択を整理する。
class ShapeSelectionModel : public QObject {
    Q_OBJECT
public:
    /// @brief 対象文書の削除・破棄通知へ接続して空の選択状態を作る。
    /// @param document 選択対象の図形文書。寿命はQPointerで監視する。
    /// @param parent この選択モデルを所有するQtの親。nullptrなら親を設定しない。
    explicit ShapeSelectionModel(DrawingDocument *document, QObject *parent = nullptr);
    /// @brief 選択中の有効なUUIDを文書の描画順で返す。
    QList<QUuid> selectedIds() const;
    /// @brief 選択中の有効な図形数を返す。
    int count() const { return selectedIds().size(); }
    /// @brief 指定IDが選択中かつ文書内で有効かを返す。
    /// @param id 確認する図形UUID。
    bool contains(const QUuid &id) const;
    /// @brief 単独選択の場合に図形データのコピーを返す。未選択・複数選択では空。
    std::optional<ShapeData> singleShape() const;
    /// @brief 選択集合を置き換える。不正IDが含まれる場合は全体を変更せずfalse。
    /// @param ids 選択するUUID一覧。重複・順序は選択集合に影響しない。
    bool setSelectedIds(const QList<QUuid> &ids);
    /// @brief 指定した1図形だけを選ぶ。不正IDなら選択を変更せずfalse。
    /// @param id 単独選択する図形UUID。
    bool selectOnly(const QUuid &id);
    /// @brief 1図形の追加選択・解除を切り替える。不正IDならfalse。
    /// @param id 切り替える図形UUID。
    bool toggle(const QUuid &id);
    /// @brief 全選択を解除する。もともと空なら通知しない。
    void clear();
signals:
    /// @brief 選択IDの集合が変わった後に通知する。表示やパネルは現在状態を取得する。
    void selectionChanged();
private:
    /// @brief 削除処理後の文書を参照し、存在しないIDを一括で選択から除く。
    void pruneSelection();
    // m_document: 所有しない文書への弱い参照。破棄時に空になる。
    QPointer<DrawingDocument> m_document;
    // m_selectedIds: 選択状態の正本。表示アイテムの選択フラグとは独立したUUID集合。
    QSet<QUuid> m_selectedIds;
};
#endif
