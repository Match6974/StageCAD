#include "mainwindow.h"
#include "pdfcalibrationdialog.h"
#include "pdfplacement.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>

// Exercise the application's real event loop and stack-allocated MainWindow,
// including destruction after exec() returns (as in main.cpp).
int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    const QString scenario = QString::fromLocal8Bit(argv[1]);
    QTimer startup;
    QObject::connect(&startup, &QTimer::timeout, [&] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (scenario == "empty") dialog->reject();
            else if (auto *input = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                input->setText(QString::fromUtf8(SAMPLE_PDF_PATH));
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    startup.start(20);
    MainWindow w;
    startup.stop();
    w.show();
    QTimer::singleShot(0, &w, [&] {
        const bool hasPdf = scenario != "empty";
        auto *placement = w.findChild<PdfPlacement*>();
        if (!placement || placement->hasPage() != hasPdf) { app.exit(3); return; }
        if (scenario == "calibrated") {
            if (!placement->calibrate(10, 20, {-10, 10})) { app.exit(4); return; }
        }
        if (scenario == "dialog" || scenario == "picking" || scenario == "canceled") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            w.findChild<QAction*>("calibratePdfAction")->trigger();
            auto *dialog = w.findChild<PdfCalibrationDialog*>();
            if (!dialog) { app.exit(5); return; }
            if (scenario == "picking")
                dialog->findChild<QPushButton*>("measurePdfCurrentButton")->click();
            else if (scenario == "canceled") dialog->reject();
        }
        if (!w.close()) app.exit(6);
    });
    QTimer::singleShot(5000, &app, [&] { app.exit(7); });
    const int result = app.exec();
    std::printf("Shutdown scenario %s: event loop returned %d\n", qPrintable(scenario), result);
    return result;
}
