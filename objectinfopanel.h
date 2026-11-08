/**
 * @file objectinfopanel.h
 * @brief 選択モデルと図形文書から、現在の形状情報を表示し、単独選択した直線・四角形・円を実寸で編集する。
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
class RectangleAnchorSelector;

/// @brief 単独選択図形を表示し、直線・寸法の項目変更を文書へ渡す。参照モデル自体は所有しない。
class ObjectInfoPanel : public QWidget {
    Q_OBJECT
public:
    /// @brief 選択変更・文書変更へ接続し、未選択時を含む情報表示を作る。
    /// @param document 表示と形状編集の対象モデル。寿命は弱い参照で監視する。
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
    /// @brief 形状編集用の数値入力を作り、共通の確定・取消イベントを接続する。
    /// @param key 入力する項目名。寸法は正値、座標は座標範囲で検証する。
    /// @param parent 入力欄を所有するグループまたは行コンテナ。
    QLineEdit *createEditor(const QString &key, QWidget *parent);
    /// @brief 対象項目を数値入力として表示し、最新値を未編集状態で設定する。
    /// @param key 登録済みの編集項目名。
    /// @param value 表示する実寸mmまたは角度の値。
    void showEditor(const QString &key, double value);
    /// @brief 現在の単独選択を再確認して、指定された1項目だけを確定する。固定点は四角形の9点、円の中心、直線の始点を使う。
    /// @param key 更新を要求する形状項目の内部名。
    void commitField(const QString &key);
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
    // m_fieldRows: 項目の表示ラベルと編集欄を切り替える、形状グループ所有の行コンテナ。
    QHash<QString, QWidget*> m_fieldRows;
    // m_anchorSelector: 四角形のサイズ変更で固定する9点。形状グループが所有する。
    RectangleAnchorSelector *m_anchorSelector;
    // m_anchorLabel: 選択中の固定点名を示す、形状グループ所有のラベル。
    QLabel *m_anchorLabel;
    // m_lineGroup: 直線を単独選択したときだけ表示する編集グループ。スクロール内容が所有する。
    QGroupBox *m_lineGroup;
    // m_lineForm: 単位付き項目名と数値入力を並べる、直線グループ所有のレイアウト。
    QFormLayout *m_lineForm;
    // m_edits: 直線6項目と幅・高さ・半径の入力欄。各形状グループまたは行コンテナが所有する。
    QHash<QString, QLineEdit*> m_edits;
    // m_error: 不正入力・形状更新失敗の理由を表示する、パネル所有のラベル。
    QLabel *m_error;
    // m_editId: 現在の入力欄に表示した図形UUID。選択切り替え時は下書きとともに更新する。
    QUuid m_editId;
};
#endif
