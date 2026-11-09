/**
 * @file texteditdialog.cpp
 * @brief 文字とサイズの下書きを検証し、単位切替で値の精度を保つ。
 */
#include "texteditdialog.h"
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDoubleValidator>
#include <QSignalBlocker>
#include <QLocale>
#include <limits>
#include <cmath>

/// @brief 文字を文書へ適用せずに編集し、Enterの改行とダイアログ確定を分離する。
/// @param text 初期のプレーンテキスト。
/// @param pointSize 文字サイズ（pt）。
/// @param family 試作版で固定するフォント名。
/// @param parent Qtの所有者。
TextEditDialog::TextEditDialog(const QString &text,double pointSize,const QString &family,QWidget *parent)
    : QDialog(parent),m_canonicalSize(pointSize)
{
    setObjectName("textEditDialog"); setWindowTitle(tr("文字の入力・編集")); resize(480,340);
    // layout: 文字入力・サイズ・確定ボタンをまとめる、ダイアログ所有のレイアウト。
    auto *layout = new QVBoxLayout(this);
    // form: フォント名とサイズを表示するレイアウト。
    auto *form = new QFormLayout; layout->addLayout(form);
    form->addRow(tr("フォント（固定）"),new QLabel(family,this));
    // sizeRow: 任意サイズ・単位・定型候補を横に並べるコンテナ。
    auto *sizeRow = new QWidget(this);
    // sizeLayout: 単位と値の関係を示すコンテナ所有のレイアウト。
    auto *sizeLayout = new QHBoxLayout(sizeRow); sizeLayout->setContentsMargins(0,0,0,0);
    m_size = new QLineEdit(this); m_size->setObjectName("textSizeInput");
    // locale: 入力時は桁区切りを拒否し、OSに合う小数点記号だけを受け付ける。
    QLocale locale; locale.setNumberOptions(QLocale::RejectGroupSeparator|QLocale::OmitGroupSeparator);
    // validator: 有限の正値を確定時に検証する、入力欄所有の通常表記検証器。
    auto *validator = new QDoubleValidator(0,std::numeric_limits<double>::max(),6,m_size);
    validator->setNotation(QDoubleValidator::StandardNotation); validator->setLocale(locale); m_size->setValidator(validator);
    m_size->setText(locale.toString(pointSize,'f',6)); sizeLayout->addWidget(m_size);
    m_unit = new QComboBox(this); m_unit->setObjectName("textSizeUnit");
    m_unit->addItem(tr("pt"),1.0); m_unit->addItem(tr("mm"),72.0/25.4); m_unit->addItem(tr("インチ"),72.0); sizeLayout->addWidget(m_unit);
    // presets: CSVの定型サイズをポイント単位で選択するコンボボックス。
    auto *presets = new QComboBox(this); presets->setObjectName("textSizePresets"); presets->addItem(tr("任意サイズ"));
    // value: CSVに記載された文字サイズの定型値（pt）。
    for (const int value : {4,6,7,8,9,10,11,12,14,18,20,24,28,36,48,72,96,144}) presets->addItem(tr("%1 pt").arg(value),value);
    sizeLayout->addWidget(presets); form->addRow(tr("文字サイズ"),sizeRow);
    m_text = new QPlainTextEdit(this); m_text->setObjectName("textContentInput"); m_text->setPlainText(text);
    m_text->setPlaceholderText(tr("文字を入力してください（改行できます）")); layout->addWidget(m_text);
    m_error = new QLabel(this); m_error->setObjectName("textInputError"); m_error->setWordWrap(true); layout->addWidget(m_error);
    // buttons: OKは下書き確定、取消・Escは下書き破棄として扱う標準ボタン。
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,this);
    buttons->setObjectName("textDialogButtons"); m_ok = buttons->button(QDialogButtonBox::Ok);
    // button: Enterを文字入力から奪わないよう、自動デフォルトを無効にする確定・取消ボタン。
    for (auto *button : buttons->findChildren<QPushButton*>()) { button->setAutoDefault(false); button->setDefault(false); }
    layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&QDialog::accept);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(m_text,&QPlainTextEdit::textChanged,this,&TextEditDialog::validate);
    // コールバック: ユーザーがサイズを変更した場合だけ正確なpt値を更新する。
    connect(m_size,&QLineEdit::textChanged,this,[this] {
        // valid: 現在の単位で入力を数値変換できたかどうか。
        bool valid = false;
        // value: 検証器と同じロケールで換算する候補サイズ（pt）。
        const double value = m_size->findChild<QDoubleValidator*>()->locale().toDouble(m_size->text(),&valid)*m_unitFactor;
        m_canonicalSize = valid && std::isfinite(value) && value>0 ? value : 0; validate();
    });
    // コールバック: 単位表示だけを切り替え、内部のpt値は変更しない。
    connect(m_unit,&QComboBox::currentIndexChanged,this,[this] {
        m_unitFactor = m_unit->currentData().toDouble();
        // blocker: 表示の換算がユーザー編集として二重適用されないようにする。
        QSignalBlocker blocker(m_size);
        // locale: 桁区切りのない小数点表記に揃える表示用ロケール。
        QLocale locale; locale.setNumberOptions(QLocale::OmitGroupSeparator);
        m_size->setText(locale.toString(m_canonicalSize/m_unitFactor,'f',6)); validate();
    });
    // index: 選択した定型サイズの番号。0は任意指定なので値を変えない。
    // コールバック: 定型ptを現在の単位へ換算して入力する。
    connect(presets,&QComboBox::activated,this,[this,presets](int index) {
        if (index <= 0) return;
        m_canonicalSize = presets->itemData(index).toDouble();
        // blocker: 定型値を表示桁へ丸めても、正確なpt値を書き換えない。
        QSignalBlocker blocker(m_size);
        // locale: OSの小数点を使い、桁区切りを出力しないロケール。
        QLocale locale; locale.setNumberOptions(QLocale::OmitGroupSeparator);
        m_size->setText(locale.toString(m_canonicalSize/m_unitFactor,'f',6)); validate();
    });
    validate(); m_text->setFocus();
}
/// @brief HTMLではなく、入力された改行付き文字列を返す。
QString TextEditDialog::text() const { return m_text->toPlainText(); }
/// @brief 有効なサイズだけをptへ換算する。非有限・0以下では0を返す。
double TextEditDialog::pointSize() const {
    return m_size->hasAcceptableInput() && std::isfinite(m_canonicalSize) && m_canonicalSize>0 ? m_canonicalSize : 0;
}
/// @brief 空白だけの文字列と不正なサイズを確定させない。
void TextEditDialog::validate() {
    // valid: プレーンテキストとサイズがどちらも有効かどうか。
    const bool valid = !text().trimmed().isEmpty() && pointSize()>0;
    m_ok->setEnabled(valid); m_error->setText(valid ? QString() : tr("文字を入力し、サイズを0より大きい有限の数値で指定してください。"));
}
