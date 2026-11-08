/**
 * @file toolpalette.h
 * @brief 基本ツールの排他選択、アイコン表示、幅に応じた再配置を扱う。
 */

#ifndef TOOLPALETTE_H
#define TOOLPALETTE_H

#include "drawingtool.h"
#include <QWidget>
#include <QList>

class QAction;
class QActionGroup;
class QGridLayout;
class QScrollArea;
class QToolButton;
class QLabel;

/// @brief 基本ツールを排他的に選ぶパレット。ボタンやアクションはQtの親子関係で所有する。
class ToolPalette : public QWidget
{
    Q_OBJECT
public:
    /// @brief 基本ツールのアクションとボタンを作り、排他選択と再配置を接続する。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit ToolPalette(QWidget *parent = nullptr);
    /// @brief パレットで現在選択されているツールを返す。
    DrawingTool currentTool() const { return m_currentTool; }
    /// @brief 使用可能なツールだけを排他的に選び、変化した場合に通知する。
    /// @param tool 現在処理するツールの種類。
    bool setCurrentTool(DrawingTool tool);
    /// @brief 指定ツールに対応するアクションを返す。見つからない場合はnullptr。
    /// @param tool 現在処理するツールの種類。
    QAction *toolAction(DrawingTool tool) const;
    /// @brief ツールの表示名を現在の翻訳で返す。
    /// @param tool 現在処理するツールの種類。
    static QString toolName(DrawingTool tool);
    /// @brief ツールの初期モードの表示文字列を返す。
    /// @param tool 現在処理するツールの種類。
    static QString modeText(DrawingTool tool);
    /// @brief パレット下部の操作案内を更新する。
    /// @param text 表示または検証に使用する文字列。
    void setToolHint(const QString &text);

signals:
    /// @brief パレットで選択されたツールの変更を通知する。
    /// @param tool 現在処理するツールの種類。
    void toolChanged(DrawingTool tool);

protected:
    /// @brief スクロール領域のサイズ変更を検知して再配置を予約する。
    /// @param watched サイズ変更を監視するイベント送信元オブジェクト。
    /// @param event Qtから渡された入力または画面更新のイベント。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// @brief 再配置をイベントループへ1回だけ予約し、連続リサイズでの重複処理を防ぐ。
    void scheduleLayout();
    /// @brief スクロールバーの有無も考慮し、パレット幅に合う列数へボタンを並べ直す。
    void reflow();
    // m_scroll: パレットのボタン一覧を表示するスクロール領域。パレットが所有する。
    QScrollArea *m_scroll;
    // m_content: スクロール領域が所有する、ツールボタンの配置先ウィジェット。
    QWidget *m_content;
    // m_grid: ボタン配置用レイアウト。列数変更時に再生成し、親ウィジェットへ設定する。
    QGridLayout *m_grid = nullptr;
    // m_group: ツールの排他選択を管理するアクショングループ。パレットが所有する。
    QActionGroup *m_group;
    // m_buttons: コンテンツウィジェットが所有する、表示順のツールボタン参照。
    QList<QToolButton*> m_buttons;
    // m_currentTool: パレットで選択中のツール。
    DrawingTool m_currentTool = DrawingTool::Selection;
    // m_columns: 直前に適用したパレットの列数。同じ列数での再配置を避ける。
    int m_columns = 0;
    // m_layoutPending: イベントループへ再配置を予約済みであることを示す。
    bool m_layoutPending = false;
    // m_hint: パレット下部の操作案内ラベル。パレットが所有する。
    QLabel *m_hint;
};

#endif
