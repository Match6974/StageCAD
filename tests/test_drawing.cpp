#include "drawingview.h"
#include "drawingsettings.h"
#include "mainwindow.h"
#include "toolpalette.h"
#include "pdfplacement.h"
#include <QAction>
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
