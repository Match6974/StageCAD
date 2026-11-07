#include "drawingsettings.h"
#include "drawingview.h"
#include "mainwindow.h"
#include "pdfcalibrationdialog.h"
#include "pdfpageitem.h"
#include "pdfplacement.h"
#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPdfDocument>
#include <QPdfView>
#include <QPdfPageNavigator>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <limits>

namespace {
QImage sampleImage()
{
    QImage image(100, 100, QImage::Format_RGB32);
    image.fill(Qt::red);
    for (int y = 50; y < 100; ++y)
        for (int x = 0; x < 100; ++x) image.setPixelColor(x, y, Qt::blue);
    return image;
}
void fileAnswer(QTimer &timer, const QString &path = {})
{
    QObject::connect(&timer, &QTimer::timeout, &timer, [path] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (path.isEmpty()) dialog->reject();
            else if (auto *edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                edit->setText(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    timer.start(20);
}
QPointF click(DrawingView *view, QPointF model)
{
    const QPoint position = view->mapFromScene(model);
    const QPointF actual = view->mapToScene(position);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
    return actual;
}
QPushButton *okButton(PdfCalibrationDialog *dialog)
{
    return dialog->findChild<QDialogButtonBox*>("pdfCalibrationButtons")->button(QDialogButtonBox::Ok);
}
}

class PdfCalibrationTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QLocale::setDefault(QLocale::c());
    }
    void physicalSizeAndFixedFirstPoint()
    {
        PdfPlacement placement;
        QVERIFY(!placement.hasPage());
        QVERIFY(placement.setPage(sampleImage(), {720, 360}, 1));
        QVERIFY(qAbs(placement.sizeMm().width() - 254) < 1e-10);
        QVERIFY(qAbs(placement.sizeMm().height() - 127) < 1e-10);
        const QPointF first(-76.2, 31.75), second(-50.8, 31.75);
        const double current = QLineF(first, second).length();
        const QPointF originalCenter = placement.centerMm();
        const QSizeF originalSize = placement.sizeMm();
        const QPointF firstLocal((first.x() - originalCenter.x()) / originalSize.width(),
                                 (first.y() - originalCenter.y()) / originalSize.height());
        const QPointF secondLocal((second.x() - originalCenter.x()) / originalSize.width(),
                                  (second.y() - originalCenter.y()) / originalSize.height());
        QSignalSpy changes(&placement, &PdfPlacement::placementChanged);
        QVERIFY(placement.calibrate(current, 254, first));
        QCOMPARE(changes.count(), 1);
        auto world = [&](QPointF local) {
            return placement.centerMm() + QPointF(local.x() * placement.sizeMm().width(),
                                                  local.y() * placement.sizeMm().height());
        };
        QVERIFY(QLineF(world(firstLocal), first).length() < 1e-9);
        QVERIFY(qAbs(QLineF(world(firstLocal), world(secondLocal)).length() - 254) < 1e-9);
        QVERIFY(qAbs(placement.sizeMm().width() / placement.sizeMm().height() - 2) < 1e-12);
        QVERIFY(placement.calibrate(254, 127, first));
        QVERIFY(QLineF(world(firstLocal), first).length() < 1e-9);
        QVERIFY(qAbs(QLineF(world(firstLocal), world(secondLocal)).length() - 127) < 1e-9);
    }
    void invalidOperationsPreservePlacement()
    {
        PdfPlacement placement;
        QVERIFY(!placement.calibrate(1, 2, {}));
        QVERIFY(placement.setPage(sampleImage(), {720, 360}, 50));
        QVERIFY(placement.calibrate(100, 200, {10, 20}));
        const PdfGeometry before = placement.geometry();
        QSignalSpy changes(&placement, &PdfPlacement::placementChanged);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (double bad : {0.0, -1.0, nan, inf}) {
            QVERIFY(!placement.calibrate(bad, 10, {}));
            QVERIFY(!placement.calibrate(10, bad, {}));
        }
        QVERIFY(!placement.calibrate(1e-12, 1e7, {}));
        QVERIFY(!placement.calibrate(10, 20, {nan, 0}));
        QVERIFY(!placement.setPage({}, {72, 72}, 1));
        QVERIFY(!placement.setPage(sampleImage(), {nan, 72}, 1));
        QVERIFY(!placement.setPage(sampleImage(), {72, 72}, 0));
        QVERIFY(!placement.setPage(sampleImage(), {-72, 72}, 1));
        QCOMPARE(placement.sizeMm(), before.sizeMm);
        QCOMPARE(placement.centerMm(), before.centerMm);
        QCOMPARE(changes.count(), 0);
    }
    void rasterOrientationAndScaleIndependence()
    {
        DrawingSettings settings;
        DrawingView view(&settings);
        PdfPlacement placement;
        view.scene()->addItem(new PdfPageItem(&placement));
        QVERIFY(placement.setPage(sampleImage(), {72, 72}, 1));
        view.resize(800, 800); view.show(); QTest::qWait(10);
        QImage screen = view.viewport()->grab().toImage();
        const QColor top = screen.pixelColor(view.mapFromScene({0, 6}));
        const QColor bottom = screen.pixelColor(view.mapFromScene({0, -6}));
        QVERIFY(top.red() > 200 && top.blue() < 50);
        QVERIFY(bottom.blue() > 200 && bottom.red() < 50);
        const QSizeF realSize = placement.sizeMm();
        QVERIFY(settings.setScaleDenominator(100));
        QVERIFY(view.setDisplayZoom(2));
        QCOMPARE(placement.sizeMm(), realSize);
        QCOMPARE(placement.centerMm(), QPointF());
    }
    void distancePickingValidationAndEscape()
    {
        DrawingSettings settings; DrawingView view(&settings);
        view.resize(800, 800); view.show(); QTest::qWait(10);
        QSignalSpy picked(&view, &DrawingView::distancePicked);
        QSignalSpy canceled(&view, &DrawingView::distancePickCanceled);
        const QRectF bounds(-50, -100, 100, 200);
        view.startDistancePick(DrawingView::DistanceKind::Current, bounds);
        click(&view, {70, 0}); // Outside the PDF is not a valid current distance.
        const QPointF first = click(&view, {-30, 10});
        click(&view, {-30, 10});
        QCOMPARE(picked.count(), 0); QVERIFY(view.isPickingDistance());
        const QPointF second = click(&view, {30, 10});
        QCOMPARE(picked.count(), 1);
        QCOMPARE(picked.last().at(1).toPointF(), first);
        QCOMPARE(picked.last().at(2).toPointF(), second);
        QVERIFY(!view.isPickingDistance());
        view.startDistancePick(DrawingView::DistanceKind::Target, bounds);
        click(&view, {70, 0}); click(&view, {90, 0}); // Target may come from outside the PDF.
        QCOMPARE(picked.count(), 2);
        view.startDistancePick(DrawingView::DistanceKind::Current, bounds);
        click(&view, {0, 0}); QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(canceled.count(), 1); QVERIFY(!view.isPickingDistance());
    }
    void dialogUnitsPrecisionAndInvalidInput()
    {
        PdfCalibrationDialog dialog; dialog.show();
        auto *current = dialog.findChild<QLineEdit*>("pdfCurrentDistance");
        auto *target = dialog.findChild<QLineEdit*>("pdfTargetDistance");
        auto *unit = dialog.findChild<QComboBox*>("pdfTargetUnit");
        QVERIFY(!okButton(&dialog)->isEnabled());
        current->setText("50"); target->setText("100");
        QVERIFY(!okButton(&dialog)->isEnabled()); // The fixed first point must be measured.
        dialog.setCurrentMeasurement({10, 20}, {40, 60});
        QCOMPARE(dialog.currentDistanceMm(), 50.0);
        QCOMPARE(*dialog.fixedPointMm(), QPointF(10, 20));
        QVERIFY(okButton(&dialog)->isEnabled());
        unit->setCurrentIndex(1);
        QCOMPARE(target->text(), QStringLiteral("10"));
        QCOMPARE(dialog.targetDistanceMm(), 100.0);
        target->setText("25"); QCOMPARE(dialog.targetDistanceMm(), 250.0);
        for (const QString &bad : {QString(), QStringLiteral("0"), QStringLiteral("-1"),
                                   QStringLiteral("nan"), QStringLiteral("inf"), QStringLiteral("1,000")}) {
            target->setText(bad); QVERIFY(!okButton(&dialog)->isEnabled());
        }
        dialog.setCurrentMeasurement({1, 2}, {2, 3});
        dialog.setTargetMeasurement({0, 0}, {1, 2});
        QCOMPARE(dialog.currentDistanceMm(), std::sqrt(2.0));
        QCOMPARE(dialog.targetDistanceMm(), std::sqrt(5.0));
        unit->setCurrentIndex(0); unit->setCurrentIndex(1);
        QCOMPARE(dialog.targetDistanceMm(), std::sqrt(5.0));
        QSignalSpy apply(&dialog, &PdfCalibrationDialog::applyRequested);
        QTest::mouseClick(okButton(&dialog), Qt::LeftButton);
        QCOMPARE(apply.count(), 1); QVERIFY(dialog.isVisible());
        dialog.setCurrentMeasurement({0, 0}, {0, 0});
        QVERIFY(!okButton(&dialog)->isEnabled()); QVERIFY(!dialog.fixedPointMm());
        dialog.setCurrentMeasurement({0, 0}, {10, 0});
        dialog.setTargetMeasurement({0, 0}, {0, 0});
        QVERIFY(!okButton(&dialog)->isEnabled());
        dialog.reject(); QCOMPARE(dialog.result(), int(QDialog::Rejected));
    }
    void dialogRespectsDecimalLocale()
    {
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        PdfCalibrationDialog dialog;
        QLocale::setDefault(QLocale::c());
        dialog.setCurrentMeasurement({0, 0}, {10.5, 0});
        auto *target = dialog.findChild<QLineEdit*>("pdfTargetDistance");
        target->setText("25,5");
        QCOMPARE(dialog.currentDistanceMm(), 10.5);
        QCOMPARE(dialog.targetDistanceMm(), 25.5);
        QVERIFY(okButton(&dialog)->isEnabled());
        dialog.findChild<QComboBox*>("pdfTargetUnit")->setCurrentIndex(1);
        QCOMPARE(target->text(), QStringLiteral("2,55"));
        QCOMPARE(dialog.targetDistanceMm(), 25.5);
    }
    void windowImportCalibrateCancelAndPageCache()
    {
        QTimer startup; fileAnswer(startup);
        MainWindow window; startup.stop(); window.show();
        auto *settings = window.findChild<DrawingSettings*>();
        auto *placement = window.findChild<PdfPlacement*>();
        auto *view = window.findChild<DrawingView*>();
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        auto *pages = window.findChild<QSpinBox*>("pdfPageInput");
        auto *importAction = window.findChild<QAction*>("importPdfAction");
        auto *calibrate = window.findChild<QAction*>("calibratePdfAction");
        QVERIFY(settings && placement && view && tabs && pages && importAction && calibrate);
        QVERIFY(!calibrate->isEnabled());
        QVERIFY(settings->setScaleDenominator(50));
        QTimer choose; fileAnswer(choose, QString::fromUtf8(SAMPLE_PDF_PATH));
        importAction->trigger(); choose.stop();
        QVERIFY(placement->hasPage()); QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(pages->maximum(), 2);
        auto *document = window.findChild<QPdfDocument*>(); QVERIFY(document);
        QCOMPARE(placement->sizeMm(), document->pagePointSize(0) * (25.4 / 72 * 50));
        const PdfGeometry before = placement->geometry();
        auto *line = view->scene()->addLine(0, 0, 1000, 0);
        calibrate->trigger();
        auto *dialog = window.findChild<PdfCalibrationDialog*>(); QVERIFY(dialog);
        QVERIFY(!importAction->isEnabled()); QVERIFY(!pages->isEnabled());
        QVERIFY(!tabs->tabBar()->isEnabled());
        QTest::mouseClick(dialog->findChild<QPushButton*>("measurePdfCurrentButton"), Qt::LeftButton);
        QVERIFY(view->isPickingDistance()); QVERIFY(!dialog->isVisible());
        const QPointF first = click(view, {-before.sizeMm.width() / 5, before.sizeMm.height() / 10});
        click(view, {before.sizeMm.width() / 5, before.sizeMm.height() / 10});
        QVERIFY(dialog->isVisible());
        QCOMPARE(*dialog->fixedPointMm(), first);
        QCOMPARE(placement->sizeMm(), before.sizeMm); // Measuring is not applying.
        auto *target = dialog->findChild<QLineEdit*>("pdfTargetDistance");
        target->setText(QString::number(dialog->currentDistanceMm() * 2, 'f', 10));
        const double ratio = dialog->targetDistanceMm() / dialog->currentDistanceMm();
        const QPointF localFirst((first.x() - before.centerMm.x()) / before.sizeMm.width(),
                                  (first.y() - before.centerMm.y()) / before.sizeMm.height());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) {
            QVERIFY(window.grab().save("pdf-workspace.png"));
            QVERIFY(dialog->grab().save("pdf-calibration-dialog.png"));
        }
        QTest::mouseClick(okButton(dialog), Qt::LeftButton);
        const QPointF fixedAfter = placement->centerMm()
            + QPointF(localFirst.x() * placement->sizeMm().width(), localFirst.y() * placement->sizeMm().height());
        QVERIFY(QLineF(first, fixedAfter).length() < 1e-8);
        QVERIFY(qAbs(placement->sizeMm().width() - before.sizeMm.width() * ratio) < 1e-8);
        QCOMPARE(settings->scaleDenominator(), 50.0); QCOMPARE(line->line().length(), 1000.0);
        const PdfGeometry calibrated = placement->geometry();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        calibrate->trigger(); dialog = window.findChild<PdfCalibrationDialog*>(); QVERIFY(dialog);
        QTest::mouseClick(dialog->findChild<QPushButton*>("measurePdfCurrentButton"), Qt::LeftButton);
        click(view, {0, 0}); QTest::keyClick(view, Qt::Key_Escape);
        QVERIFY(dialog->isVisible()); QVERIFY(!view->isPickingDistance());
        QTest::mouseClick(dialog->findChild<QPushButton*>("measurePdfCurrentButton"), Qt::LeftButton);
        click(view, {0, 0}); click(view, {500, 0});
        QTest::mouseClick(dialog->findChild<QPushButton*>("measurePdfTargetButton"), Qt::LeftButton);
        click(view, {0, 500}); click(view, {500, 500});
        QVERIFY(dialog->isVisible()); QVERIFY(okButton(dialog)->isEnabled());
        auto *current = dialog->findChild<QLineEdit*>("pdfCurrentDistance");
        current->setText("0.000001"); dialog->findChild<QLineEdit*>("pdfTargetDistance")->setText("10000000");
        QTest::mouseClick(okButton(dialog), Qt::LeftButton);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->findChild<QLabel*>("pdfCalibrationError")->text().isEmpty());
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        dialog->reject();
        QCOMPARE(placement->centerMm(), calibrated.centerMm);
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        QVERIFY(importAction->isEnabled()); QVERIFY(pages->isEnabled());
        QVERIFY(settings->setScaleDenominator(100));
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        pages->setValue(2);
        QCOMPARE(window.findChild<QPdfView*>()->pageNavigator()->currentPage(), 1);
        QCOMPARE(placement->sizeMm(), document->pagePointSize(1) * (25.4 / 72 * 50));
        pages->setValue(1);
        QCOMPARE(window.findChild<QPdfView*>()->pageNavigator()->currentPage(), 0);
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        QCOMPARE(placement->centerMm(), calibrated.centerMm);
        QTimer cancelImport; fileAnswer(cancelImport); importAction->trigger(); cancelImport.stop();
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        QFile invalid(temporary.filePath("bad.pdf")); QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a PDF"); invalid.close();
        QString error;
        QVERIFY(!window.importPdf(invalid.fileName(), &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!window.importPdf(temporary.filePath("missing.pdf"), &error));
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm); QCOMPARE(placement->centerMm(), calibrated.centerMm);
        QCOMPARE(window.findChild<QPdfDocument*>(), document);
    }
};

QTEST_MAIN(PdfCalibrationTests)
#include "test_pdfcalibration.moc"
