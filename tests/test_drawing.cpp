/**
 * @file test_drawing.cpp
 * @brief 図形の描画、選択、移動、削除とモデル・表示の同期を検証する。
 */

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

/// @brief 描画と選択編集、およびモデルと表示の同期を検証する。
class DrawingTests : public QObject {
    Q_OBJECT
private slots:
    /// @brief モデル側の変更が表示へ反映され、ドラッグ中の削除でも参照が残らないことを検証する。
    void modelCommandsSynchronizeView() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        // document: 確定済み図形のID・形状・配置を管理する共通モデル。
        auto *document = view.document();
        // rect: 共通モデルへ追加した図形のUUID。
        const QUuid rect = document->addShape(QRectF(-20,-20,40,40));
        // line: 共通モデルへ追加した図形のUUID。
        const QUuid line = document->addShape(QLineF(50,0,90,0));
        QCOMPARE(view.shapeItems().size(), 2);
        // rectItem: 矩形に対応する描画アイテム。シーンが所有する。
        // lineItem: 直線に対応する描画アイテム。シーンが所有する。
        auto *rectItem = view.shapeItems()[0], *lineItem = view.shapeItems()[1];
        QCOMPARE(view.shapeId(rectItem), rect); QCOMPARE(view.shapeId(lineItem), line);
        QVERIFY(document->translateShapes({line}, {5.25,10.125}));
        QCOMPARE(lineItem->pos(), QPointF(5.25,10.125));
        QCOMPARE(qgraphicsitem_cast<QGraphicsLineItem*>(lineItem)->line(), QLineF(50,0,90,0));
        // start: 入力・表示位置を比較する画面座標（px）。
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
    /// @brief 単独選択、Shift追加・解除、空白クリック、重なり、編集禁止時の挙動を検証する。
    void clickAndShiftSelection() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-60,-20}); view.submitDrawingPoint({-20,20});
        view.submitDrawingPoint({20,-20}); view.submitDrawingPoint({60,20});
        // first: 始点・最初の頂点、または比較する1つ目の対象。
        // second: 終点・次の頂点、または比較する2つ目の対象。
        auto *first = view.shapeItems()[0], *second = view.shapeItems()[1];
        view.setActiveTool(DrawingTool::Selection);
        // click: 指定位置へのクリック操作をまとめるテスト用コールバック。
        // p: クリック補助へ渡す指定点の実寸座標（mm）。
        // modifiers: クリックに付加するShift等の修飾キー。
        // コールバック: 実寸座標を画面位置へ変換し、指定した修飾キー付きでクリックする。
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
        // topLine: 重なった矩形より手前にある、選択対象の直線アイテム。
        auto *topLine = view.shapeItems().last(); view.setActiveTool(DrawingTool::Selection);
        click({-40,0}); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({topLine}));
        view.setDrawingEnabled(false); click({40,0}); QTest::keyClick(&view, Qt::Key_Delete);
        QCOMPARE(view.shapeItems().size(), 3); QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({topLine}));
    }
    /// @brief 多角形の輪郭に沿った選択判定と、外接矩形だけでは選ばれないことを検証する。
    void polygonSelectionFollowsOutline() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Polygon);
        // point: 用紙中心基準の実寸座標（mm）。
        for (QPointF point : {QPointF(-40,-20), QPointF(40,-20), QPointF(0,40)}) view.submitDrawingPoint(point);
        QVERIFY(view.finishPolygon()); view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({35,30}));
        QVERIFY(view.selectedShapeItems().isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({0,0}));
        QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({view.shapeItems().first()}));
    }
    /// @brief 複数図形の相対位置を保った移動、確定、取消、実寸維持を検証する。
    void groupMoveAndRollback() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-60,-20}); view.submitDrawingPoint({-20,20});
        view.setActiveTool(DrawingTool::Circle); view.submitDrawingPoint({40,0}); view.submitDrawingPoint({50,0});
        view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({-40,0}));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, view.mapFromScene({40,0}));
        // start: 入力・表示位置を比較する画面座標（px）。
        // end: ドラッグの解放位置または移動イベントの送り先（画面px）。
        const QPoint start = view.mapFromScene({-40,0}), end = start+QPoint(50,-35);
        // delta: 開始位置から加える移動量（実寸mm）。
        const QPointF delta = view.mapToScene(end)-view.mapToScene(start);
        // rect: 処理対象の矩形形状またはその描画アイテム。
        // geometry: 寸法や頂点が変わっていないか比較する形状データ。
        auto *rect = qgraphicsitem_cast<QGraphicsRectItem*>(view.shapeItems()[0]); const QRectF geometry = rect->rect();
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), end);
        // shape: 描画順に処理する確定済み図形データ。
        for (const auto &shape : view.document()->shapes()) QCOMPARE(shape.positionMm, QPointF());
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QCOMPARE(rect->rect(), geometry);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(view.grab().save("step2-selection.png"));
        // moved: 入力・表示位置を比較する画面座標（px）。
        const QPoint moved = view.mapFromScene(QPointF(-40,0)+delta);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, moved);
        QTest::mouseMove(view.viewport(), moved+QPoint(50,40));
        QVERIFY(rect->pos() != delta); QTest::keyClick(&view, Qt::Key_Escape);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, moved+QPoint(50,40));
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QVERIFY(settings.setScaleDenominator(50)); QVERIFY(view.setDisplayZoom(2));
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
        QCOMPARE(rect->rect(), geometry);
        // A tool change also rolls back an unfinished move.
        // current: 入力・表示位置を比較する画面座標（px）。
        const QPoint current = view.mapFromScene(QPointF(-40,0)+delta);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, current);
        QTest::mouseMove(view.viewport(), current+QPoint(60,0)); view.setActiveTool(DrawingTool::Line);
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        for (auto *item : view.shapeItems()) {
            QCOMPARE(item->pos(), delta);
            QCOMPARE(view.document()->shape(view.shapeId(item))->positionMm, delta);
        }
    }
    /// @brief ズームに依存しない選択許容幅と、座標上限を超える移動の拒否を検証する。
    void screenToleranceAndMoveLimit() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Line); view.submitDrawingPoint({-100,0}); view.submitDrawingPoint({100,0});
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = view.shapeItems().first(); view.setActiveTool(DrawingTool::Selection);
        // scale: 選択幅や寸法維持を検証する縮尺分母。
        for (double scale : {1.0, 50.0}) {
            QVERIFY(settings.setScaleDenominator(scale));
            // pixel: 入力・表示位置を比較する画面座標（px）。
            const QPoint pixel = view.mapFromScene({0,0})+QPoint(0,5);
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
            QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({line}));
            view.clearShapeSelection();
        }
        // A group move that would exceed the coordinate limit is rejected as a whole.
        QVERIFY(view.document()->translateShapes({view.shapeId(line)}, {9999900,0})); view.setContentBounds({9999700,-100,500,200}); view.centerOn({9999900,0});
        // start: 入力・表示位置を比較する画面座標（px）。
        const QPoint start = view.mapFromScene({9999900,0});
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(view.selectedShapeItems(), QList<QGraphicsItem*>({line}));
        QTest::mouseMove(view.viewport(), start+QPoint(100,0));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start+QPoint(100,0));
        QCOMPARE(line->pos(), QPointF(9999900,0));
    }
    /// @brief Delと右クリックによる削除、選択解除、再描画時の参照と範囲を検証する。
    void deletionAndContextMenu() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Line); view.submitDrawingPoint({-60,0}); view.submitDrawingPoint({-20,0});
        view.submitDrawingPoint({20,0}); view.submitDrawingPoint({60,0});
        view.submitDrawingPoint({-60,40}); view.submitDrawingPoint({-20,40});
        // backgroundItems: 図形以外のシーンアイテム数。削除で用紙等が失われないことを確かめる。
        const int backgroundItems = view.scene()->items().size()-3;
        view.setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, view.mapFromScene({-40,0}));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, view.mapFromScene({-40,40}));
        QCOMPARE(view.selectedShapeItems().size(), 2);
        QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.shapeItems().size(), 1);
        QVERIFY(view.selectedShapeItems().isEmpty());
        // pixel: 入力・表示位置を比較する画面座標（px）。
        const QPoint pixel = view.mapFromScene({40,0});
        // event: Qtから渡された入力または画面更新のイベント。
        QContextMenuEvent event(QContextMenuEvent::Mouse, pixel, view.viewport()->mapToGlobal(pixel));
        QApplication::sendEvent(view.viewport(), &event);
        // menu: 右クリックで開いた図形削除メニュー。
        auto *menu = view.findChild<QMenu*>("shapeSelectionMenu"); QVERIFY(menu && menu->isVisible());
        // action: 画面やツールの操作を表すアクション。所有権は所属するQtオブジェクトにある。
        auto *action = menu->findChild<QAction*>("deleteSelectedShapesAction"); QVERIFY(action && action->isEnabled());
        QCOMPARE(view.selectedShapeItems().size(), 1); action->trigger(); menu->hide();
        QVERIFY(view.shapeItems().isEmpty()); QCOMPARE(view.scene()->items().size(), backgroundItems);
        QVERIFY(!view.deleteSelectedShapes());
        // Bounds and pointer lists remain valid after removal and subsequent drawing.
        view.setActiveTool(DrawingTool::Circle); view.submitDrawingPoint({0,0}); view.submitDrawingPoint({10,0});
        QCOMPARE(view.shapeItems().size(), 1); QVERIFY(settings.setScaleDenominator(100));
    }
    /// @brief パンやPDF距離測定が、図形の移動・削除として扱われないことを検証する。
    void pdfPickingAndPanDoNotEdit() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        view.setActiveTool(DrawingTool::Rectangle); view.submitDrawingPoint({-20,-20}); view.submitDrawingPoint({20,20});
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        auto *item = view.shapeItems().first(); view.setActiveTool(DrawingTool::Selection);
        // start: 入力・表示位置を比較する画面座標（px）。
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
    /// @brief 4種類のうち2点で確定する図形の実寸形状と、縮尺・ズームからの独立性を検証する。
    void exactGeometryAndScale() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings);
        QVERIFY(view.setActiveTool(DrawingTool::Line));
        QVERIFY(view.submitDrawingPoint({-100.125, 20.5}));
        QVERIFY(view.submitDrawingPoint({300.25, -50.75}));
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = qgraphicsitem_cast<QGraphicsLineItem*>(view.shapeItems().last()); QVERIFY(line);
        // expected: 描画・頂点順・座標の比較に使う期待値。
        const QLineF expected(-100.125, 20.5, 300.25, -50.75); QCOMPARE(line->line(), expected);
        QVERIFY(view.setActiveTool(DrawingTool::Rectangle));
        QVERIFY(view.submitDrawingPoint({80, -40})); QVERIFY(view.submitDrawingPoint({-20, 60}));
        // rect: 処理対象の矩形形状またはその描画アイテム。
        auto *rect = qgraphicsitem_cast<QGraphicsRectItem*>(view.shapeItems().last()); QVERIFY(rect);
        QCOMPARE(rect->rect(), QRectF(-20, -40, 100, 100));
        QVERIFY(view.setActiveTool(DrawingTool::Circle));
        QVERIFY(view.submitDrawingPoint({10, 20})); QVERIFY(view.submitDrawingPoint({40, 60}));
        // circle: 対象の円形状またはその描画アイテム。
        auto *circle = qgraphicsitem_cast<QGraphicsEllipseItem*>(view.shapeItems().last()); QVERIFY(circle);
        QCOMPARE(circle->rect(), QRectF(-40, -30, 100, 100));
        QVERIFY(settings.setScaleDenominator(50)); QVERIFY(view.setDisplayZoom(2)); view.resetView();
        QCOMPARE(line->line(), expected); QCOMPARE(rect->rect(), QRectF(-20, -40, 100, 100));
        QCOMPARE(circle->rect(), QRectF(-40, -30, 100, 100)); QCOMPARE(view.shapeItems().size(), 3);
    }
    /// @brief 2点クリックで作る直線・四角・円をテスト行として用意する。
    void mouseShapes_data() {
        QTest::addColumn<DrawingTool>("tool");
        QTest::newRow("line") << DrawingTool::Line;
        QTest::newRow("rectangle") << DrawingTool::Rectangle;
        QTest::newRow("circle") << DrawingTool::Circle;
    }
    /// @brief クリック位置、プレビュー、確定後の各形状を検証する。
    void mouseShapes() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        // tool: テスト行から取得する値。現在処理するツールの種類。
        QFETCH(DrawingTool, tool); DrawingSettings settings; DrawingView view(&settings);
        view.resize(900,800); view.show(); QTest::qWait(10); view.setActiveTool(tool);
        // firstPixel: 入力・表示位置を比較する画面座標（px）。
        // secondPixel: 2点目をクリックする画面位置（px）。
        const QPoint firstPixel = view.mapFromScene({-30,20}), secondPixel = view.mapFromScene({40,-20});
        // first: 用紙中心基準の実寸座標（mm）。
        // second: 終点・次の頂点、または比較する2つ目の対象。
        const QPointF first = view.mapToScene(firstPixel), second = view.mapToScene(secondPixel);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstPixel);
        QTest::mouseMove(view.viewport(), secondPixel); QVERIFY(!view.grab().isNull());
        QVERIFY(view.shapeItems().isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, secondPixel);
        QCOMPARE(view.shapeItems().size(), 1); QCOMPARE(view.draftPointCount(), 0);
        // item: 現在処理する図形の表示アイテム、またはツールの定義。
        auto *item = view.shapeItems().last();
        if (tool == DrawingTool::Line)
            QCOMPARE(qgraphicsitem_cast<QGraphicsLineItem*>(item)->line(), QLineF(first,second));
        else if (tool == DrawingTool::Rectangle)
            QCOMPARE(qgraphicsitem_cast<QGraphicsRectItem*>(item)->rect(), QRectF(first,second).normalized());
        else {
            // rect: 処理対象の矩形形状またはその描画アイテム。
            const auto rect = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->rect();
            QCOMPARE(rect.center(), first); QVERIFY(std::abs(rect.width()-2*QLineF(first,second).length()) < 1e-9);
            QCOMPARE(rect.width(), rect.height());
        }
    }
    /// @brief 不正座標・大きさ0の拒否と、Esc・ツール切り替え・測定による取消を検証する。
    void invalidPointsAndCancel() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
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
    /// @brief 全象限の許可角度へShift制限されることを検証する。
    void shiftAngles() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.setActiveTool(DrawingTool::Line);
        // pi: 度とラジアンの変換に使う円周率。
        constexpr double pi = 3.14159265358979323846;
        // angle: 全象限で検証する許可角度（度）。
        for (int angle : {0,30,45,60,90,120,135,150,180,210,225,240,270,300,315,330}) {
            // a: 検証対象の許可方向角（ラジアン）。
            // near: 許可角度から少しずらした試験入力の方向（ラジアン）。
            const double a = angle*pi/180, near = (angle+2)*pi/180;
            QVERIFY(view.submitDrawingPoint({12, -5}));
            QVERIFY(view.submitDrawingPoint({12+100*std::cos(near), -5+100*std::sin(near)}, true));
            // line: 対象の直線形状またはその描画アイテム。
            const auto line = qgraphicsitem_cast<QGraphicsLineItem*>(view.shapeItems().last())->line();
            // delta: 開始位置から加える移動量（実寸mm）。
            const QPointF delta = line.p2()-line.p1();
            QVERIFY(std::abs(delta.x()*std::sin(a)-delta.y()*std::cos(a)) < 1e-8);
            QVERIFY(delta.x()*std::cos(a)+delta.y()*std::sin(a) > 0);
        }
    }
    /// @brief 多角形を始点または終点で完了する2つのケースを用意する。
    void polygonMouseCompletion_data() { QTest::addColumn<bool>("first"); QTest::newRow("first")<<true; QTest::newRow("last")<<false; }
    /// @brief ダブルクリックによる閉鎖、頂点順、余分な点を作らないことを検証する。
    void polygonMouseCompletion() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        // first: テスト行から取得する値。始点・最初の頂点、または比較する1つ目の対象。
        QFETCH(bool, first); DrawingSettings settings; DrawingView view(&settings);
        view.resize(900,800); view.show(); QTest::qWait(10); view.setActiveTool(DrawingTool::Polygon);
        // expected: 描画・頂点順・座標の比較に使う期待値。
        QPolygonF expected;
        // point: 用紙中心基準の実寸座標（mm）。
        for (QPointF point : {QPointF(-30,-20), QPointF(40,-20), QPointF(20,30)}) {
            // pixel: 入力・表示位置を比較する画面座標（px）。
            const QPoint pixel = view.mapFromScene(point); expected.append(view.mapToScene(pixel));
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        }
        QCOMPARE(view.draftPointCount(), 3); QVERIFY(view.canFinishPolygon());
        // pixel: 入力・表示位置を比較する画面座標（px）。
        const QPoint pixel = view.mapFromScene(first ? expected.first() : expected.last());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QVERIFY(view.shapeItems().isEmpty()); QCOMPARE(view.draftPointCount(), 3);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QCOMPARE(view.shapeItems().size(), 1); QCOMPARE(view.draftPointCount(), 0);
        // polygon: 頂点順を保持した多角形の形状または描画アイテム。
        auto *polygon = qgraphicsitem_cast<QGraphicsPolygonItem*>(view.shapeItems().last()); QVERIFY(polygon);
        QCOMPARE(polygon->polygon(), expected);
        QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.shapeItems().size(), 1);
    }
    /// @brief 一直線の多角形を拒否し、Delで頂点を戻して作成できることを検証する。
    void polygonUndoAndDegenerate() {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings); view.setActiveTool(DrawingTool::Polygon);
        // point: 用紙中心基準の実寸座標（mm）。
        for (QPointF point : {QPointF(0,0), QPointF(10,0), QPointF(20,0)}) QVERIFY(view.submitDrawingPoint(point));
        QVERIFY(!view.finishPolygon()); QTest::keyClick(&view, Qt::Key_Delete); QCOMPARE(view.draftPointCount(), 2);
        QVERIFY(!view.submitDrawingPoint({0,0})); QVERIFY(view.submitDrawingPoint({20,20}));
        QVERIFY(view.finishPolygon()); QCOMPARE(view.shapeItems().size(), 1);
    }
    /// @brief 数値とマウスの混在入力、完了ボタン、PDF補正とタブ切り替えを検証する。
    void numericUiAndMixedInput() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のPDF選択へ自動応答するタイマー。
        // コールバック: 起動時のファイル選択をキャンセルして、空の作業領域を検証する。
        QTimer startup; connect(&startup, &QTimer::timeout, [] {
            // w: スタック上のメイン画面。イベントループ終了後に破棄する。
            for (auto *w : QApplication::topLevelWidgets())
                // d: 起動時の選択をキャンセルするために見つけたファイルダイアログ。
                if (auto *d = qobject_cast<QFileDialog*>(w); d && d->isVisible()) d->reject();
        // window: テストで操作するメイン画面。スコープ終了時に破棄する。
        }); startup.start(20); MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        // palette: 選択ツール、排他性、ドック配置を検証する基本パレット。
        auto *view = window.findChild<DrawingView*>(); auto *palette = window.findChild<ToolPalette*>();
        // x: 実寸X座標を数値入力するスピンボックス。
        auto *x = window.findChild<QDoubleSpinBox*>("drawingXInput");
        // y: 実寸Y座標を数値入力するスピンボックス。
        auto *y = window.findChild<QDoubleSpinBox*>("drawingYInput");
        // add: 数値座標を1点として渡す点指定ボタン。
        auto *add = window.findChild<QPushButton*>("submitDrawingPointButton");
        // finish: 数値入力から多角形を閉じる完了ボタン。
        auto *finish = window.findChild<QPushButton*>("finishPolygonButton"); QVERIFY(view && palette && x && y && add && finish);
        palette->toolAction(DrawingTool::Line)->trigger();
        // pixel: 入力・表示位置を比較する画面座標（px）。
        // first: 用紙中心基準の実寸座標（mm）。
        const QPoint pixel = view->mapFromScene({-10,20}); const QPointF first = view->mapToScene(pixel);
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, pixel);
        QCOMPARE(view->draftPointCount(), 1); x->setValue(123.456789); y->setValue(-98.765432); add->click();
        QCOMPARE(view->shapeItems().size(), 1);
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = qgraphicsitem_cast<QGraphicsLineItem*>(view->shapeItems().last());
        QCOMPARE(line->line(), QLineF(first, {123.456789,-98.765432}));
        palette->toolAction(DrawingTool::Polygon)->trigger(); QVERIFY(!finish->isEnabled());
        // point: 用紙中心基準の実寸座標（mm）。
        for (QPointF point : {QPointF(-20,-20), QPointF(20,-20), QPointF(0,30)}) {
            x->setValue(point.x()); y->setValue(point.y()); add->click();
        }
        QVERIFY(finish->isEnabled()); finish->click(); QCOMPARE(view->shapeItems().size(), 2);
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        // pdf: 取り込み済みPDFの配置モデル。
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
