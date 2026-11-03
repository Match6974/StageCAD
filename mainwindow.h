#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class DrawingSettings;
class DrawingView;
class QLabel;

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

private:
    void editScale();
    void updateScaleDisplay();

    Ui::MainWindow *ui;
    DrawingSettings *m_drawingSettings;
    DrawingView *m_drawingView;
    QLabel *m_scaleLabel;
    QLabel *m_coordinates;
    QLabel *m_paperCoverage;
};
#endif // MAINWINDOW_H
