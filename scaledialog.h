#ifndef SCALEDIALOG_H
#define SCALEDIALOG_H

#include <QDialog>

class DrawingSettings;
class QComboBox;
class QLineEdit;
class QLabel;
class QPushButton;

// Edits a draft value; the drawing changes only after a valid OK.
class ScaleDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ScaleDialog(const DrawingSettings *settings, QWidget *parent = nullptr);
    double scaleDenominator() const;

private:
    void validateInput();
    QComboBox *m_presets;
    QLineEdit *m_custom;
    QLabel *m_error;
    QPushButton *m_ok;
};

#endif
