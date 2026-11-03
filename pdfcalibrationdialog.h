#ifndef PDFCALIBRATIONDIALOG_H
#define PDFCALIBRATIONDIALOG_H

#include <QDialog>
#include <QPointF>
#include <optional>

class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;

class PdfCalibrationDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PdfCalibrationDialog(QWidget *parent = nullptr);
    double currentDistanceMm() const;
    double targetDistanceMm() const;
    std::optional<QPointF> fixedPointMm() const { return m_fixedPoint; }
    void setCurrentMeasurement(QPointF firstMm, QPointF secondMm);
    void setTargetMeasurement(QPointF firstMm, QPointF secondMm);
    void setApplyError(const QString &message);

signals:
    void measureCurrentRequested();
    void measureTargetRequested();
    void applyRequested();

private:
    void validateInput();
    static double parse(const QLineEdit *edit);
    static QString format(const QLineEdit *edit, double value);
    QLineEdit *m_current;
    QLineEdit *m_target;
    QComboBox *m_unit;
    QLabel *m_anchorLabel;
    QLabel *m_error;
    QLabel *m_factorLabel;
    QPushButton *m_ok;
    double m_unitFactor = 1;
    std::optional<double> m_measuredCurrent;
    std::optional<double> m_measuredTarget;
    std::optional<QPointF> m_fixedPoint;
};

#endif
