/**
 * @file objectinfopanel.h
 * @brief 選択モデルと図形文書から、現在の形状情報を読み取り専用で表示する。
 */
#ifndef OBJECTINFOPANEL_H
#define OBJECTINFOPANEL_H

#include <QWidget>
#include <QPointer>
#include <QHash>

class DrawingDocument;
class ShapeSelectionModel;
class QLabel;
class QGroupBox;
class QFormLayout;

/// @brief 単独選択図形の実寸情報を表示するパネル。文書と選択モデルは所有・変更しない。
class ObjectInfoPanel : public QWidget {
    Q_OBJECT
public:
    /// @brief 選択変更・文書変更へ接続し、未選択時を含む情報表示を作る。
    /// @param document 読み取り対象の図形モデル。寿命は弱い参照で監視する。
    /// @param selection 表示する図形を指定する選択モデル。所有権は呼び出し側にある。
    /// @param parent パネルと子ウィジェットを所有するQtの親。
    explicit ObjectInfoPanel(DrawingDocument *document, ShapeSelectionModel *selection, QWidget *parent = nullptr);
    /// @brief 子ウィジェットの破棄より先に、モデルからの通知を切り離す。
    ~ObjectInfoPanel() override;
private:
    /// @brief 選択状態と現在の実寸形状を読み取り、該当項目だけ表示する。
    void refresh();
    /// @brief 形状項目を作成し、後で値と表示可否を変更できるよう登録する。
    /// @param key 項目を識別する内部名。図形UUIDとは別のUI識別子。
    /// @param title 項目の表示名。
    void addField(const QString &key, const QString &title);
    /// @brief 指定項目へ値を設定し、その行を表示する。
    /// @param key 登録済みの項目名。
    /// @param value 単位を含む読み取り専用の表示文字列。
    void showField(const QString &key, const QString &value);
    // m_document: 所有しない確定図形モデル。破棄時に空になる。
    QPointer<DrawingDocument> m_document;
    // m_selection: 所有しない選択モデル。表示アイテムのポインタは参照しない。
    QPointer<ShapeSelectionModel> m_selection;
    // m_summary: 未選択・単独・複数選択の状態を表示する、パネル所有のラベル。
    QLabel *m_summary;
    // m_shapeGroup: 形状の情報行をまとめるグループ。スクロール内容が所有する。
    QGroupBox *m_shapeGroup;
    // m_form: 項目名と値を並べる、形状グループ所有のレイアウト。
    QFormLayout *m_form;
    // m_values: 項目名から値ラベルへの参照表。各ラベルは形状グループが所有する。
    QHash<QString, QLabel*> m_values;
};
#endif
