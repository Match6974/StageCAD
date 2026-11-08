/**
 * @file shutdown_probe.cpp
 * @brief 実際のイベントループで各操作状態からの終了・オブジェクト破棄を検証する。
 */

#include "mainwindow.h"
#include "drawingview.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "pdfcalibrationdialog.h"
#include "pdfplacement.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QDockWidget>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QTabWidget>
#include <QTimer>
#include <cstdio>

// Exercise the application's real event loop and stack-allocated MainWindow,
// including destruction after exec() returns (as in main.cpp).
/// @brief Qtのイベントループを開始し、終了後にスタック上のメイン画面を破棄する。
/// @param argc コマンドライン引数の数。Qtアプリケーションへ渡す。
/// @param argv コマンドライン引数の文字列配列。Qtアプリケーションへ渡す。
int main(int argc, char **argv)
{
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    // app: イベントループとトップレベルウィジェットを管理するQtアプリケーション。
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    // scenario: コマンドラインで指定された終了時の操作状態。
    const QString scenario = QString::fromLocal8Bit(argv[1]);
    // startup: 起動時のPDF選択へ自動応答するタイマー。
    QTimer startup;
    // コールバック: 起動ダイアログへシナリオに応じてPDFパスまたはキャンセルを返す。
    QObject::connect(&startup, &QTimer::timeout, [&] {
        // widget: トップレベル画面を走査して見つけたウィジェット。所有権は取得しない。
        for (auto *widget : QApplication::topLevelWidgets()) {
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (scenario == "empty") dialog->reject();
            // input: 数値やファイル名の入力欄。所有権は所属する画面にある。
            else if (auto *input = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                input->setText(QString::fromUtf8(SAMPLE_PDF_PATH));
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    startup.start(20);
    // w: スタック上のメイン画面。イベントループ終了後に破棄する。
    MainWindow w;
    startup.stop();
    w.show();
    // コールバック: 画面表示後に終了テストの操作状態を作り、実際の閉じる処理を呼ぶ。
    QTimer::singleShot(0, &w, [&] {
        // hasPdf: 今回の終了シナリオでPDFが読み込まれる予定かどうか。
        const bool hasPdf = scenario != "empty";
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        auto *placement = w.findChild<PdfPlacement*>();
        if (!placement || placement->hasPage() != hasPdf) { app.exit(3); return; }
        if (scenario == "calibrated") {
            if (!placement->calibrate(10, 20, {-10, 10})) { app.exit(4); return; }
        }
        if (scenario == "dialog" || scenario == "picking" || scenario == "canceled") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            w.findChild<QAction*>("calibratePdfAction")->trigger();
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            auto *dialog = w.findChild<PdfCalibrationDialog*>();
            if (!dialog) { app.exit(5); return; }
            if (scenario == "picking")
                dialog->findChild<QPushButton*>("measurePdfCurrentButton")->click();
            else if (scenario == "canceled") dialog->reject();
        }
        if (scenario == "floating-palette") w.findChild<QDockWidget*>("basicToolDock")->setFloating(true);
        if (scenario == "closed-palette") w.findChild<QDockWidget*>("basicToolDock")->close();
        if (scenario == "drawing-draft") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
            auto *view = w.findChild<DrawingView*>();
            view->setActiveTool(DrawingTool::Polygon);
            view->submitDrawingPoint({0, 0}); view->submitDrawingPoint({20, 0}); view->submitDrawingPoint({10, 20});
        }
        if (scenario == "selected-shapes" || scenario == "moving-shapes") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
            auto *view = w.findChild<DrawingView*>();
            view->setActiveTool(DrawingTool::Rectangle);
            view->submitDrawingPoint({-20, -20}); view->submitDrawingPoint({20, 20});
            view->setActiveTool(DrawingTool::Selection);
            // point: 用紙中心基準の実寸座標（mm）。
            const QPointF point = view->mapFromScene({0, 0});
            // press: 図形を押下し、選択・移動待ちの状態を作るマウスイベント。
            QMouseEvent press(QEvent::MouseButtonPress, point, view->viewport()->mapToGlobal(point),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(view->viewport(), &press);
            if (scenario == "moving-shapes") {
                // end: ドラッグの解放位置または移動イベントの送り先（画面px）。
                const QPointF end = point + QPointF(50, 40);
                // move: ドラッグ途中でウィンドウを閉じる状態を再現するマウス移動イベント。
                QMouseEvent move(QEvent::MouseMove, end, view->viewport()->mapToGlobal(end),
                                 Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(view->viewport(), &move);
            }
        }
        if (scenario == "floating-object-info" || scenario == "closed-object-info") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            // view: 情報パレットに表示する図形と選択状態を作るCADビュー。
            auto *view = w.findChild<DrawingView*>();
            // id: 終了直前にも情報表示が有効な円のUUID。
            const auto id = view->document()->addShape(CircleGeometry{{0,0},10});
            view->selectionModel()->selectOnly(id);
            // dock: 切り離しまたは非表示の状態で破棄する情報パレット。
            auto *dock = w.findChild<QDockWidget*>("objectInfoDock");
            if (scenario == "floating-object-info") dock->setFloating(true); else dock->close();
        }
        if (scenario == "editing-line") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            // view: 直線と選択状態を所有するビュー。
            auto *view = w.findChild<DrawingView*>();
            // id: 編集途中でも確定形状を保ったまま閉じる対象の直線UUID。
            const auto id = view->document()->addShape(QLineF(0,0,100,0));
            view->selectionModel()->selectOnly(id);
            // edit: 未確定の不正値を残して閉じる、パネル所有の入力欄。
            auto *edit = w.findChild<QLineEdit*>("edit_endX");
            edit->setText("invalid"); edit->setModified(true); edit->setFocus();
        }
        if (scenario == "editing-rectangle" || scenario == "editing-circle") {
            w.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0);
            // view: 終了時まで確定図形と選択状態を保持するCADビュー。
            auto *view = w.findChild<DrawingView*>();
            // rectangle: 幅の編集中に閉じるか、円の半径編集中に閉じるかの区別。
            const bool rectangle = scenario == "editing-rectangle";
            // id: 編集対象の確定図形のUUID。
            const auto id = rectangle ? view->document()->addShape(QRectF(0,0,100,50))
                                      : view->document()->addShape(CircleGeometry{{0,0},10});
            view->selectionModel()->selectOnly(id);
            // edit: 不正な下書きを残して閉じる、パネル所有の寸法入力欄。
            auto *edit = w.findChild<QLineEdit*>(rectangle ? "edit_width" : "edit_radius");
            edit->setText("invalid"); edit->setModified(true); edit->setFocus();
            if (rectangle) w.findChild<QDockWidget*>("objectInfoDock")->setFloating(true);
        }
        if (!w.close()) app.exit(6);
    });
    // コールバック: 正常に終了しない場合は5秒で異常終了し、テストの停止を検出する。
    QTimer::singleShot(5000, &app, [&] { app.exit(7); });
    // result: 実際のイベントループが返したプロセス終了コード。
    const int result = app.exec();
    std::printf("Shutdown scenario %s: event loop returned %d\n", qPrintable(scenario), result);
    return result;
}
