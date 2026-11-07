#include "pdfcalibrationdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>
#include <cmath>

namespace {
QLineEdit *distanceInput(QWidget *parent, const char *name)
{
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(QString::fromLatin1(name));
    auto *validator = new QDoubleValidator(0, 10000000, 12, edit);
    QLocale locale;
    locale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    validator->setLocale(locale);
    validator->setNotation(QDoubleValidator::StandardNotation);
    edit->setValidator(validator);
    return edit;
}

bool positiveDistance(double value) { return std::isfinite(value) && value > 0 && value <= 10000000; }
}

PdfCalibrationDialog::PdfCalibrationDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("pdfCalibrationDialog");
    setWindowTitle(tr("PDFの伸縮"));
    setModal(false);
    auto *layout = new QVBoxLayout(this);
    auto *method = new QRadioButton(tr("距離で設定（縦横比固定）"), this);
    method->setChecked(true);
    layout->addWidget(method);
    auto *help = new QLabel(tr("PDF上の現在の距離を測定し、正しい実寸を入力してください。"
                              "測定した1点目を固定してPDF全体を伸縮します。"), this);
    help->setWordWrap(true); layout->addWidget(help);
    auto *form = new QFormLayout;
    auto *currentRow = new QHBoxLayout;
    m_current = distanceInput(this, "pdfCurrentDistance");
    currentRow->addWidget(m_current);
    currentRow->addWidget(new QLabel(tr("mm"), this));
    auto *measureCurrent = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight), tr("2点から測定"), this);
    measureCurrent->setObjectName("measurePdfCurrentButton");
    currentRow->addWidget(measureCurrent);
    form->addRow(tr("現在の距離"), currentRow);
    auto *targetRow = new QHBoxLayout;
    m_target = distanceInput(this, "pdfTargetDistance");
    targetRow->addWidget(m_target);
    m_unit = new QComboBox(this); m_unit->setObjectName("pdfTargetUnit");
    m_unit->addItem(tr("mm"), 1.0); m_unit->addItem(tr("cm"), 10.0);
    targetRow->addWidget(m_unit);
    auto *measureTarget = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight), tr("2点から取得"), this);
    measureTarget->setObjectName("measurePdfTargetButton");
    targetRow->addWidget(measureTarget);
    form->addRow(tr("新しい距離"), targetRow);
    layout->addLayout(form);
    m_anchorLabel = new QLabel(tr("固定点: 現在の距離を測定した1点目"), this);
    layout->addWidget(m_anchorLabel);
    m_factorLabel = new QLabel(this); m_factorLabel->setObjectName("pdfCalibrationFactor");
    layout->addWidget(m_factorLabel);
    m_error = new QLabel(this); m_error->setObjectName("pdfCalibrationError");
    m_error->setWordWrap(true); layout->addWidget(m_error);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("pdfCalibrationButtons");
    m_ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(measureCurrent, &QPushButton::clicked, this, &PdfCalibrationDialog::measureCurrentRequested);
    connect(measureTarget, &QPushButton::clicked, this, &PdfCalibrationDialog::measureTargetRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        validateInput();
        if (m_ok->isEnabled()) emit applyRequested();
    });
    connect(m_current, &QLineEdit::textChanged, this, [this] {
        m_measuredCurrent.reset(); validateInput();
    });
    connect(m_target, &QLineEdit::textChanged, this, [this] {
        m_measuredTarget.reset(); validateInput();
    });
    connect(m_unit, &QComboBox::currentIndexChanged, this, [this] {
        const double mm = targetDistanceMm();
        m_unitFactor = m_unit->currentData().toDouble();
        auto *validator = static_cast<QDoubleValidator*>(const_cast<QValidator*>(m_target->validator()));
        validator->setTop(10000000 / m_unitFactor);
        if (positiveDistance(mm)) {
            QSignalBlocker block(m_target);
            m_target->setText(format(m_target, mm / m_unitFactor));
            m_measuredTarget = mm;
        }
        validateInput();
    });
    validateInput();
}

double PdfCalibrationDialog::parse(const QLineEdit *edit)
{
    bool ok = false;
    const auto *validator = static_cast<const QDoubleValidator*>(edit->validator());
    const double value = validator->locale().toDouble(edit->text(), &ok);
    return ok ? value : 0;
}

QString PdfCalibrationDialog::format(const QLineEdit *edit, double value)
{
    const QLocale locale = static_cast<const QDoubleValidator*>(edit->validator())->locale();
    QString text = locale.toString(value, 'f', 12);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(locale.decimalPoint())) text.chop(locale.decimalPoint().size());
    return text;
}

double PdfCalibrationDialog::currentDistanceMm() const
{
    return m_measuredCurrent.value_or(parse(m_current));
}

double PdfCalibrationDialog::targetDistanceMm() const
{
    return m_measuredTarget.value_or(parse(m_target) * m_unitFactor);
}

void PdfCalibrationDialog::setCurrentMeasurement(QPointF first, QPointF second)
{
    const double distance = QLineF(first, second).length();
    if (!positiveDistance(distance)) {
        m_fixedPoint.reset(); m_measuredCurrent.reset(); m_current->clear();
        m_anchorLabel->setText(tr("固定点: 現在の距離を測定した1点目"));
        validateInput();
        setApplyError(tr("現在の距離は0より大きく、10000000mm以下で指定してください。"));
        return;
    }
    {
        QSignalBlocker block(m_current);
        m_current->setText(format(m_current, distance));
    }
    m_measuredCurrent = distance;
    m_fixedPoint = first;
    m_anchorLabel->setText(tr("固定点（1点目）: X %1 mm / Y %2 mm")
        .arg(first.x(), 0, 'f', 2).arg(first.y(), 0, 'f', 2));
    validateInput();
}

void PdfCalibrationDialog::setTargetMeasurement(QPointF first, QPointF second)
{
    const double distance = QLineF(first, second).length();
    if (!positiveDistance(distance)) {
        m_measuredTarget.reset(); m_target->clear(); validateInput();
        setApplyError(tr("新しい距離は0より大きく、10000000mm以下で指定してください。"));
        return;
    }
    {
        QSignalBlocker block(m_target);
        m_target->setText(format(m_target, distance / m_unitFactor));
    }
    m_measuredTarget = distance;
    validateInput();
}

void PdfCalibrationDialog::setApplyError(const QString &message) { m_error->setText(message); }

void PdfCalibrationDialog::validateInput()
{
    const bool valid = m_fixedPoint.has_value() && m_current->hasAcceptableInput()
        && m_target->hasAcceptableInput() && positiveDistance(currentDistanceMm())
        && positiveDistance(targetDistanceMm());
    m_ok->setEnabled(valid);
    m_error->setText(valid ? QString() : tr("PDF上の現在距離を2点で測定し、新しい距離に正の数値を入力してください。"));
    m_factorLabel->setText(valid ? tr("補正倍率: %1倍（縦横同比率）")
        .arg(targetDistanceMm() / currentDistanceMm(), 0, 'g', 12) : tr("補正倍率: —"));
}
