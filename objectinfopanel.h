/**
 * @file objectinfopanel.h
 * @brief 選択モデルと図形文書から、現在の形状情報を表示し、単独選択した直線を実寸で編集する。
 */
#ifndef OBJECTINFOPANEL_H
#define OBJECTINFOPANEL_H

#include <QWidget>
#include <QPointer>
#include <QHash>
#include <QUuid>

class DrawingDocument;
class ShapeSelectionModel;
class QLabel;
class QGroupBox;
class QFormLayout;
class QLineEdit;

/// @brief 単独選択図形を表示し、直線の項目変更を文書へ渡す。参照モデル自体は所有しない。
class ObjectInfoPanel : public QWidget {
    Q_OBJECT
public:
    /// @brief 選択変更・文書変更へ接続し、未選択時を含む情報表示を作る。
    /// @param document 表示と直線編集の対象モデル。寿命は弱い参照で監視する。
    /// @param selection 表示する図形を指定する選択モデル。所有権は呼び出し側にある。
    /// @param parent パネルと子ウィジェットを所有するQtの親。
    explicit ObjectInfoPanel(DrawingDocument *document, ShapeSelectionModel *selection, QWidget *parent = nullptr);
    /// @brief 子ウィジェットの破棄より先に、モデルからの通知を切り離す。
    ~ObjectInfoPanel() override;
protected:
    /// @brief Enterで1項目を確定し、Escで未確定入力を戻す。
    /// @param watched 編集用入力欄。所有権はパネル内のグループにある。
    /// @param event Qtから渡されるキー等の入力イベント。
    bool eventFilter(QObject *watched, QEvent *event) override;
    /// @brief 操作禁止へ切り替わる際に未確定入力を破棄する。
    /// @param event パネルの有効・無効等の変更イベント。
    void changeEvent(QEvent *event) override;
private:
    /// @brief 直線の入力欄を作り、Enter・Escの監視とロケール対応の検証を設定する。
    /// @param key 編集する直線項目の内部名。
    /// @param title 単位を含む表示名。
    void addLineEditor(const QString &key, const QString &title);
    /// @brief 現在の単独選択を再確認して、指定された1項目だけを確定する。
    /// @param key 更新を要求する直線項目の内部名。
    void commitLineField(const QString &key);
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
    // m_lineGroup: 直線を単独選択したときだけ表示する編集グループ。スクロール内容が所有する。
    QGroupBox *m_lineGroup;
    // m_lineForm: 単位付き項目名と数値入力を並べる、直線グループ所有のレイアウト。
    QFormLayout *m_lineForm;
    // m_lineEdits: 直線の6項目の入力欄。所有権は直線グループにある。
    QHash<QString, QLineEdit*> m_lineEdits;
    // m_error: 不正入力・形状更新失敗の理由を表示する、パネル所有のラベル。
    QLabel *m_error;
    // m_lineId: 現在の入力欄に表示した直線UUID。選択切り替え時は下書きとともに更新する。
    QUuid m_lineId;
};
#endif
