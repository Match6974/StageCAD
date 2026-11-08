/**
 * @file test_pdfcalibration.cpp
 * @brief PDF補正の固定点、寸法、測定、入力、ページ別復元を検証する。
 */

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
/// @brief 画像の上下を見分けられる、上が赤・下が青のテスト画像を作る。
QImage sampleImage()
{
    // image: 描画または取り込みに使用するラスタ画像。
    QImage image(100, 100, QImage::Format_RGB32);
    image.fill(Qt::red);
    // y: 上下方向の検証用画像を書き込む画素の行インデックス。
    for (int y = 50; y < 100; ++y)
        // x: 上下方向の検証用画像を書き込む画素の列インデックス。
        for (int x = 0; x < 100; ++x) image.setPixelColor(x, y, Qt::blue);
    return image;
}
/// @brief PDFファイル選択へ、タイマーから指定パスまたはキャンセルを返す。
/// @param timer ダイアログへの応答をイベントループから繰り返し試みるタイマー。
/// @param path 読み込むPDFのファイルパス。空なら選択を取り消す。
void fileAnswer(QTimer &timer, const QString &path = {})
{
    // コールバック: ファイル選択が表示されたら、指定したPDFを選ぶかキャンセルする。
    QObject::connect(&timer, &QTimer::timeout, &timer, [path] {
        // widget: トップレベル画面を走査して見つけたウィジェット。所有権は取得しない。
        for (auto *widget : QApplication::topLevelWidgets()) {
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (path.isEmpty()) dialog->reject();
            // edit: 対象のテキスト入力欄。所有権は所属するダイアログにある。
            else if (auto *edit = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                edit->setText(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    timer.start(20);
}
/// @brief 実寸座標に対応する画面位置をクリックする。測定補助では画素丸め後の実寸座標を返す。
/// @param view 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
/// @param model 用紙中心基準の実寸座標（mm）。
QPointF click(DrawingView *view, QPointF model)
{
    // position: 入力・表示位置を比較する画面座標（px）。
    const QPoint position = view->mapFromScene(model);
    // actual: 画面座標へ丸めた後に実際にクリックされる実寸座標（mm）。
    const QPointF actual = view->mapToScene(position);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, position);
    return actual;
}
/// @brief PDF補正ダイアログの確定ボタンを取得する。
/// @param dialog 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
QPushButton *okButton(PdfCalibrationDialog *dialog)
{
    return dialog->findChild<QDialogButtonBox*>("pdfCalibrationButtons")->button(QDialogButtonBox::Ok);
}
}

/// @brief PDF取り込みと距離補正の回帰テストをまとめる。
class PdfCalibrationTests : public QObject
{
    Q_OBJECT
private slots:
    /// @brief テスト用のダイアログ設定やロケールを整える。
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QLocale::setDefault(QLocale::c());
    }
    /// @brief PDFポイントからの寸法換算、固定した1点目、補正距離と縦横比を検証する。
    void physicalSizeAndFixedFirstPoint()
    {
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        PdfPlacement placement;
        QVERIFY(!placement.hasPage());
        QVERIFY(placement.setPage(sampleImage(), {720, 360}, 1));
        QVERIFY(qAbs(placement.sizeMm().width() - 254) < 1e-10);
        QVERIFY(qAbs(placement.sizeMm().height() - 127) < 1e-10);
        // first: 用紙中心基準の実寸座標（mm）。
        // second: 終点・次の頂点、または比較する2つ目の対象。
        const QPointF first(-76.2, 31.75), second(-50.8, 31.75);
        // current: 補正前の距離または現在距離の入力欄。
        const double current = QLineF(first, second).length();
        // originalCenter: 補正前のPDF中心の実寸座標（mm）。
        const QPointF originalCenter = placement.centerMm();
        // originalSize: 操作前のPDF実寸寸法（mm）。
        const QSizeF originalSize = placement.sizeMm();
        // firstLocal: PDF画像内での1点目の相対座標。幅・高さを1として正規化する。
        const QPointF firstLocal((first.x() - originalCenter.x()) / originalSize.width(),
                                 (first.y() - originalCenter.y()) / originalSize.height());
        // secondLocal: PDF画像内での2点目の相対座標。幅・高さを1として正規化する。
        const QPointF secondLocal((second.x() - originalCenter.x()) / originalSize.width(),
                                  (second.y() - originalCenter.y()) / originalSize.height());
        // changes: placementChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changes(&placement, &PdfPlacement::placementChanged);
        QVERIFY(placement.calibrate(current, 254, first));
        QCOMPARE(changes.count(), 1);
        // world: PDF内の相対位置を、現在の配置に応じた実寸mmへ戻すコールバック。
        // local: PDF画像内の正規化座標（幅・高さが1）。
        // コールバック: PDF画像内の正規化座標を、補正後の配置に応じた実寸mmへ換算する。
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
    /// @brief 無効な距離・画像・サイズ・補正後範囲を拒否し、配置が維持されることを検証する。
    void invalidOperationsPreservePlacement()
    {
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        PdfPlacement placement;
        QVERIFY(!placement.calibrate(1, 2, {}));
        QVERIFY(placement.setPage(sampleImage(), {720, 360}, 50));
        QVERIFY(placement.calibrate(100, 200, {10, 20}));
        // before: 変更前の配置または画面位置。失敗・取消で維持されることを確認する。
        const PdfGeometry before = placement.geometry();
        // changes: placementChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changes(&placement, &PdfPlacement::placementChanged);
        // nan: 有限値として受け付けないことを検証するNaN。
        const double nan = std::numeric_limits<double>::quiet_NaN();
        // inf: 有限値として受け付けないことを検証する無限大。
        const double inf = std::numeric_limits<double>::infinity();
        // bad: 拒否されることを確認する不正な値、または無効なPDFファイルパス。
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
    /// @brief PDF画像の上下方向と、作業領域の縮尺から実寸が独立していることを検証する。
    void rasterOrientationAndScaleIndependence()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingView view(&settings);
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        PdfPlacement placement;
        view.scene()->addItem(new PdfPageItem(&placement));
        QVERIFY(placement.setPage(sampleImage(), {72, 72}, 1));
        view.resize(800, 800); view.show(); QTest::qWait(10);
        // screen: ビューを描画した画面画像。PDFの上下方向の確認に使う。
        QImage screen = view.viewport()->grab().toImage();
        // top: 画像上部を画面から取得した色。上下反転していないことを確かめる。
        const QColor top = screen.pixelColor(view.mapFromScene({0, 6}));
        // bottom: 画像下部を画面から取得した色。上下反転していないことを確かめる。
        const QColor bottom = screen.pixelColor(view.mapFromScene({0, -6}));
        QVERIFY(top.red() > 200 && top.blue() < 50);
        QVERIFY(bottom.blue() > 200 && bottom.red() < 50);
        // realSize: 用紙またはPDFが覆う実寸幅・高さ（mm）。
        const QSizeF realSize = placement.sizeMm();
        QVERIFY(settings.setScaleDenominator(100));
        QVERIFY(view.setDisplayZoom(2));
        QCOMPARE(placement.sizeMm(), realSize);
        QCOMPARE(placement.centerMm(), QPointF());
    }
    /// @brief 測定範囲、同一点の拒否、2点完了、Escによる中止を検証する。
    void distancePickingValidationAndEscape()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingSettings settings; DrawingView view(&settings);
        view.resize(800, 800); view.show(); QTest::qWait(10);
        // picked: distancePickedの通知回数と引数を記録するシグナル監視。
        QSignalSpy picked(&view, &DrawingView::distancePicked);
        // canceled: distancePickCanceledの通知回数と引数を記録するシグナル監視。
        QSignalSpy canceled(&view, &DrawingView::distancePickCanceled);
        // bounds: 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
        const QRectF bounds(-50, -100, 100, 200);
        view.startDistancePick(DrawingView::DistanceKind::Current, bounds);
        click(&view, {70, 0}); // Outside the PDF is not a valid current distance.
        // first: 用紙中心基準の実寸座標（mm）。
        const QPointF first = click(&view, {-30, 10});
        click(&view, {-30, 10});
        QCOMPARE(picked.count(), 0); QVERIFY(view.isPickingDistance());
        // second: 用紙中心基準の実寸座標（mm）。
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
    /// @brief mm/cm換算、精度、固定点の必須性、不正入力と確定可否を検証する。
    void dialogUnitsPrecisionAndInvalidInput()
    {
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
        PdfCalibrationDialog dialog; dialog.show();
        // current: 補正前の距離または現在距離の入力欄。
        auto *current = dialog.findChild<QLineEdit*>("pdfCurrentDistance");
        // target: 補正後の距離、対応する入力欄、または操作対象のボタン。
        auto *target = dialog.findChild<QLineEdit*>("pdfTargetDistance");
        // unit: 新しい距離の入力単位（mm/cm）を選ぶコンボボックス。
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
        // bad: 拒否されることを確認する不正な値、または無効なPDFファイルパス。
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
        // apply: applyRequestedの通知回数と引数を記録するシグナル監視。
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
    /// @brief 小数点の異なるロケールでも入力値と検証が正しく対応することを確認する。
    void dialogRespectsDecimalLocale()
    {
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
        PdfCalibrationDialog dialog;
        QLocale::setDefault(QLocale::c());
        dialog.setCurrentMeasurement({0, 0}, {10.5, 0});
        // target: 補正後の距離、対応する入力欄、または操作対象のボタン。
        auto *target = dialog.findChild<QLineEdit*>("pdfTargetDistance");
        target->setText("25,5");
        QCOMPARE(dialog.currentDistanceMm(), 10.5);
        QCOMPARE(dialog.targetDistanceMm(), 25.5);
        QVERIFY(okButton(&dialog)->isEnabled());
        dialog.findChild<QComboBox*>("pdfTargetUnit")->setCurrentIndex(1);
        QCOMPARE(target->text(), QStringLiteral("2,55"));
        QCOMPARE(dialog.targetDistanceMm(), 25.5);
    }
    /// @brief 取り込み・補正・取消・ページ別配置の復元と不正操作を統合検証する。
    void windowImportCalibrateCancelAndPageCache()
    {
        // startup: 起動時のPDF選択へ自動応答するタイマー。
        QTimer startup; fileAnswer(startup);
        // window: テストで操作するメイン画面。スコープ終了時に破棄する。
        MainWindow window; startup.stop(); window.show();
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        auto *settings = window.findChild<DrawingSettings*>();
        // placement: PDFの実寸寸法と中心を保持する配置モデル。
        auto *placement = window.findChild<PdfPlacement*>();
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        auto *view = window.findChild<DrawingView*>();
        // tabs: CAD作業領域とPDF参照を切り替えるタブ。
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        // pages: PDFの表示ページを切り替える入力欄。
        auto *pages = window.findChild<QSpinBox*>("pdfPageInput");
        // importAction: ファイル選択を開くPDF取り込みアクション。
        auto *importAction = window.findChild<QAction*>("importPdfAction");
        // calibrate: PDF補正ダイアログを開くアクション。
        auto *calibrate = window.findChild<QAction*>("calibratePdfAction");
        QVERIFY(settings && placement && view && tabs && pages && importAction && calibrate);
        QVERIFY(!calibrate->isEnabled());
        QVERIFY(settings->setScaleDenominator(50));
        // choose: PDFファイルの選択へ自動応答するタイマー。
        QTimer choose; fileAnswer(choose, QString::fromUtf8(SAMPLE_PDF_PATH));
        importAction->trigger(); choose.stop();
        QVERIFY(placement->hasPage()); QCOMPARE(tabs->currentIndex(), 0);
        QCOMPARE(pages->maximum(), 2);
        // document: PDFページ情報・画像を取得する文書。所有権は呼び出し側またはメイン画面にある。
        auto *document = window.findChild<QPdfDocument*>(); QVERIFY(document);
        QCOMPARE(placement->sizeMm(), document->pagePointSize(0) * (25.4 / 72 * 50));
        // before: 変更前の配置または画面位置。失敗・取消で維持されることを確認する。
        const PdfGeometry before = placement->geometry();
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = view->scene()->addLine(0, 0, 1000, 0);
        calibrate->trigger();
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
        auto *dialog = window.findChild<PdfCalibrationDialog*>(); QVERIFY(dialog);
        QVERIFY(!importAction->isEnabled()); QVERIFY(!pages->isEnabled());
        QVERIFY(!tabs->tabBar()->isEnabled());
        QTest::mouseClick(dialog->findChild<QPushButton*>("measurePdfCurrentButton"), Qt::LeftButton);
        QVERIFY(view->isPickingDistance()); QVERIFY(!dialog->isVisible());
        // first: 用紙中心基準の実寸座標（mm）。
        const QPointF first = click(view, {-before.sizeMm.width() / 5, before.sizeMm.height() / 10});
        click(view, {before.sizeMm.width() / 5, before.sizeMm.height() / 10});
        QVERIFY(dialog->isVisible());
        QCOMPARE(*dialog->fixedPointMm(), first);
        QCOMPARE(placement->sizeMm(), before.sizeMm); // Measuring is not applying.
        // target: 補正後の距離、対応する入力欄、または操作対象のボタン。
        auto *target = dialog->findChild<QLineEdit*>("pdfTargetDistance");
        target->setText(QString::number(dialog->currentDistanceMm() * 2, 'f', 10));
        // ratio: PDF補正前後の寸法比。縦横が同じ倍率になることを確認する。
        const double ratio = dialog->targetDistanceMm() / dialog->currentDistanceMm();
        // localFirst: 固定点のPDF画像内での相対座標（幅・高さが1）。
        const QPointF localFirst((first.x() - before.centerMm.x()) / before.sizeMm.width(),
                                  (first.y() - before.centerMm.y()) / before.sizeMm.height());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) {
            QVERIFY(window.grab().save("pdf-workspace.png"));
            QVERIFY(dialog->grab().save("pdf-calibration-dialog.png"));
        }
        QTest::mouseClick(okButton(dialog), Qt::LeftButton);
        // fixedAfter: 補正後に求め直した固定点の実寸座標（mm）。
        const QPointF fixedAfter = placement->centerMm()
            + QPointF(localFirst.x() * placement->sizeMm().width(), localFirst.y() * placement->sizeMm().height());
        QVERIFY(QLineF(first, fixedAfter).length() < 1e-8);
        QVERIFY(qAbs(placement->sizeMm().width() - before.sizeMm.width() * ratio) < 1e-8);
        QCOMPARE(settings->scaleDenominator(), 50.0); QCOMPARE(line->line().length(), 1000.0);
        // calibrated: 補正確定後のPDF配置。ページ切り替え後の復元と比較する。
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
        // current: 補正前の距離または現在距離の入力欄。
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
        // cancelImport: 取り込みダイアログをキャンセルする自動応答タイマー。
        QTimer cancelImport; fileAnswer(cancelImport); importAction->trigger(); cancelImport.stop();
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm);
        // temporary: 不正PDFの読み込みを試すための一時ディレクトリ。
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        // invalid: 拒否すべき入力値、または失敗時の状態を確認する対象。
        QFile invalid(temporary.filePath("bad.pdf")); QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a PDF"); invalid.close();
        // error: 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
        QString error;
        QVERIFY(!window.importPdf(invalid.fileName(), &error)); QVERIFY(!error.isEmpty());
        QVERIFY(!window.importPdf(temporary.filePath("missing.pdf"), &error));
        QCOMPARE(placement->sizeMm(), calibrated.sizeMm); QCOMPARE(placement->centerMm(), calibrated.centerMm);
        QCOMPARE(window.findChild<QPdfDocument*>(), document);
    }
};

QTEST_MAIN(PdfCalibrationTests)
#include "test_pdfcalibration.moc"
