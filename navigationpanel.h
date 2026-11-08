/**
 * @file navigationpanel.h
 * @brief デザインレイヤーの一覧・追加・名前変更・表示・作成先切替を提供する。
 */
#ifndef NAVIGATIONPANEL_H
#define NAVIGATIONPANEL_H
#include <QWidget>
#include <QPointer>
class DrawingDocument;
class DrawingSettings;
class QTreeWidget;
class QLabel;

/// @brief 文書のレイヤーを表示・操作する。文書と設定は所有せず、Qtの親がパネルを所有する。
class NavigationPanel : public QWidget {
    Q_OBJECT
public:
    /// @brief レイヤー操作と文書通知を接続する。
    /// @param document 所有しないレイヤー・図形文書。
    /// @param settings 所有しない現在の縮尺設定。
    /// @param parent パネルの所有者。
    NavigationPanel(DrawingDocument *document,DrawingSettings *settings,QWidget *parent=nullptr);
    /// @brief 子のインライン編集が破棄中のパネルへ通知しないよう接続を解除する。
    ~NavigationPanel() override;
private:
    /// @brief UUIDで行を同期する。名前編集中の行を不要に作り直さない。
    void refresh();
    // m_document: 文書の寿命を監視する弱い参照。
    QPointer<DrawingDocument> m_document;
    // m_settings: 作業領域の縮尺変更通知を監視する表示設定への弱い参照。
    QPointer<DrawingSettings> m_settings;
    // m_list: 4列のレイヤー一覧。パネルが所有する。
    QTreeWidget *m_list;
    // m_error: 名前の重複等の入力エラーを表示するパネル所有のラベル。
    QLabel *m_error;
};
#endif
