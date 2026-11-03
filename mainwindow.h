#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include "pdfplacement.h"

class DrawingSettings;
class DrawingView;
class QLabel;
class QAction;
class QPdfDocument;
class QPdfView;
class QSpinBox;
class QTabWidget;
class PdfCalibrationDialog;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool importPdf(const QString &path, QString *error = nullptr);

private:
    void editScale();
    void updateScaleDisplay();
    void choosePdf();
    bool showPdfPage(int page, QString *error);
    void editPdfCalibration();
    void updatePdfActions();

    Ui::MainWindow *ui;
    DrawingSettings *m_drawingSettings;
    DrawingView *m_drawingView;
    QLabel *m_scaleLabel;
    QLabel *m_coordinates;
    QLabel *m_paperCoverage;
    PdfPlacement *m_pdfPlacement;
    QPdfDocument *m_pdfDocument = nullptr;
    QPdfView *m_pdfView;
    QTabWidget *m_tabs;
    QAction *m_scaleAction;
    QAction *m_importPdfAction;
    QAction *m_calibratePdfAction;
    QAction *m_zoomInAction;
    QAction *m_zoomOutAction;
    QAction *m_fitPaperAction;
    QSpinBox *m_pdfPageInput;
    QLabel *m_pdfPageCount;
    QPointer<PdfCalibrationDialog> m_pdfCalibrationDialog;
    QHash<int, PdfGeometry> m_pdfPageGeometry;
    int m_pdfPage = 0;
    double m_importDenominator = 1;
    QString m_pdfPath;
};
#endif // MAINWINDOW_H
