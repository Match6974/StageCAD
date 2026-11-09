/**
 * @file scaledialog.cpp
 * @brief 縮尺の候補選択と任意値入力を検証し、確定前の設定値を保持する。
 */

#include "scaledialog.h"
#include "drawingsettings.h"

#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

/// @brief 現在の縮尺を下書きとして取り込み、候補選択と数値入力のUIを作る。
/// @param settings 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
ScaleDialog::ScaleDialog(const DrawingSettings *settings, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("scaleDialog");
    setWindowTitle(tr("作業領域の縮尺"));
    // layout: 画面部品を縦方向に並べる、親ウィジェット所有のレイアウト。
    auto *layout = new QVBoxLayout(this);
    // description: 縮尺の意味を説明する、親ダイアログ所有のラベル。
    auto *description = new QLabel(tr("図形の実寸は変えず、用紙への換算倍率を設定します。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    // form: ラベルと入力欄を行ごとに並べるフォームレイアウト。
    auto *form = new QFormLayout;
    m_presets = new QComboBox(this);
    m_presets->setObjectName("scalePresets");
    // value: 縮尺1:Nの分母として扱う数値。
    for (const double value : {1.0, 10.0, 100.0})
        m_presets->addItem(QStringLiteral("1:%1").arg(value), value);
    m_presets->addItem(tr("任意の縮尺"));
    form->addRow(tr("メートル／工学系"), m_presets);

    m_custom = new QLineEdit(this);
    m_custom->setObjectName("scaleDenominatorInput");
    // locale: 小数点などの入力・表示形式に使うロケール。
    const QLocale locale;
    // Standard decimal notation and locale-aware decimal separator; no grouping.
    // inputLocale: 桁区切りを拒否するよう調整した入力専用ロケール。
    QLocale inputLocale = locale;
    inputLocale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    // validator: 入力範囲とロケール表記を制限する数値バリデータ。親入力欄が所有する。
    auto *validator = new QDoubleValidator(DrawingSettings::minimumScale,
        DrawingSettings::maximumScale, 12, m_custom);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(inputLocale);
    m_custom->setValidator(validator);
    // initialValue: 現在の縮尺から作る、末尾の不要な0を取り除いた初期入力文字列。
    QString initialValue = inputLocale.toString(settings->scaleDenominator(), 'f', 12);
    while (initialValue.endsWith(QLatin1Char('0'))) initialValue.chop(1);
    if (initialValue.endsWith(inputLocale.decimalPoint()))
        initialValue.chop(inputLocale.decimalPoint().size());
    m_custom->setText(initialValue);
    form->addRow(tr("縮尺 1 :"), m_custom);
    layout->addLayout(form);
    m_scaleText = new QCheckBox(tr("スケールテキスト"),this);
    m_scaleText->setObjectName("scaleTextCheckBox");
    m_scaleText->setToolTip(tr("ON: 文字の見た目も縮尺に比例して伸縮します。OFF: 用紙上の大きさを保ちます。フォントサイズの設定値は変更しません。"));
    layout->addWidget(m_scaleText);

    // preset: 現在の縮尺に対応する候補のインデックス。任意入力は末尾。
    int preset = m_presets->findData(settings->scaleDenominator());
    if (preset < 0) preset = m_presets->count() - 1;
    m_presets->setCurrentIndex(preset);
    m_custom->setEnabled(preset == m_presets->count() - 1);
    m_error = new QLabel(this);
    m_error->setObjectName("scaleInputError");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    // note: 設定の対象範囲を説明する、親ダイアログ所有のラベル。
    auto *note = new QLabel(tr("対象は現在の作業領域です。PDF参照表示の倍率は変更しません。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("scaleDialogButtons");
    m_ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // コールバック: 入力を再検証し、有効な縮尺の場合だけダイアログを確定する。
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        validateInput();
        if (m_ok->isEnabled()) accept();
    });
    // index: コンボボックスまたはタブで選択されたインデックス。
    // コールバック: 任意入力の有効・無効を切り替え、定型値を選んだ場合は入力欄へ反映する。
    connect(m_presets, &QComboBox::currentIndexChanged, this, [this](int index) {
        // custom: 任意の縮尺入力が選択されているかどうか。
        const bool custom = index == m_presets->count() - 1;
        m_custom->setEnabled(custom);
        if (!custom) m_custom->setText(m_presets->currentText().section(':', 1));
        validateInput();
    });
    connect(m_custom, &QLineEdit::textChanged, this, &ScaleDialog::validateInput);
    validateInput();
}

/// @brief ダイアログの下書き値を縮尺分母として読み取る。変換失敗時は0を返す。
double ScaleDialog::scaleDenominator() const
{
    // validator: 入力範囲とロケール表記を制限する数値バリデータ。親入力欄が所有する。
    const auto *validator = static_cast<const QDoubleValidator*>(m_custom->validator());
    // ok: 数値変換の成否、またはダイアログの確定ボタン。
    bool ok = false;
    // value: 縮尺1:Nの分母として扱う数値。
    const double value = validator->locale().toDouble(m_custom->text(), &ok);
    return ok ? value : 0.0;
}

/// @brief 確定時に適用する、文字の見た目の伸縮指定を返す。
bool ScaleDialog::scaleText() const { return m_scaleText->isChecked(); }

/// @brief 入力の有効性を確認し、確定ボタンとエラー表示を更新する。
void ScaleDialog::validateInput()
{
    // valid: 入力値や処理対象が有効であることを示す判定結果。
    const bool valid = m_custom->hasAcceptableInput()
        && DrawingSettings::isValidScale(scaleDenominator());
    m_ok->setEnabled(valid);
    m_error->setText(valid ? QString() : tr("0.000001〜10000の範囲で縮尺の分母を入力してください。"));
}
