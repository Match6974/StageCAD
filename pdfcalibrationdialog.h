/**
 * @file pdfcalibrationdialog.h
 * @brief PDFの距離補正に使う測定値・単位・固定点を入力、検証する。
 */

#ifndef PDFCALIBRATIONDIALOG_H
#define PDFCALIBRATIONDIALOG_H

#include <QDialog>
#include <QPointF>
#include <optional>

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;

/// @brief 補正値を一時保持し、適用要求を通知する。PDF本体の更新は呼び出し側が行う。
class PdfCalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    /// @brief 距離・単位・固定点の補正UIを作り、入力検証と要求通知を接続する。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit PdfCalibrationDialog(QWidget *parent = nullptr);
    /// @brief 現在距離を実寸mmで返す。測定した精密値があれば入力文字列より優先する。
    double currentDistanceMm() const;
    /// @brief 新しい距離を実寸mmで返す。測定値がなければ選択単位からmmへ換算する。
    double targetDistanceMm() const;
    /// @brief 現在距離の測定で指定した1点目を返す。未測定なら空のoptional。
    std::optional<QPointF> fixedPointMm() const { return m_fixedPoint; }
    /// @brief 現在距離の2点を取り込み、1点目を固定点として入力表示へ反映する。
    /// @param firstMm 指定した1点目の実寸座標（mm）。
    /// @param secondMm 指定した2点目の実寸座標（mm）。
    void setCurrentMeasurement(QPointF firstMm, QPointF secondMm);
    /// @brief 新しい距離の2点を取り込み、現在の単位に換算して入力表示へ反映する。
    /// @param firstMm 指定した1点目の実寸座標（mm）。
    /// @param secondMm 指定した2点目の実寸座標（mm）。
    void setTargetMeasurement(QPointF firstMm, QPointF secondMm);
    /// @brief 補正の適用に失敗した理由をダイアログへ表示する。
    /// @param message 利用者へ表示するエラー理由。
    void setApplyError(const QString &message);

signals:
    /// @brief PDF上の現在距離を2点から測定する要求を通知する。
    void measureCurrentRequested();
    /// @brief 新しい距離を2点から取得する要求を通知する。
    void measureTargetRequested();
    /// @brief 入力済みの距離と固定点をPDFへ適用する要求を通知する。
    void applyRequested();

private:
    /// @brief 入力の有効性を確認し、確定ボタンとエラー表示を更新する。
    void validateInput();
    /// @brief 入力欄のロケールで数値を読み取る。変換できない場合は0を返す。
    /// @param edit 対象のテキスト入力欄。所有権は所属するダイアログにある。
    static double parse(const QLineEdit *edit);
    /// @brief 入力欄のロケールで数値を表記し、末尾の不要な0と小数点を除く。
    /// @param edit 対象のテキスト入力欄。所有権は所属するダイアログにある。
    /// @param value 入力欄に選択されている単位（mmまたはcm）で扱う数値。
    static QString format(const QLineEdit *edit, double value);
    // m_current: 現在距離をmmで入力する欄。補正ダイアログが所有する。
    QLineEdit *m_current;
    // m_target: 新しい距離を選択中の単位で入力する欄。補正ダイアログが所有する。
    QLineEdit *m_target;
    // m_unit: 新しい距離の単位（mm/cm）を選ぶコンボボックス。
    QComboBox *m_unit;
    // m_anchorLabel: 現在距離の1点目である固定点を表示するラベル。
    QLabel *m_anchorLabel;
    // m_error: 入力または適用エラーを表示するラベル。所属ダイアログが所有する。
    QLabel *m_error;
    // m_factorLabel: 縦横共通の補正倍率を表示するラベル。
    QLabel *m_factorLabel;
    // m_ok: 有効な入力が揃ったときだけ押せる確定ボタン。ボタンボックスが所有する。
    QPushButton *m_ok;
    // m_unitFactor: 選択した距離単位からmmへの換算係数（mm=1、cm=10）。
    double m_unitFactor = 1;
    // m_measuredCurrent: 表示文字列への丸め前の現在距離（実寸mm）。手入力時は破棄する。
    std::optional<double> m_measuredCurrent;
    // m_measuredTarget: 表示文字列への丸め前の新しい距離（実寸mm）。手入力時は破棄する。
    std::optional<double> m_measuredTarget;
    // m_fixedPoint: PDF補正で動かさない測定の1点目（実寸mm）。
    std::optional<QPointF> m_fixedPoint;
};

#endif
