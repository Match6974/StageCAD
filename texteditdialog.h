/**
 * @file texteditdialog.h
 * @brief 横書き文字の下書き入力と、ポイント・mm・インチのサイズ指定を提供する。
 */
#ifndef TEXTEDITDIALOG_H
#define TEXTEDITDIALOG_H
#include <QDialog>
class QPlainTextEdit;
class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;

/// @brief 確定前の文字列・サイズを編集するダイアログ。文書モデルは所有・変更しない。
class TextEditDialog : public QDialog {
    Q_OBJECT
public:
    /// @brief 編集する下書きと固定フォント名を表示する。
    /// @param text 初期文字列。改行を含められる。
    /// @param pointSize 初期文字サイズ（pt）。
    /// @param family 試作版で固定するフォント名。
    /// @param parent ダイアログの所有者。
    TextEditDialog(const QString &text,double pointSize,const QString &family,QWidget *parent=nullptr);
    /// @brief 入力中のプレーンテキストを返す。HTMLとして扱わない。
    QString text() const;
    /// @brief 現在の単位を換算した文字サイズ（pt）を返す。不正値は0を返す。
    double pointSize() const;
private:
    /// @brief 文字の有無と有限な正のサイズを検証し、確定ボタンを制御する。
    void validate();
    // m_text: 日本語IMEと改行を受け付ける、ダイアログ所有の入力欄。
    QPlainTextEdit *m_text;
    // m_size: 任意値を現在の単位で指定する、ダイアログ所有のサイズ欄。
    QLineEdit *m_size;
    // m_unit: pt・mm・インチの換算倍率を保持する単位選択。
    QComboBox *m_unit;
    // m_ok: 有効な下書きだけを確定するボタン。
    QPushButton *m_ok;
    // m_error: 不正入力の理由を説明するラベル。
    QLabel *m_error;
    // m_unitFactor: 単位変更前の「1単位あたりpt」。丸めた表示値からの再変換を避けるため保持する。
    double m_unitFactor = 1;
    // m_canonicalSize: 単位切替で丸め直さない、現在の正確なサイズ（pt）。
    double m_canonicalSize = 12;
};
#endif
