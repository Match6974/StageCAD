#include "drawingtool.h"
#include "drawingsettings.h"
#include "drawingview.h"
#include "mainwindow.h"
#include "pdfcalibrationdialog.h"
#include "pdfplacement.h"
#include "toolpalette.h"
#include <QAction>
#include <QDockWidget>
#include <QFileDialog>
#include <QGraphicsLineItem>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>

namespace {
QToolButton *button(ToolPalette *palette, DrawingTool tool)
{
    for (auto *candidate : palette->findChildren<QToolButton*>())
        if (candidate->defaultAction() == palette->toolAction(tool)) return candidate;
    return nullptr;
}
void cancelStartup(QTimer &timer)
{
    QObject::connect(&timer, &QTimer::timeout, &timer, [] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
    });
    timer.start(20);
}
void drag(DrawingView &view, Qt::MouseButton mouseButton, QPoint delta)
{
    const QPoint start = view.viewport()->rect().center();
    QTest::mousePress(view.viewport(), mouseButton, Qt::NoModifier, start);
    QTest::mouseMove(view.viewport(), start + delta);
    QTest::mouseRelease(view.viewport(), mouseButton, Qt::NoModifier, start + delta);
}
void wheel(DrawingView &view, QPoint position, int angleDelta)
{
    QWheelEvent event(QPointF(position), QPointF(view.viewport()->mapToGlobal(position)),
        {}, {0, angleDelta}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
    QVERIFY(event.isAccepted());
}
}

class ToolPaletteTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
    void exclusiveToolsAndDisabledFutureTools()
    {
        ToolPalette palette; palette.resize(260, 400); palette.show();
        QCOMPARE(palette.currentTool(), DrawingTool::Selection);
        QSignalSpy changed(&palette, &ToolPalette::toolChanged);
        for (DrawingTool tool : {DrawingTool::Pan, DrawingTool::Line, DrawingTool::Rectangle,
                                 DrawingTool::Circle, DrawingTool::Polygon, DrawingTool::Selection}) {
            auto *target = button(&palette, tool); QVERIFY(target);
            QVERIFY(!target->icon().isNull());
            QTest::mouseClick(target, Qt::LeftButton);
            QCOMPARE(palette.currentTool(), tool);
            int checked = 0;
            for (auto *candidate : palette.findChildren<QToolButton*>()) checked += candidate->isChecked();
            QCOMPARE(checked, 1);
        }
        QCOMPARE(changed.count(), 6);
        QTest::mouseClick(button(&palette, DrawingTool::Selection), Qt::LeftButton);
        QCOMPARE(changed.count(), 6);
        for (DrawingTool future : {DrawingTool::Reshape, DrawingTool::Join, DrawingTool::Dimension, DrawingTool::Text}) {
            QVERIFY(!palette.toolAction(future)->isEnabled());
            QVERIFY(!palette.setCurrentTool(future));
            palette.toolAction(future)->trigger();
            QCOMPARE(palette.currentTool(), DrawingTool::Selection);
        }
        QCOMPARE(changed.count(), 6);
    }
    void responsiveRowsAndScrolling()
    {
        ToolPalette palette; palette.resize(120, 160); palette.show();
        auto *scroll = palette.findChild<QScrollArea*>("toolPaletteScroll"); QVERIFY(scroll);
        auto *first = button(&palette, DrawingTool::Selection);
        auto *second = button(&palette, DrawingTool::Pan);
        QTRY_VERIFY(second->y() > first->y());
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        for (auto *candidate : palette.findChildren<QToolButton*>())
            QVERIFY(candidate->geometry().right() < scroll->viewport()->width());
        auto *last = button(&palette, DrawingTool::Text);
        scroll->ensureWidgetVisible(last);
        QTRY_VERIFY(scroll->viewport()->rect().intersects(QRect(last->mapTo(scroll->viewport(), QPoint()), last->size())));
        palette.resize(360, 180);
        QTRY_COMPARE(second->y(), first->y());
        QVERIFY(second->x() > first->x());
        QVERIFY(!palette.toolAction(DrawingTool::Text)->isEnabled());
        palette.resize(120, 160);
        QTRY_VERIFY(second->y() > first->y());
        palette.resize(180, 750);
        QTRY_COMPARE(second->y(), first->y());
        QTRY_COMPARE(scroll->verticalScrollBar()->maximum(), 0);
    }
    void panAndWheelKeepRealGeometry()
    {
        DrawingSettings settings; QVERIFY(settings.setScaleDenominator(50));
        DrawingView view(&settings); view.resize(900, 800); view.show(); QTest::qWait(10);
        const QLineF realLine(0, 0, 1000, 500);
        auto *line = view.scene()->addLine(realLine);
        QVERIFY(view.setActiveTool(DrawingTool::Pan));
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        QPoint before = view.mapFromScene({100, 100});
        drag(view, Qt::LeftButton, {60, 35});
        QPoint after = view.mapFromScene({100, 100});
        QVERIFY((after - before - QPoint(60, 35)).manhattanLength() <= 2);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        QVERIFY(view.setActiveTool(DrawingTool::Line));
        QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);
        before = view.mapFromScene({100, 100});
        drag(view, Qt::MiddleButton, {-40, 25});
        after = view.mapFromScene({100, 100});
        QVERIFY((after - before - QPoint(-40, 25)).manhattanLength() <= 2);
        QCOMPARE(view.activeTool(), DrawingTool::Line);
        const QPoint cursor(20, 30); // Include the gray margin, not only the paper.
        const QPointF fixed = view.mapToScene(cursor);
        wheel(view, cursor, 120);
        QCOMPARE(view.displayZoom(), 1.25);
        QVERIFY(QLineF(view.mapFromScene(fixed), cursor).length() <= 2);
        wheel(view, cursor, -120);
        QCOMPARE(view.displayZoom(), 1.0);
        QVERIFY(QLineF(view.mapFromScene(fixed), cursor).length() <= 2);
        QCOMPARE(line->line(), realLine); QCOMPARE(settings.scaleDenominator(), 50.0);
        QVERIFY(view.setDisplayZoom(10)); wheel(view, cursor, 120); QCOMPARE(view.displayZoom(), 10.0);
        QVERIFY(view.setDisplayZoom(0.1)); wheel(view, cursor, -120); QCOMPARE(view.displayZoom(), 0.1);
        view.resetView(); QCOMPARE(view.displayZoom(), 1.0);
        QVERIFY((view.mapFromScene({0, 0}) - view.viewport()->rect().center()).manhattanLength() <= 2);
        QCOMPARE(line->line(), realLine);
    }
    void middlePanDoesNotPickMeasurementPoints()
    {
        DrawingSettings settings; DrawingView view(&settings);
        view.resize(800, 800); view.show(); QTest::qWait(10);
        QVERIFY(view.setActiveTool(DrawingTool::Pan));
        view.startDistancePick(DrawingView::DistanceKind::Current, {-100, -100, 200, 200});
        QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);
        const QPoint firstPosition = view.mapFromScene({-30, 20});
        const QPointF first = view.mapToScene(firstPosition);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstPosition);
        QSignalSpy picked(&view, &DrawingView::distancePicked);
        drag(view, Qt::MiddleButton, {30, 20});
        QCOMPARE(picked.count(), 0); QVERIFY(view.isPickingDistance());
        QVERIFY(!view.setActiveTool(DrawingTool::Circle));
        const QPoint secondPosition = view.mapFromScene({30, 20});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, secondPosition);
        QCOMPARE(picked.count(), 1); QCOMPARE(picked.first().at(1).toPointF(), first);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
    }
    void dockModesAndPdfInteraction()
    {
        QTimer startup; cancelStartup(startup); MainWindow window; startup.stop(); window.show();
        auto *palette = window.findChild<ToolPalette*>();
        auto *dock = window.findChild<QDockWidget*>("basicToolDock");
        auto *view = window.findChild<DrawingView*>();
        auto *mode = window.findChild<QLabel*>("activeToolModeLabel");
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        QVERIFY(palette && dock && view && mode && tabs);
        QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
        const int initialItems = view->scene()->items().size();
        palette->toolAction(DrawingTool::Line)->trigger();
        QCOMPARE(view->activeTool(), DrawingTool::Line);
        QVERIFY(mode->text().contains(QString::fromUtf8("任意角度")));
        palette->toolAction(DrawingTool::Circle)->trigger();
        QVERIFY(mode->text().contains(QString::fromUtf8("半径")));
        QCOMPARE(view->scene()->items().size(), initialItems); // Palette task does not draw shapes.
        dock->setFloating(true); QTRY_VERIFY(dock->isFloating());
        dock->resize(360, 250); dock->setFloating(false); QTRY_VERIFY(!dock->isFloating());
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
        dock->close(); QVERIFY(window.isVisible()); QVERIFY(!dock->isVisible());
        dock->toggleViewAction()->trigger(); QTRY_VERIFY(dock->isVisible());
        tabs->setCurrentIndex(1); QVERIFY(!palette->isEnabled());
        tabs->setCurrentIndex(0); QVERIFY(palette->isEnabled());
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        auto *placement = window.findChild<PdfPlacement*>();
        const auto original = placement->geometry();
        palette->toolAction(DrawingTool::Pan)->trigger();
        window.findChild<QAction*>("calibratePdfAction")->trigger();
        auto *dialog = window.findChild<PdfCalibrationDialog*>(); QVERIFY(dialog);
        QVERIFY(!palette->isEnabled());
        palette->toolAction(DrawingTool::Line)->trigger();
        QCOMPARE(palette->currentTool(), DrawingTool::Pan); QCOMPARE(view->activeTool(), DrawingTool::Pan);
        dialog->findChild<QPushButton*>("measurePdfCurrentButton")->click();
        QCOMPARE(view->viewport()->cursor().shape(), Qt::CrossCursor);
        QTest::keyClick(view, Qt::Key_Escape); QVERIFY(dialog->isVisible());
        dialog->reject(); QVERIFY(palette->isEnabled());
        QCOMPARE(view->viewport()->cursor().shape(), Qt::OpenHandCursor);
        QCOMPARE(placement->sizeMm(), original.sizeMm); QCOMPARE(placement->centerMm(), original.centerMm);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            window.addDockWidget(Qt::LeftDockWidgetArea, dock);
            window.resizeDocks({dock}, {180}, Qt::Horizontal);
            QTest::qWait(20);
            QVERIFY(window.grab().save("step2-toolpalette.png"));
        }
    }
};

QTEST_MAIN(ToolPaletteTests)
#include "test_toolpalette.moc"
