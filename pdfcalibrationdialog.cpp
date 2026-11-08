/**
 * @file pdfcalibrationdialog.cpp
 * @brief PDFの距離補正に使う測定値・単位・固定点を入力、検証する。
 */

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
/// @brief 有限の正距離を入力するための、ロケール対応の入力欄を作る。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
/// @param name 画面部品の識別名または表示名。
QLineEdit *distanceInput(QWidget *parent, const char *name)
{
    // edit: 対象のテキスト入力欄。所有権は所属するダイアログにある。
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(QString::fromLatin1(name));
    // validator: 入力範囲とロケール表記を制限する数値バリデータ。親入力欄が所有する。
    auto *validator = new QDoubleValidator(0, 10000000, 12, edit);
    // locale: 小数点などの入力・表示形式に使うロケール。
    QLocale locale;
    locale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    validator->setLocale(locale);
    validator->setNotation(QDoubleValidator::StandardNotation);
    edit->setValidator(validator);
    return edit;
}

/// @brief 距離が有限で、0より大きく10000000mm以下かを確認する。
/// @param value 有効性を調べる実寸距離（mm）。
bool positiveDistance(double value) { return std::isfinite(value) && value > 0 && value <= 10000000; }
}

/// @brief 距離・単位・固定点の補正UIを作り、入力検証と要求通知を接続する。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
PdfCalibrationDialog::PdfCalibrationDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName("pdfCalibrationDialog");
    setWindowTitle(tr("PDFの伸縮"));
    setModal(false);
    // layout: 画面部品を縦方向に並べる、親ウィジェット所有のレイアウト。
    auto *layout = new QVBoxLayout(this);
    // method: 縦横同比率の距離補正を示す、親ダイアログ所有のラジオボタン。
    auto *method = new QRadioButton(tr("距離で設定（縦横比固定）"), this);
    method->setChecked(true);
    layout->addWidget(method);
    // help: 距離測定と固定点を案内する、親ダイアログ所有のラベル。
    auto *help = new QLabel(tr("PDF上の現在の距離を測定し、正しい実寸を入力してください。"
                              "測定した1点目を固定してPDF全体を伸縮します。"), this);
    help->setWordWrap(true); layout->addWidget(help);
    // form: ラベルと入力欄を行ごとに並べるフォームレイアウト。
    auto *form = new QFormLayout;
    // currentRow: 現在距離と測定ボタンを横に並べるレイアウト。
    auto *currentRow = new QHBoxLayout;
    m_current = distanceInput(this, "pdfCurrentDistance");
    currentRow->addWidget(m_current);
    currentRow->addWidget(new QLabel(tr("mm"), this));
    // measureCurrent: 現在距離の2点測定を要求する、親ダイアログ所有のボタン。
    auto *measureCurrent = new QPushButton(style()->standardIcon(QStyle::SP_ArrowRight), tr("2点から測定"), this);
    measureCurrent->setObjectName("measurePdfCurrentButton");
    currentRow->addWidget(measureCurrent);
    form->addRow(tr("現在の距離"), currentRow);
    // targetRow: 新しい距離、単位、取得ボタンを横に並べるレイアウト。
    auto *targetRow = new QHBoxLayout;
    m_target = distanceInput(this, "pdfTargetDistance");
    targetRow->addWidget(m_target);
    m_unit = new QComboBox(this); m_unit->setObjectName("pdfTargetUnit");
    m_unit->addItem(tr("mm"), 1.0); m_unit->addItem(tr("cm"), 10.0);
    targetRow->addWidget(m_unit);
    // measureTarget: 新しい距離の2点取得を要求する、親ダイアログ所有のボタン。
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
    // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName("pdfCalibrationButtons");
    m_ok = buttons->button(QDialogButtonBox::Ok);
    layout->addWidget(buttons);
    connect(measureCurrent, &QPushButton::clicked, this, &PdfCalibrationDialog::measureCurrentRequested);
    connect(measureTarget, &QPushButton::clicked, this, &PdfCalibrationDialog::measureTargetRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    // コールバック: 再検証して有効な場合だけ、呼び出し元へ補正の適用を要求する。
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        validateInput();
        if (m_ok->isEnabled()) emit applyRequested();
    });
    // コールバック: 現在距離の手入力では測定時の精密値を破棄し、入力値を再検証する。
    connect(m_current, &QLineEdit::textChanged, this, [this] {
        m_measuredCurrent.reset(); validateInput();
    });
    // コールバック: 新しい距離の手入力では測定時の精密値を破棄し、入力値を再検証する。
    connect(m_target, &QLineEdit::textChanged, this, [this] {
        m_measuredTarget.reset(); validateInput();
    });
    // コールバック: 実寸mmの値を保ったまま単位表示と入力上限を変更する。
    connect(m_unit, &QComboBox::currentIndexChanged, this, [this] {
        // mm: 単位切り替え前の新しい距離（実寸mm）。
        const double mm = targetDistanceMm();
        m_unitFactor = m_unit->currentData().toDouble();
        // validator: 入力範囲とロケール表記を制限する数値バリデータ。親入力欄が所有する。
        auto *validator = static_cast<QDoubleValidator*>(const_cast<QValidator*>(m_target->validator()));
        validator->setTop(10000000 / m_unitFactor);
        if (positiveDistance(mm)) {
            // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
            QSignalBlocker block(m_target);
            m_target->setText(format(m_target, mm / m_unitFactor));
            m_measuredTarget = mm;
        }
        validateInput();
    });
    validateInput();
}

/// @brief 入力欄のロケールで数値を読み取る。変換できない場合は0を返す。
/// @param edit 対象のテキスト入力欄。所有権は所属するダイアログにある。
double PdfCalibrationDialog::parse(const QLineEdit *edit)
{
    // ok: 数値変換の成否、またはダイアログの確定ボタン。
    bool ok = false;
    // validator: 入力範囲とロケール表記を制限する数値バリデータ。親入力欄が所有する。
    const auto *validator = static_cast<const QDoubleValidator*>(edit->validator());
    // value: 入力欄に選択されている単位（mmまたはcm）で扱う数値。
    const double value = validator->locale().toDouble(edit->text(), &ok);
    return ok ? value : 0;
}

/// @brief 入力欄のロケールで数値を表記し、末尾の不要な0と小数点を除く。
/// @param edit 対象のテキスト入力欄。所有権は所属するダイアログにある。
/// @param value 入力欄に選択されている単位（mmまたはcm）で扱う数値。
QString PdfCalibrationDialog::format(const QLineEdit *edit, double value)
{
    // locale: 小数点などの入力・表示形式に使うロケール。
    const QLocale locale = static_cast<const QDoubleValidator*>(edit->validator())->locale();
    // text: 表示または検証に使用する文字列。
    QString text = locale.toString(value, 'f', 12);
    while (text.endsWith(QLatin1Char('0'))) text.chop(1);
    if (text.endsWith(locale.decimalPoint())) text.chop(locale.decimalPoint().size());
    return text;
}

/// @brief 現在距離を実寸mmで返す。測定した精密値があれば入力文字列より優先する。
double PdfCalibrationDialog::currentDistanceMm() const
{
    return m_measuredCurrent.value_or(parse(m_current));
}

/// @brief 新しい距離を実寸mmで返す。測定値がなければ選択単位からmmへ換算する。
double PdfCalibrationDialog::targetDistanceMm() const
{
    return m_measuredTarget.value_or(parse(m_target) * m_unitFactor);
}

/// @brief 現在距離の2点を取り込み、1点目を固定点として入力表示へ反映する。
/// @param first 測定の1点目の実寸座標（mm）。
/// @param second 測定の2点目の実寸座標（mm）。
void PdfCalibrationDialog::setCurrentMeasurement(QPointF first, QPointF second)
{
    // distance: 指定した2点間の実寸距離（mm）。
    const double distance = QLineF(first, second).length();
    if (!positiveDistance(distance)) {
        m_fixedPoint.reset(); m_measuredCurrent.reset(); m_current->clear();
        m_anchorLabel->setText(tr("固定点: 現在の距離を測定した1点目"));
        validateInput();
        setApplyError(tr("現在の距離は0より大きく、10000000mm以下で指定してください。"));
        return;
    }
    {
        // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
        QSignalBlocker block(m_current);
        m_current->setText(format(m_current, distance));
    }
    m_measuredCurrent = distance;
    m_fixedPoint = first;
    m_anchorLabel->setText(tr("固定点（1点目）: X %1 mm / Y %2 mm")
        .arg(first.x(), 0, 'f', 2).arg(first.y(), 0, 'f', 2));
    validateInput();
}

/// @brief 新しい距離の2点を取り込み、現在の単位に換算して入力表示へ反映する。
/// @param first 測定の1点目の実寸座標（mm）。
/// @param second 測定の2点目の実寸座標（mm）。
void PdfCalibrationDialog::setTargetMeasurement(QPointF first, QPointF second)
{
    // distance: 指定した2点間の実寸距離（mm）。
    const double distance = QLineF(first, second).length();
    if (!positiveDistance(distance)) {
        m_measuredTarget.reset(); m_target->clear(); validateInput();
        setApplyError(tr("新しい距離は0より大きく、10000000mm以下で指定してください。"));
        return;
    }
    {
        // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
        QSignalBlocker block(m_target);
        m_target->setText(format(m_target, distance / m_unitFactor));
    }
    m_measuredTarget = distance;
    validateInput();
}

/// @brief 補正の適用に失敗した理由をダイアログへ表示する。
/// @param message 利用者へ表示するエラー理由。
void PdfCalibrationDialog::setApplyError(const QString &message) { m_error->setText(message); }

/// @brief 入力の有効性を確認し、確定ボタンとエラー表示を更新する。
void PdfCalibrationDialog::validateInput()
{
    // valid: 入力値や処理対象が有効であることを示す判定結果。
    const bool valid = m_fixedPoint.has_value() && m_current->hasAcceptableInput()
        && m_target->hasAcceptableInput() && positiveDistance(currentDistanceMm())
        && positiveDistance(targetDistanceMm());
    m_ok->setEnabled(valid);
    m_error->setText(valid ? QString() : tr("PDF上の現在距離を2点で測定し、新しい距離に正の数値を入力してください。"));
    m_factorLabel->setText(valid ? tr("補正倍率: %1倍（縦横同比率）")
        .arg(targetDistanceMm() / currentDistanceMm(), 0, 'g', 12) : tr("補正倍率: —"));
}
