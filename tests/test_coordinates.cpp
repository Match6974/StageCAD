#include "drawingsettings.h"
#include "drawingview.h"
#include "mainwindow.h"
#include "scaledialog.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPdfDocument>
#include <QPdfView>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <limits>

namespace {
// Drive the real startup file dialog without depending on native OS dialogs.
void answerFileDialog(QTimer &timer, const QString &path = {})
{
    QObject::connect(&timer, &QTimer::timeout, &timer, [path] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (path.isEmpty()) {
                dialog->reject();
            } else if (auto *input = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                input->setText(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    timer.start(20);
}

void answerScaleDialog(QTimer &timer, const QString &value, bool accept)
{
    QObject::connect(&timer, &QTimer::timeout, &timer, [value, accept] {
        for (auto *widget : QApplication::topLevelWidgets()) {
            auto *dialog = qobject_cast<ScaleDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            auto *presets = dialog->findChild<QComboBox*>("scalePresets");
            presets->setCurrentIndex(presets->count() - 1);
            dialog->findChild<QLineEdit*>("scaleDenominatorInput")->setText(value);
            auto *buttons = dialog->findChild<QDialogButtonBox*>("scaleDialogButtons");
            QTest::mouseClick(buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel),
                              Qt::LeftButton);
        }
    });
    timer.start(20);
}
}

class CoordinateTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QLocale::setDefault(QLocale::c());
    }

    void scaleConversion_data()
    {
        QTest::addColumn<double>("denominator");
        QTest::addColumn<double>("paperLength");
        QTest::newRow("original") << 1.0 << 1000.0;
        QTest::newRow("one-to-ten") << 10.0 << 100.0;
        QTest::newRow("one-to-hundred") << 100.0 << 10.0;
        QTest::newRow("custom-one-to-fifty") << 50.0 << 20.0;
        QTest::newRow("fractional-enlargement") << 0.5 << 2000.0;
    }

    void scaleConversion()
    {
        QFETCH(double, denominator);
        QFETCH(double, paperLength);
        DrawingSettings settings;
        QVERIFY(settings.setScaleDenominator(denominator));
        const QPointF a(0, 0), b(1000, 0);
        const QPointF center = settings.modelToPaperMm(a);
        QCOMPARE(center, QPointF(105, 148.5));
        const double printedLength = QLineF(center, settings.modelToPaperMm(b)).length();
        QVERIFY(qAbs(printedLength - paperLength) < 1e-9);
        QCOMPARE(QLineF(a, b).length(), 1000.0);
        QCOMPARE(settings.paperToModelMm(center), QPointF(0, 0));
        const QPointF point(-234.5, 678.9);
        QVERIFY(QLineF(point, settings.paperToModelMm(settings.modelToPaperMm(point))).length() < 1e-8);
        QVERIFY(settings.modelToPaperMm({0, 100}).y() < center.y());
        QCOMPARE(settings.paperRectInModel().center(), QPointF(0, 0));
        QCOMPARE(settings.printableRectInModel().center(), QPointF(0, 0));
        QCOMPARE(settings.paperRectInModel().size(), QSizeF(210, 297) * denominator);
        QCOMPARE(settings.printableRectInModel().size(), QSizeF(201.4, 287.9) * denominator);
    }

    void invalidScalesLeaveSettingsUntouched()
    {
        DrawingSettings settings;
        QCOMPARE(settings.scaleDenominator(), 1.0);
        QSignalSpy changes(&settings, &DrawingSettings::scaleChanged);
        QVERIFY(settings.setScaleDenominator(50));
        QVERIFY(settings.setScaleDenominator(50));
        QCOMPARE(changes.count(), 1);
        for (const double invalid : {0.0, -1.0, 1e-7, 1e10,
                 std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!settings.setScaleDenominator(invalid));
            QCOMPARE(settings.scaleDenominator(), 50.0);
        }
        QCOMPARE(changes.count(), 1);
        DrawingSettings anotherDrawing;
        QCOMPARE(anotherDrawing.scaleDenominator(), 1.0);
    }

    void viewKeepsModelGeometryStable()
    {
        DrawingSettings settings;
        DrawingView view(&settings);
        view.resize(850, 750); view.show();
        QTest::qWait(10);
        const QLineF modelLine(-500, 100, 500, 100);
        auto *line = view.scene()->addLine(modelLine);
        auto pixelLength = [&] {
            return QLineF(view.mapFromScene(line->line().p1()),
                          view.mapFromScene(line->line().p2())).length();
        };
        QVERIFY(settings.setScaleDenominator(100));
        const double at100 = pixelLength();
        QVERIFY(settings.setScaleDenominator(50));
        QVERIFY(qAbs(pixelLength() - at100 * 2) < 2);
        QCOMPARE(line->line(), modelLine);
        QCOMPARE(line->line().length(), 1000.0);
        const double paperLength = QLineF(settings.modelToPaperMm(modelLine.p1()),
                                          settings.modelToPaperMm(modelLine.p2())).length();
        const double atZoom1 = pixelLength();
        const auto initialTransform = view.transform();
        const auto initialSize = view.maximumViewportSize();
        QVERIFY(view.setDisplayZoom(2));
        QCOMPARE(view.transform().m11(), initialTransform.m11() * 2);
        // Each endpoint is rounded to a viewport pixel, hence at most 2px.
        QVERIFY2(qAbs(pixelLength() - atZoom1 * 2) <= 2,
            qPrintable(QString("zoom1 pixels=%1 zoom2 pixels=%2 scales=%3/%4 size=%5x%6/%7x%8")
                .arg(atZoom1).arg(pixelLength()).arg(initialTransform.m11()).arg(view.transform().m11())
                .arg(initialSize.width()).arg(initialSize.height())
                .arg(view.maximumViewportSize().width()).arg(view.maximumViewportSize().height())));
        QCOMPARE(line->line(), modelLine);
        QCOMPARE(settings.scaleDenominator(), 50.0);
        QCOMPARE(QLineF(settings.modelToPaperMm(modelLine.p1()),
                        settings.modelToPaperMm(modelLine.p2())).length(), paperLength);
        QVERIFY(!view.setDisplayZoom(0));
        QVERIFY(!view.setDisplayZoom(std::numeric_limits<double>::quiet_NaN()));
        QCOMPARE(view.displayZoom(), 2.0);
        QVERIFY(view.setDisplayZoom(1));
        view.resize(1000, 900); QTest::qWait(10);
        QCOMPARE(line->line(), modelLine);
        QVERIFY((view.mapFromScene({0, 0}) - view.viewport()->rect().center()).manhattanLength() <= 2);
        QVERIFY(view.mapFromScene({0, 100}).y() < view.mapFromScene({0, 0}).y());
        QVERIFY(view.mapFromScene({100, 0}).x() > view.mapFromScene({0, 0}).x());
        QSignalSpy cursor(&view, &DrawingView::cursorPositionChanged);
        const QPoint position = view.mapFromScene({1000, 1000});
        QTest::mouseMove(view.viewport(), position);
        QTRY_VERIFY(!cursor.isEmpty());
        QCOMPARE(cursor.last().at(0).toPointF(), view.mapToScene(position));
        QSignalSpy left(&view, &DrawingView::cursorLeft);
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&view, &leave);
        QCOMPARE(left.count(), 1);
        for (double bound : {DrawingSettings::minimumScale, DrawingSettings::maximumScale}) {
            QVERIFY(settings.setScaleDenominator(bound));
            QVERIFY(view.viewportTransform().isInvertible());
            QCOMPARE(line->line(), modelLine);
        }
        view.resize(50, 50);
        QVERIFY(view.setDisplayZoom(0.1));
        QVERIFY(view.viewportTransform().isInvertible());
    }

    void dialogPresetsAndValidation()
    {
        DrawingSettings settings;
        ScaleDialog dialog(&settings); dialog.show();
        auto *presets = dialog.findChild<QComboBox*>("scalePresets");
        auto *input = dialog.findChild<QLineEdit*>("scaleDenominatorInput");
        auto *buttons = dialog.findChild<QDialogButtonBox*>("scaleDialogButtons");
        auto *ok = buttons->button(QDialogButtonBox::Ok);
        QCOMPARE(dialog.scaleDenominator(), 1.0);
        QVERIFY(!input->isEnabled());
        for (int i = 0; i < 3; ++i) {
            presets->setCurrentIndex(i);
            QCOMPARE(dialog.scaleDenominator(), presets->itemData(i).toDouble());
            QVERIFY(ok->isEnabled());
        }
        presets->setCurrentIndex(3);
        QVERIFY(input->isEnabled());
        for (const QString &bad : {QString(), QStringLiteral("0"), QStringLiteral("-50"),
                 QStringLiteral("nan"), QStringLiteral("inf"), QStringLiteral("abc"),
                 QStringLiteral("10001"), QStringLiteral("0.0000001"), QStringLiteral("1,000")}) {
            input->setText(bad);
            QVERIFY2(!ok->isEnabled(), qPrintable(bad));
        }
        input->setText("50.25");
        QVERIFY(ok->isEnabled());
        QCOMPARE(dialog.scaleDenominator(), 50.25);
        QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(settings.scaleDenominator(), 1.0);
    }

    void dialogRespectsDecimalLocale()
    {
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        DrawingSettings settings;
        QVERIFY(settings.setScaleDenominator(1234.5));
        ScaleDialog dialog(&settings);
        // Restore before assertions so later tests do not inherit this locale.
        QLocale::setDefault(QLocale::c());
        auto *input = dialog.findChild<QLineEdit*>("scaleDenominatorInput");
        auto *buttons = dialog.findChild<QDialogButtonBox*>("scaleDialogButtons");
        QCOMPARE(input->text(), QStringLiteral("1234,5"));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        input->setText("50,25");
        QCOMPARE(dialog.scaleDenominator(), 50.25);
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }

    void mainWindowScaleTransactionAndCoordinates()
    {
        QTimer startup; answerFileDialog(startup);
        MainWindow window; startup.stop(); window.show();
        auto *settings = window.findChild<DrawingSettings*>();
        auto *view = window.findChild<DrawingView*>();
        auto *action = window.findChild<QAction*>("editScaleAction");
        auto *label = window.findChild<QLabel*>("drawingScaleLabel");
        auto *coordinates = window.findChild<QLabel*>("coordinatesLabel");
        QVERIFY(settings && view && action && label && coordinates);
        QVERIFY(action->isEnabled());
        auto *line = view->scene()->addLine(0, 0, 1000, 0);
        QTimer cancel; answerScaleDialog(cancel, "100", false);
        action->trigger(); cancel.stop();
        QCOMPARE(settings->scaleDenominator(), 1.0);
        QTimer accept; answerScaleDialog(accept, "50", true);
        action->trigger(); accept.stop();
        QCOMPARE(settings->scaleDenominator(), 50.0);
        QCOMPARE(line->line().length(), 1000.0);
        QVERIFY(label->text().contains("1:50"));
        auto *coverage = window.findChild<QLabel*>("paperCoverageLabel");
        QVERIFY(coverage->text().contains("10500"));
        QTest::qWait(10);
        const QPoint position = view->mapFromScene({500, -500});
        QTest::mouseMove(view->viewport(), position);
        const QPointF model = view->mapToScene(position);
        QTRY_VERIFY(coordinates->text().contains(QString::number(model.x(), 'f', 2)));
        QVERIFY(coordinates->text().contains(QString::number(model.y(), 'f', 2)));
        QSignalSpy changes(settings, &DrawingSettings::scaleChanged);
        bool invalidInputBlocked = false;
        QTimer invalid; answerScaleDialog(invalid, "0", true);
        // Invalid OK is disabled; close it with Escape after checking the state.
        QTimer::singleShot(100, &window, [&invalidInputBlocked] {
            if (auto *dialog = qobject_cast<ScaleDialog*>(QApplication::activeModalWidget())) {
                auto *buttons = dialog->findChild<QDialogButtonBox*>("scaleDialogButtons");
                invalidInputBlocked = !buttons->button(QDialogButtonBox::Ok)->isEnabled();
                QTest::keyClick(dialog, Qt::Key_Escape);
            }
        });
        action->trigger(); invalid.stop();
        QVERIFY(invalidInputBlocked);
        QCOMPARE(changes.count(), 0);
        QCOMPARE(settings->scaleDenominator(), 50.0);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE"))
            QVERIFY(window.grab().save("step1-workspace.png"));
    }

    void existingPdfViewerStillWorks()
    {
        QTimer startup; answerFileDialog(startup, QString::fromUtf8(SAMPLE_PDF_PATH));
        MainWindow window; startup.stop(); window.show();
        auto *document = window.findChild<QPdfDocument*>();
        auto *pdfView = window.findChild<QPdfView*>();
        auto *settings = window.findChild<DrawingSettings*>();
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        auto *scaleAction = window.findChild<QAction*>("editScaleAction");
        QVERIFY(document && pdfView && settings && tabs && scaleAction);
        QCOMPARE(document->status(), QPdfDocument::Status::Ready);
        QCOMPARE(document->pageCount(), 2);
        QCOMPARE(pdfView->document(), document);
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(!scaleAction->isEnabled());
        const QSizeF originalSize = document->pagePointSize(0);
        const qreal referenceZoom = pdfView->zoomFactor();
        tabs->setCurrentIndex(0);
        QVERIFY(scaleAction->isEnabled());
        QVERIFY(settings->setScaleDenominator(100));
        QCOMPARE(document->pagePointSize(0), originalSize);
        QCOMPARE(pdfView->zoomFactor(), referenceZoom);
        QVERIFY(!document->render(0, QSize(600, 800)).isNull());
    }
};

QTEST_MAIN(CoordinateTests)
#include "test_coordinates.moc"
