/**
 * @file test_toolpalette.cpp
 * @brief ツール選択、パレット配置、画面移動、ズーム、PDF測定との連携を検証する。
 */

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
/// @brief 指定ツールに結び付くパレットのボタンを探す。
/// @param palette 選択ツール、排他性、ドック配置を検証する基本パレット。
/// @param tool 現在処理するツールの種類。
QToolButton *button(ToolPalette *palette, DrawingTool tool)
{
    // candidate: 比較対象の許可角度またはツールボタン。
    for (auto *candidate : palette->findChildren<QToolButton*>())
        if (candidate->defaultAction() == palette->toolAction(tool)) return candidate;
    return nullptr;
}
/// @brief 起動時に開くファイル選択ダイアログをタイマーでキャンセルする。
/// @param timer ダイアログへの応答をイベントループから繰り返し試みるタイマー。
void cancelStartup(QTimer &timer)
{
    // コールバック: 起動時のファイル選択をキャンセルし、パレットの操作テストへ進める。
    QObject::connect(&timer, &QTimer::timeout, &timer, [] {
        // widget: トップレベル画面を走査して見つけたウィジェット。所有権は取得しない。
        for (auto *widget : QApplication::topLevelWidgets())
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
    });
    timer.start(20);
}
/// @brief ビュー上で指定ボタンのドラッグ操作を再現する。
/// @param view 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
/// @param mouseButton ドラッグに使用する左または中ボタン。
/// @param delta 直前の押下・移動位置からの画面移動量（px）。
void drag(DrawingView &view, Qt::MouseButton mouseButton, QPoint delta)
{
    // start: 入力・表示位置を比較する画面座標（px）。
    const QPoint start = view.viewport()->rect().center();
    QTest::mousePress(view.viewport(), mouseButton, Qt::NoModifier, start);
    QTest::mouseMove(view.viewport(), start + delta);
    QTest::mouseRelease(view.viewport(), mouseButton, Qt::NoModifier, start + delta);
}
/// @brief 指定位置と回転量のホイールイベントをビューへ送る。
/// @param view 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
/// @param position 入力・表示位置を比較する画面座標（px）。
/// @param angleDelta ホイールへ送る回転量。120で通常1段階分。
void wheel(DrawingView &view, QPoint position, int angleDelta)
{
    // event: Qtから渡された入力または画面更新のイベント。
    QWheelEvent event(QPointF(position), QPointF(view.viewport()->mapToGlobal(position)),
        {}, {0, angleDelta}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
    QVERIFY(event.isAccepted());
}
}

/// @brief パレットとビュー操作の回帰テストをまとめる。
class ToolPaletteTests : public QObject
{
    Q_OBJECT
private slots:
    /// @brief ネイティブではないファイル選択ダイアログを使って操作を検証できるようにする。
    void initTestCase() { QApplication::setAttribute(Qt::AA_DontUseNativeDialogs); }
    /// @brief ツール選択の排他性と、未対応ツールが選べないことを検証する。
    void exclusiveToolsAndDisabledFutureTools()
    {
        // palette: 選択ツール、排他性、ドック配置を検証する基本パレット。
        ToolPalette palette; palette.resize(260, 400); palette.show();
        QCOMPARE(palette.currentTool(), DrawingTool::Selection);
        // changed: toolChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changed(&palette, &ToolPalette::toolChanged);
        // tool: 現在処理するツールの種類。
        for (DrawingTool tool : {DrawingTool::Pan, DrawingTool::Line, DrawingTool::Rectangle,
                                 DrawingTool::Circle, DrawingTool::Polygon, DrawingTool::Selection}) {
            // target: 補正後の距離、対応する入力欄、または操作対象のボタン。
            auto *target = button(&palette, tool); QVERIFY(target);
            QVERIFY(!target->icon().isNull());
            QTest::mouseClick(target, Qt::LeftButton);
            QCOMPARE(palette.currentTool(), tool);
            // checked: 押下状態になっているボタン数。排他選択の検証に使う。
            int checked = 0;
            // candidate: 比較対象の許可角度またはツールボタン。
            for (auto *candidate : palette.findChildren<QToolButton*>()) checked += candidate->isChecked();
            QCOMPARE(checked, 1);
        }
        QCOMPARE(changed.count(), 6);
        QTest::mouseClick(button(&palette, DrawingTool::Selection), Qt::LeftButton);
        QCOMPARE(changed.count(), 6);
        // future: 未対応で無効になっていることを確認する後続ツール。
        for (DrawingTool future : {DrawingTool::Reshape, DrawingTool::Join, DrawingTool::Dimension, DrawingTool::Text}) {
            QVERIFY(!palette.toolAction(future)->isEnabled());
            QVERIFY(!palette.setCurrentTool(future));
            palette.toolAction(future)->trigger();
            QCOMPARE(palette.currentTool(), DrawingTool::Selection);
        }
        QCOMPARE(changed.count(), 6);
    }
    /// @brief パレットの幅に応じた折り返しと縦スクロールを検証する。
    void responsiveRowsAndScrolling()
    {
        // palette: 選択ツール、排他性、ドック配置を検証する基本パレット。
        ToolPalette palette; palette.resize(120, 160); palette.show();
        // scroll: パレットのスクロール可能範囲を検証するスクロール領域。
        auto *scroll = palette.findChild<QScrollArea*>("toolPaletteScroll"); QVERIFY(scroll);
        // first: 並び順と折り返しを比較するツールボタン。
        auto *first = button(&palette, DrawingTool::Selection);
        // second: 並び順と折り返しを比較するツールボタン。
        auto *second = button(&palette, DrawingTool::Pan);
        QTRY_VERIFY(second->y() > first->y());
        QVERIFY(scroll->verticalScrollBar()->maximum() > 0);
        // candidate: 比較対象の許可角度またはツールボタン。
        for (auto *candidate : palette.findChildren<QToolButton*>())
            QVERIFY(candidate->geometry().right() < scroll->viewport()->width());
        // last: スクロールで到達できることを確認する最後のツールボタン。
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
    /// @brief パンとカーソル中心ズームで実寸形状が変化しないことを検証する。
    void panAndWheelKeepRealGeometry()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings; QVERIFY(settings.setScaleDenominator(50));
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingView view(&settings); view.resize(900, 800); view.show(); QTest::qWait(10);
        // realLine: 表示操作で変わらないことを確かめる元の線分（実寸mm）。
        const QLineF realLine(0, 0, 1000, 500);
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = view.scene()->addLine(realLine);
        QVERIFY(view.setActiveTool(DrawingTool::Pan));
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
        // before: 入力・表示位置を比較する画面座標（px）。
        QPoint before = view.mapFromScene({100, 100});
        drag(view, Qt::LeftButton, {60, 35});
        // after: 入力・表示位置を比較する画面座標（px）。
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
        // cursor: 入力・表示位置を比較する画面座標（px）。
        const QPoint cursor(20, 30); // Include the gray margin, not only the paper.
        // fixed: 補正ダイアログから得た固定点。未測定なら空。
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
    /// @brief 測定中の中ボタンドラッグが測定点を追加せず、カーソル状態も戻ることを検証する。
    void middlePanDoesNotPickMeasurementPoints()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings);
        view.resize(800, 800); view.show(); QTest::qWait(10);
        QVERIFY(view.setActiveTool(DrawingTool::Pan));
        view.startDistancePick(DrawingView::DistanceKind::Current, {-100, -100, 200, 200});
        QCOMPARE(view.viewport()->cursor().shape(), Qt::CrossCursor);
        // firstPosition: 入力・表示位置を比較する画面座標（px）。
        const QPoint firstPosition = view.mapFromScene({-30, 20});
        // first: 用紙中心基準の実寸座標（mm）。
        const QPointF first = view.mapToScene(firstPosition);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, firstPosition);
        // picked: distancePickedの通知回数と引数を記録するシグナル監視。
        QSignalSpy picked(&view, &DrawingView::distancePicked);
        drag(view, Qt::MiddleButton, {30, 20});
        QCOMPARE(picked.count(), 0); QVERIFY(view.isPickingDistance());
        QVERIFY(!view.setActiveTool(DrawingTool::Circle));
        // secondPosition: 入力・表示位置を比較する画面座標（px）。
        const QPoint secondPosition = view.mapFromScene({30, 20});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, secondPosition);
        QCOMPARE(picked.count(), 1); QCOMPARE(picked.first().at(1).toPointF(), first);
        QCOMPARE(view.viewport()->cursor().shape(), Qt::OpenHandCursor);
    }
    /// @brief ドック操作、モード表示、再表示とPDF補正中の操作制限を検証する。
    void dockModesAndPdfInteraction()
    {
        // startup: 起動時のPDF選択へ自動応答するタイマー。
        // window: テストで操作するメイン画面。スコープ終了時に破棄する。
        QTimer startup; cancelStartup(startup); MainWindow window; startup.stop(); window.show();
        // palette: 選択ツール、排他性、ドック配置を検証する基本パレット。
        auto *palette = window.findChild<ToolPalette*>();
        // dock: 切り離し・配置変更・再表示を検証する基本パレットのドック。
        auto *dock = window.findChild<QDockWidget*>("basicToolDock");
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        auto *view = window.findChild<DrawingView*>();
        // mode: ツールの選択モード表示。
        auto *mode = window.findChild<QLabel*>("activeToolModeLabel");
        // tabs: CAD作業領域とPDF参照を切り替えるタブ。
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        QVERIFY(palette && dock && view && mode && tabs);
        QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
        // initialItems: ツール切り替え前のシーンアイテム数。
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
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        auto *placement = window.findChild<PdfPlacement*>();
        // original: 補正または操作前の配置データ。
        const auto original = placement->geometry();
        palette->toolAction(DrawingTool::Pan)->trigger();
        window.findChild<QAction*>("calibratePdfAction")->trigger();
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
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
