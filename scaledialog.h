/**
 * @file scaledialog.h
 * @brief 縮尺の候補選択と任意値入力を検証し、確定前の設定値を保持する。
 */

#ifndef SCALEDIALOG_H
#define SCALEDIALOG_H

#include <QDialog>

class DrawingSettings;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;

// Edits a draft value; the drawing changes only after a valid OK.
/// @brief 縮尺の入力値を検証するダイアログ。確定するまで設定モデルを変更しない。
class ScaleDialog : public QDialog
{
    Q_OBJECT
public:
    /// @brief 現在の縮尺を下書きとして取り込み、候補選択と数値入力のUIを作る。
    /// @param settings 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit ScaleDialog(const DrawingSettings *settings, QWidget *parent = nullptr);
    /// @brief ダイアログの下書き値を縮尺分母として読み取る。変換失敗時は0を返す。
    double scaleDenominator() const;

private:
    /// @brief 入力の有効性を確認し、確定ボタンとエラー表示を更新する。
    void validateInput();
    // m_presets: 定型縮尺と任意入力を選ぶコンボボックス。縮尺ダイアログが所有する。
    QComboBox *m_presets;
    // m_custom: 任意縮尺の分母を入力する欄。縮尺ダイアログが所有する。
    QLineEdit *m_custom;
    // m_error: 入力または適用エラーを表示するラベル。所属ダイアログが所有する。
    QLabel *m_error;
    // m_ok: 有効な入力が揃ったときだけ押せる確定ボタン。ボタンボックスが所有する。
    QPushButton *m_ok;
};

#endif
