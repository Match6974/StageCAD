#include "scaledialog.h"
#include "drawingsettings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

ScaleDialog::ScaleDialog(const DrawingSettings *settings, QWidget *parent)
    : QDialog(parent)
{
    setObjectName("scaleDialog");
    setWindowTitle(tr("作業領域の縮尺"));
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(tr("図形の実寸は変えず、用紙への換算倍率を設定します。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *form = new QFormLayout;
    m_presets = new QComboBox(this);
    m_presets->setObjectName("scalePresets");
    for (const double value : {1.0, 10.0, 100.0})
        m_presets->addItem(QStringLiteral("1:%1").arg(value), value);
    m_presets->addItem(tr("任意の縮尺"));
    form->addRow(tr("メートル／工学系"), m_presets);

    m_custom = new QLineEdit(this);
    m_custom->setObjectName("scaleDenominatorInput");
    const QLocale locale;
    // Standard decimal notation and locale-aware decimal separator; no grouping.
    QLocale inputLocale = locale;
    inputLocale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    auto *validator = new QDoubleValidator(DrawingSettings::minimumScale,
        DrawingSettings::maximumScale, 12, m_custom);
    validator->setNotation(QDoubleValidator::StandardNotation);
    validator->setLocale(inputLocale);
    m_custom->setValidator(validator);
    QString initialValue = inputLocale.toString(settings->scaleDenominator(), 'f', 12);
    while (initialValue.endsWith(QLatin1Char('0'))) initialValue.chop(1);
    if (initialValue.endsWith(inputLocale.decimalPoint()))
        initialValue.chop(inputLocale.decimalPoint().size());
    m_custom->setText(initialValue);
    form->addRow(tr("縮尺 1 :"), m_custom);
    layout->addLayout(form);

    int preset = m_presets->findData(settings->scaleDenominator());
    if (preset < 0) preset = m_presets->count() - 1;
    m_presets->setCurrentIndex(preset);
    m_custom->setEnabled(preset == m_presets->count() - 1);
    m_error = new QLabel(this);
    m_error->setObjectName("scaleInputError");
    m_error->setWordWrap(true);
    layout->addWidget(m_error);
    auto *note = new QLabel(tr("対象は現在の作業領域です。PDF参照表示の倍率は変更しません。"), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("scaleDialogButtons");
    m_ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        validateInput();
        if (m_ok->isEnabled()) accept();
    });
    connect(m_presets, &QComboBox::currentIndexChanged, this, [this](int index) {
        const bool custom = index == m_presets->count() - 1;
        m_custom->setEnabled(custom);
        if (!custom) m_custom->setText(m_presets->currentText().section(':', 1));
        validateInput();
    });
    connect(m_custom, &QLineEdit::textChanged, this, &ScaleDialog::validateInput);
    validateInput();
}

double ScaleDialog::scaleDenominator() const
{
    const auto *validator = static_cast<const QDoubleValidator*>(m_custom->validator());
    bool ok = false;
    const double value = validator->locale().toDouble(m_custom->text(), &ok);
    return ok ? value : 0.0;
}

void ScaleDialog::validateInput()
{
    const bool valid = m_custom->hasAcceptableInput()
        && DrawingSettings::isValidScale(scaleDenominator());
    m_ok->setEnabled(valid);
    m_error->setText(valid ? QString() : tr("0.000001〜10000の範囲で縮尺の分母を入力してください。"));
}
