#include "drawingview.h"
#include "drawingdocument.h"
#include "drawingsettings.h"
#include "mainwindow.h"
#include "toolpalette.h"
#include "pdfplacement.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTimer>
#include <cmath>
#include <limits>

class DrawingTests : public QObject {
    Q_OBJECT
private slots:
    void modelCommandsSynchronizeView() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        auto *document = view.document();
        const QUuid rect = document->addShape(QRectF(-20,-20,40,40));
        const QUuid line = document->addShape(QLineF(50,0,90,0));
        QCOMPARE(view.shapeItems().size(), 2);
        auto *rectItem = view.shapeItems()[0], *lineItem = view.shapeItems()[1];
        QCOMPARE(view.shapeId(rectItem), rect); QCOMPARE(view.shapeId(lineItem), line);
        QVERIFY(document->translateShapes({line}, {5.25,10.125}));
        QCOMPARE(lineItem->pos(), QPointF(5.25,10.125));
        QCOMPARE(qgraphicsitem_cast<QGraphicsLineItem*>(lineItem)->line(), QLineF(50,0,90,0));
        const QPoint start = view.mapFromScene({0,0});
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), start+QPoint(40,30));
        QVERIFY(rectItem->pos() != QPointF()); QCOMPARE(document->shape(rect)->positionMm, QPointF());
        // An external document command safely ends the temporary drag before deleting its item.
        QVERIFY(document->removeShapes({rect}));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start+QPoint(40,30));
        QCOMPARE(view.shapeItems().size(), 1); QCOMPARE(view.shapeItems().first(), lineItem);
        QVERIFY(view.selectedShapeItems().isEmpty()); QVERIFY(!document->shape(rect));
        QVERIFY(settings.setScaleDenominator(50)); QVERIFY(view.setDisplayZoom(2));
        QCOMPARE(view.shapeId(lineItem), line); QCOMPARE(document->shape(line)->positionMm, QPointF(5.25,10.125));
        QCOMPARE(document->shapes().size(), 1);
    }
    void clickAndShiftSelection() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-60,-20}); view.submitDrawingPoint({-20,20});
        view.submitDrawingPoint({20,-20}); view.submitDrawingPoint({60,20});
        auto *first = view.shapeItems()[0], *second = view.shapeItems()[1];
        view.setActiveTool(DrawingTool::Selection);
        auto click = [&](QPointF p, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QTest::mouseClick(view.viewport(), Qt::LeftButton, modifiers, view.mapFromScene(p));
        };
        click({-40,0}); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({first}));
        click({40,0}, Qt::ShiftModifier); QCOMPARE(view.selectedShapeItems().size(), 2);
        click({-40,0}, Qt::ShiftModifier); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({second}));
        click({0,80}, Qt::ShiftModifier); QCOMPARE(view.selectedShapeItems().size(), 1);
        click({0,80}); QVERIFY(view.selectedShapeItems().isEmpty());
        click({40,0}); QTest::keyClick(&view, Qt::Key_Escape); QVERIFY(view.selectedShapeItems().isEmpty());
        view.setActiveTool(DrawingTool::Line); view.submitDrawingPoint({-60,-20}); view.submitDrawingPoint({-20,20});
        auto *topLine = view.shapeItems().last(); view.setActiveTool(DrawingTool::Selection);
        click({-40,0}); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({topLine}));
        view.setDrawingEnabled(false); click({40,0}); QTest::keyClick(&view, Qt::Key_Delete);
        QCOMPARE(view.shapeItems().size(), 3); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({topLine}));
    }
    void polygonSelectionFollowsOutline() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Polygon);
        for (QPointF point : {QPointF(-40,-20), QPointF(40,-20), QPointF(0,40)}) view.submitDrawingPoint(point);
        QVERIFY(view.finishPolygon()); view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({35,30}));
        QVERIFY(view.selectedShapeItems().isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({0,0}));
        QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({view.shapeItems().first()}));
    }
    void groupMoveAndRollback() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-60,-20}); view.submitDrawingPoint({-20,20});
        view.setActiveTool(DrawingTool::Circle); view.submitDrawingPoint({40,0}); view.submitDrawingPoint({50,0});
        view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({-40,0}));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, view.mapFromScene({40,0}));
        const QPoint start = view.mapFromScene({-40,0}), end = start+QPoint(50,-35);
        const QPointF delta = view.mapToScene(end)-view.mapToScene(start);
        auto *rect = qgraphicsitem_cast<QGraphicsRectItem*>(view.shapeItems()[0]); const QRectF geometry = rect->rect();
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), end);
        for (const auto &shape : view.document()->shapes()) QCOMPARE(shape.positionMm, QPointF());
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QCOMPARE(rect->rect(), geometry);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(view.grab().save("step2-selection.png"));
        const QPoint moved = view.mapFromScene(QPointF(-40,0)+delta);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, moved);
        QTest::mouseMove(view.viewport(), moved+QPoint(50,40));
        QVERIFY(rect->pos() != delta); QTest::keyClick(&view, Qt::Key_Escape);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, moved+QPoint(50,40));
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QVERIFY(settings.setScaleDenominator(50)); QVERIFY(view.setDisplayZoom(2));
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QCOMPARE(rect->rect(), geometry);
        // A tool change also rolls back an unfinished move.
        const QPoint current = view.mapFromScene(QPointF(-40,0)+delta);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, current);
        QTest::mouseMove(view.viewport(), current+QPoint(60,0)); view.setActiveTool(DrawingTool::Line);
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
    }
    void screenToleranceAndMoveLimit() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Line); view.submitDrawingPoint({-100,0}); view.submitDrawingPoint({100,0});
        auto *line = view.shapeItems().first(); view.setActiveTool(DrawingTool::Selection);
        for (double scale : {1.0, 50.0}) {
            QVERIFY(settings.setScaleDenominator(scale));
            const QPoint pixel = view.mapFromScene({0,0})+QPoint(0,5);
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
            QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({line}));
            view.clearShapeSelection();
        }
        // A group move that would exceed the coordinate limit is rejected as a whole.
        QVERIFY(view.document()->translateShapes({view.shapeId(line)}, {9999900,0})); view.setContentBounds({9999700,-100,500,200}); view.centerOn({9999900,0});
        const QPoint start = view.mapFromScene({9999900,0});
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({line}));
        QTest::mouseMove(view.viewport(), start+QPoint(100,0));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start+QPoint(100,0));
        QCOMPARE(line->pos(), QPointF(9999900,0));
    }
    void deletionAndContextMenu() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Line); view.submitDrawingPoint({-60,0}); view.submitDrawingPoint({-20,0});
        view.submitDrawingPoint({20,0}); view.submitDrawingPoint({60,0});
        view.submitDrawingPoint({-60,40}); view.submitDrawingPoint({-20,40});
        const int backgroundItems = view.scene()->items().size()-3;
        view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({-40,0}));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, view.mapFromScene({-40,40}));
        QCOMPARE(view.selectedShapeItems().size(), 2);
        QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.shapeItems().size(), 1);
        QVERIFY(view.selectedShapeItems().isEmpty());
        const QPoint pixel = view.mapFromScene({40,0});
        QContextMenuEvent event(QContextMenuEvent::Mouse, pixel, view.viewport()->mapToGlobal(pixel));
        QApplication::sendEvent(view.viewport(), &event);
        auto *menu = view.findChild<QMenu*>("shapeSelectionMenu"); QVERIFY(menu && menu->isVisible());
        auto *action = menu->findChild<QAction*>("deleteSelectedShapesAction"); QVERIFY(action && action->isEnabled());
        QCOMPARE(view.selectedShapeItems().size(), 1); action->trigger(); menu->hide();
        QVERIFY(view.shapeItems().isEmpty()); QCOMPARE(view.scene()->items().size(), backgroundItems);
        QVERIFY(!view.deleteSelectedShapes());
        // Bounds and pointer lists remain valid after removal and subsequent drawing.
        view.setActiveTool(DrawingTool::Circle); view.submitDrawingPoint({0,0}); view.submitDrawingPoint({10,0});
        QCOMPARE(view.shapeItems().size(), 1); QVERIFY(settings.setScaleDenominator(100));
    }
    void pdfPickingAndPanDoNotEdit() {
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-20,-20}); view.submitDrawingPoint({20,20});
        auto *item = view.shapeItems().first(); view.setActiveTool(DrawingTool::Selection);
        const QPoint start = view.mapFromScene({0,0});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), start+QPoint(30,20));
        QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, start+QPoint(30,20));
        QCOMPARE(item->pos(), QPointF());
        view.startDistancePick(DrawingView::DistanceKind::Target, {});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({0,0}));
        QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.shapeItems().size(), 1);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({30,0}));
        QCOMPARE(item->pos(), QPointF()); QCOMPARE(view.selectedShapeItems().size(), 1);
    }
    void exactGeometryAndScale() {
        DrawingSettings settings; DrawingView view(&settings);
        QVERIFY(view.setActiveTool(DrawingTool::Line));
        QVERIFY(view.submitDrawingPoint({-100.125, 20.5}));
        QVERIFY(view.submitDrawingPoint({300.25, -50.75}));
        auto *line = qgraphicsitem_cast<QGraphicsLineItem*>(view.shapeItems().last()); QVERIFY(line);
        const QLineF expected(-100.125, 20.5, 300.25, -50.75); QCOMPARE(line->line(), expected);
        QVERIFY(view.setActiveTool(DrawingTool::Rectangle));
        QVERIFY(view.submitDrawingPoint({80, -40})); QVERIFY(view.submitDrawingPoint({-20, 60}));
        auto *rect = qgraphicsitem_cast<QGraphicsRectItem*>(view.shapeItems().last()); QVERIFY(rect);
        QCOMPARE(rect->rect(), QRectF(-20, -40, 100, 100));
        QVERIFY(view.setActiveTool(DrawingTool::Circle));
        QVERIFY(view.submitDrawingPoint({10, 20})); QVERIFY(view.submitDrawingPoint({40, 60}));
        auto *circle = qgraphicsitem_cast<QGraphicsEllipseItem*>(view.shapeItems().last()); QVERIFY(circle);
        QCOMPARE(circle->rect(), QRectF(-40, -30, 100, 100));
        QVERIFY(settings.setScaleDenominator(50)); QVERIFY(view.setDisplayZoom(2)); view.resetView();
        QCOMPARE(line->line(), expected); QCOMPARE(rect->rect(), QRectF(-20, -40, 100, 100));
        QCOMPARE(circle->rect(), QRectF(-40, -30, 100, 100)); QCOMPARE(view.shapeItems().size(), 3);
    }
    void mouseShapes_data() {
        QTest::addColumn<DrawingTool>("tool");
        QTest::newRow("line") << DrawingTool::Line;
        QTest::newRow("rectangle") << DrawingTool::Rectangle;
        QTest::newRow("circle") << DrawingTool::Circle;
    }
    void mouseShapes() {
        QFETCH(DrawingTool, tool); DrawingSettings settings; DrawingView view(&settings);
        view.resize(900,800); view.show(); QTest::qWait(10); view.setActiveTool(tool);
        const QPoint firstPixel = view.mapFromScene({-30,20}), secondPixel = view.mapFromScene({40,-20});
        const QPointF first = view.mapToScene(firstPixel), second = view.mapToScene(secondPixel);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstPixel);
        QTest::mouseMove(view.viewport(), secondPixel); QVERIFY(!view.grab().isNull());
        QVERIFY(view.shapeItems().isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, secondPixel);
        QCOMPARE(view.shapeItems().size(), 1); QCOMPARE(view.draftPointCount(), 0);
        auto *item = view.shapeItems().last();
        if (tool == DrawingTool::Line)
            QCOMPARE(qgraphicsitem_cast<QGraphicsLineItem*>(item)->line(), QLineF(first,second));
        else if (tool == DrawingTool::Rectangle)
            QCOMPARE(qgraphicsitem_cast<QGraphicsRectItem*>(item)->rect(), QRectF(first,second).normalized());
        else {
            const auto rect = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->rect();
            QCOMPARE(rect.center(), first); QVERIFY(std::abs(rect.width()-2*QLineF(first,second).length()) < 1e-9);
            QCOMPARE(rect.width(), rect.height());
        }
    }
    void invalidPointsAndCancel() {
        DrawingSettings settings; DrawingView view(&settings); view.setActiveTool(DrawingTool::Rectangle);
        QVERIFY(!view.submitDrawingPoint({std::numeric_limits<double>::quiet_NaN(), 0}));
        QVERIFY(!view.submitDrawingPoint({0, std::numeric_limits<double>::infinity()}));
        QVERIFY(!view.submitDrawingPoint({10000001, 0})); QCOMPARE(view.draftPointCount(), 0);
        QVERIFY(view.submitDrawingPoint({0, 0})); QVERIFY(!view.submitDrawingPoint({0, 20}));
        QCOMPARE(view.draftPointCount(), 1); QVERIFY(view.shapeItems().isEmpty());
        QTest::keyClick(&view, Qt::Key_Escape); QCOMPARE(view.draftPointCount(), 0);
        view.submitDrawingPoint({1, 2}); view.setActiveTool(DrawingTool::Circle); QCOMPARE(view.draftPointCount(), 0);
        view.submitDrawingPoint({1, 2}); view.setDrawingEnabled(false); QCOMPARE(view.draftPointCount(), 0);
        QVERIFY(!view.submitDrawingPoint({3, 4})); view.setDrawingEnabled(true);
        view.submitDrawingPoint({1, 2}); view.startDistancePick(DrawingView::DistanceKind::Target, {});
        QCOMPARE(view.draftPointCount(), 0); QVERIFY(!view.submitDrawingPoint({3, 4}));
        view.clearDistancePick(); QVERIFY(view.shapeItems().isEmpty());
    }
    void shiftAngles() {
        DrawingSettings settings; DrawingView view(&settings); view.setActiveTool(DrawingTool::Line);
        constexpr double pi = 3.14159265358979323846;
        for (int angle : {0,30,45,60,90,120,135,150,180,210,225,240,270,300,315,330}) {
            const double a = angle*pi/180, near = (angle+2)*pi/180;
            QVERIFY(view.submitDrawingPoint({12, -5}));
            QVERIFY(view.submitDrawingPoint({12+100*std::cos(near), -5+100*std::sin(near)}, true));
            const auto line = qgraphicsitem_cast<QGraphicsLineItem*>(view.shapeItems().last())->line();
            const QPointF delta = line.p2()-line.p1();
            QVERIFY(std::abs(delta.x()*std::sin(a)-delta.y()*std::cos(a)) < 1e-8);
            QVERIFY(delta.x()*std::cos(a)+delta.y()*std::sin(a) > 0);
        }
    }
    void polygonMouseCompletion_data() { QTest::addColumn<bool>("first"); QTest::newRow("first")<<true; QTest::newRow("last")<<false; }
    void polygonMouseCompletion() {
        QFETCH(bool, first); DrawingSettings settings; DrawingView view(&settings);
        view.resize(900,800); view.show(); QTest::qWait(10); view.setActiveTool(DrawingTool::Polygon);
        QPolygonF expected;
        for (QPointF point : {QPointF(-30,-20), QPointF(40,-20), QPointF(20,30)}) {
            const QPoint pixel = view.mapFromScene(point); expected.append(view.mapToScene(pixel));
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        }
        QCOMPARE(view.draftPointCount(), 3); QVERIFY(view.canFinishPolygon());
        const QPoint pixel = view.mapFromScene(first ? expected.first() : expected.last());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QVERIFY(view.shapeItems().isEmpty()); QCOMPARE(view.draftPointCount(), 3);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QCOMPARE(view.shapeItems().size(), 1); QCOMPARE(view.draftPointCount(), 0);
        auto *polygon = qgraphicsitem_cast<QGraphicsPolygonItem*>(view.shapeItems().last()); QVERIFY(polygon);
        QCOMPARE(polygon->polygon(), expected);
        QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.shapeItems().size(), 1);
    }
    void polygonUndoAndDegenerate() {
        DrawingSettings settings; DrawingView view(&settings); view.setActiveTool(DrawingTool::Polygon);
        for (QPointF point : {QPointF(0,0), QPointF(10,0), QPointF(20,0)}) QVERIFY(view.submitDrawingPoint(point));
        QVERIFY(!view.finishPolygon()); QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.draftPointCount(), 2);
        QVERIFY(!view.submitDrawingPoint({0,0})); QVERIFY(view.submitDrawingPoint({20,20}));
        QVERIFY(view.finishPolygon()); QCOMPARE(view.shapeItems().size(), 1);
    }
    void numericUiAndMixedInput() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer startup; connect(&startup, &QTimer::timeout, [] {
            for (auto *w : QApplication::topLevelWidgets())
                if (auto *d = qobject_cast<QFileDialog*>(w); d && d->isVisible()) d->reject();
        }); startup.start(20); MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        auto *view = window.findChild<DrawingView*>(); auto *palette = window.findChild<ToolPalette*>();
        auto *x = window.findChild<QDoubleSpinBox*>("drawingXInput");
        auto *y = window.findChild<QDoubleSpinBox*>("drawingYInput");
        auto *add = window.findChild<QPushButton*>("submitDrawingPointButton");
        auto *finish = window.findChild<QPushButton*>("finishPolygonButton"); QVERIFY(view && palette && x && y && add && finish);
        palette->toolAction(DrawingTool::Line)->trigger();
        const QPoint pixel = view->mapFromScene({-10,20}); const QPointF first = view->mapToScene(pixel);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QCOMPARE(view->draftPointCount(), 1); x->setValue(123.456789); y->setValue(-98.765432); add->click();
        QCOMPARE(view->shapeItems().size(), 1);
        auto *line = qgraphicsitem_cast<QGraphicsLineItem*>(view->shapeItems().last());
        QCOMPARE(line->line(), QLineF(first, {123.456789,-98.765432}));
        palette->toolAction(DrawingTool::Polygon)->trigger(); QVERIFY(!finish->isEnabled());
        for (QPointF point : {QPointF(-20,-20), QPointF(20,-20), QPointF(0,30)}) {
            x->setValue(point.x()); y->setValue(point.y()); add->click();
        }
        QVERIFY(finish->isEnabled()); finish->click(); QCOMPARE(view->shapeItems().size(), 2);
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        auto *pdf = window.findChild<PdfPlacement*>(); QVERIFY(pdf->calibrate(10,20,{0,0}));
        QCOMPARE(line->line(), QLineF(first, {123.456789,-98.765432}));
        view->submitDrawingPoint({0,0}); window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(1);
        QCOMPARE(view->draftPointCount(), 0); QVERIFY(!add->isEnabled());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) {
            window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            QVERIFY(window.grab().save("step2-drawing.png"));
        }
    }
};
QTEST_MAIN(DrawingTests)
#include "test_drawing.moc"
