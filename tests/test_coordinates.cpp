/**
 * @file test_coordinates.cpp
 * @brief 縮尺換算、座標軸、入力検証、PDF参照との独立性を検証する。
 */

#include "drawingsettings.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "pdfplacement.h"
#include <QTreeWidget>
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
/// @brief 起動時のファイル選択へ、タイマーから指定パスまたはキャンセルを返す。
/// @param timer ダイアログへの応答をイベントループから繰り返し試みるタイマー。
/// @param path 読み込むPDFのファイルパス。空なら選択を取り消す。
void answerFileDialog(QTimer &timer, const QString &path = {})
{
    // コールバック: ファイル選択が表示されたら、指定パスの選択または取消を実行する。
    QObject::connect(&timer, &QTimer::timeout, &timer, [path] {
        // widget: トップレベル画面を走査して見つけたウィジェット。所有権は取得しない。
        for (auto *widget : QApplication::topLevelWidgets()) {
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            auto *dialog = qobject_cast<QFileDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            if (path.isEmpty()) {
                dialog->reject();
            // input: 数値やファイル名の入力欄。所有権は所属する画面にある。
            } else if (auto *input = dialog->findChild<QLineEdit*>("fileNameEdit")) {
                input->setText(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
            }
        }
    });
    timer.start(20);
}

/// @brief 縮尺ダイアログへ指定値を入力し、確定または取消を自動操作する。
/// @param timer ダイアログへの応答をイベントループから繰り返し試みるタイマー。
/// @param value 変換・検証する数値または入力文字列。
/// @param accept ダイアログを確定するか、キャンセルするかのテスト指定。
void answerScaleDialog(QTimer &timer, const QString &value, bool accept)
{
    // コールバック: 任意の縮尺を入力し、テスト指定どおりOKまたはキャンセルを操作する。
    QObject::connect(&timer, &QTimer::timeout, &timer, [value, accept] {
        // widget: トップレベル画面を走査して見つけたウィジェット。所有権は取得しない。
        for (auto *widget : QApplication::topLevelWidgets()) {
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            auto *dialog = qobject_cast<ScaleDialog*>(widget);
            if (!dialog || !dialog->isVisible()) continue;
            // presets: 定型縮尺と任意入力を選ぶコンボボックス。
            auto *presets = dialog->findChild<QComboBox*>("scalePresets");
            presets->setCurrentIndex(presets->count() - 1);
            dialog->findChild<QLineEdit*>("scaleDenominatorInput")->setText(value);
            // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
            auto *buttons = dialog->findChild<QDialogButtonBox*>("scaleDialogButtons");
            QTest::mouseClick(buttons->button(accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel),
                              Qt::LeftButton);
        }
    });
    timer.start(20);
}
}

/// @brief 実寸座標と縮尺設定の回帰テストをまとめる。
class CoordinateTests : public QObject
{
    Q_OBJECT
private slots:
    /// @brief テスト用のダイアログ設定やロケールを整える。
    void initTestCase()
    {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QLocale::setDefault(QLocale::c());
    }

    /// @brief 定型・任意・拡大縮尺と、対応する用紙上の長さをテスト行として用意する。
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

    /// @brief 用紙中心、軸方向、長さ換算、座標の往復変換を検証する。
    void scaleConversion()
    {
        // denominator: テスト行から取得する値。縮尺1:Nの分母N。
        QFETCH(double, denominator);
        // paperLength: テスト行から取得する値。現在の縮尺で用紙上に換算された長さ（物理mm）。
        QFETCH(double, paperLength);
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        QVERIFY(settings.setScaleDenominator(denominator));
        // a: 実寸線分の始点（mm）。用紙中心への換算を確認する。
        // b: 実寸線分の終点（mm）。始点との1000mmの距離を換算する。
        const QPointF a(0, 0), b(1000, 0);
        // center: 原点を用紙左上基準へ換算した物理座標（mm）。
        const QPointF center = settings.modelToPaperMm(a);
        QCOMPARE(center, QPointF(105, 148.5));
        // printedLength: 実寸線分を用紙へ換算した長さ（物理mm）。
        const double printedLength = QLineF(center, settings.modelToPaperMm(b)).length();
        QVERIFY(qAbs(printedLength - paperLength) < 1e-9);
        QCOMPARE(QLineF(a, b).length(), 1000.0);
        QCOMPARE(settings.paperToModelMm(center), QPointF(0, 0));
        // point: 用紙中心基準の実寸座標（mm）。
        const QPointF point(-234.5, 678.9);
        QVERIFY(QLineF(point, settings.paperToModelMm(settings.modelToPaperMm(point))).length() < 1e-8);
        QVERIFY(settings.modelToPaperMm({0, 100}).y() < center.y());
        QCOMPARE(settings.paperRectInModel().center(), QPointF(0, 0));
        QCOMPARE(settings.printableRectInModel().center(), QPointF(0, 0));
        QCOMPARE(settings.paperRectInModel().size(), QSizeF(210, 297) * denominator);
        QCOMPARE(settings.printableRectInModel().size(), QSizeF(201.4, 287.9) * denominator);
    }

    /// @brief 不正な縮尺の拒否と、既存値・通知回数の維持を検証する。
    void invalidScalesLeaveSettingsUntouched()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        QCOMPARE(settings.scaleDenominator(), 1.0);
        // changes: scaleChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changes(&settings, &DrawingSettings::scaleChanged);
        QVERIFY(settings.setScaleDenominator(50));
        QVERIFY(settings.setScaleDenominator(50));
        QCOMPARE(changes.count(), 1);
        // invalid: 拒否すべき入力値、または失敗時の状態を確認する対象。
        for (const double invalid : {0.0, -1.0, 1e-7, 1e10,
                 std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            QVERIFY(!settings.setScaleDenominator(invalid));
            QCOMPARE(settings.scaleDenominator(), 50.0);
        }
        QCOMPARE(changes.count(), 1);
        // anotherDrawing: 設定が他の作業領域へ漏れないことを確かめる独立した設定モデル。
        DrawingSettings anotherDrawing;
        QCOMPARE(anotherDrawing.scaleDenominator(), 1.0);
    }

    /// @brief 縮尺・ズーム・リサイズで表示だけが変わり、実寸形状が維持されることを検証する。
    void viewKeepsModelGeometryStable()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        DrawingView view(&settings);
        view.resize(850, 750); view.show();
        QTest::qWait(10);
        // modelLine: ズームや縮尺で変わらないことを確かめる元の線分（実寸mm）。
        const QLineF modelLine(-500, 100, 500, 100);
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = view.scene()->addLine(modelLine);
        // pixelLength: 線分の端点間を画面pxへ換算して長さを返すテスト用コールバック。
        // コールバック: 同じ実寸線分の、現在の表示変換での画面上の長さを求める。
        auto pixelLength = [&] {
            return QLineF(view.mapFromScene(line->line().p1()),
                          view.mapFromScene(line->line().p2())).length();
        };
        QVERIFY(settings.setScaleDenominator(100));
        // at100: 縮尺1:100で描画した線の画面上の長さ（px）。
        const double at100 = pixelLength();
        QVERIFY(settings.setScaleDenominator(50));
        QVERIFY(qAbs(pixelLength() - at100 * 2) < 2);
        QCOMPARE(line->line(), modelLine);
        QCOMPARE(line->line().length(), 1000.0);
        // paperLength: 現在の縮尺で用紙上に換算された長さ（物理mm）。
        const double paperLength = QLineF(settings.modelToPaperMm(modelLine.p1()),
                                          settings.modelToPaperMm(modelLine.p2())).length();
        // atZoom1: 表示ズーム1倍で描画した線の画面上の長さ（px）。
        const double atZoom1 = pixelLength();
        // initialTransform: ズーム前の表示変換。倍率だけが変わることを確認する。
        const auto initialTransform = view.transform();
        // initialSize: ズーム前の最大ビューポート寸法（px）。
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
        // cursor: cursorPositionChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy cursor(&view, &DrawingView::cursorPositionChanged);
        // position: 入力・表示位置を比較する画面座標（px）。
        const QPoint position = view.mapFromScene({1000, 1000});
        QTest::mouseMove(view.viewport(), position);
        QTRY_VERIFY(!cursor.isEmpty());
        QCOMPARE(cursor.last().at(0).toPointF(), view.mapToScene(position));
        // left: cursorLeftの通知回数と引数を記録するシグナル監視。
        QSignalSpy left(&view, &DrawingView::cursorLeft);
        // leave: マウスが作業領域を離れたことを再現するイベント。
        QEvent leave(QEvent::Leave); QApplication::sendEvent(&view, &leave);
        QCOMPARE(left.count(), 1);
        // bound: 許容する縮尺の上限・下限を試すテスト値。
        for (double bound : {DrawingSettings::minimumScale, DrawingSettings::maximumScale}) {
            QVERIFY(settings.setScaleDenominator(bound));
            QVERIFY(view.viewportTransform().isInvertible());
            QCOMPARE(line->line(), modelLine);
        }
        view.resize(50, 50);
        QVERIFY(view.setDisplayZoom(0.1));
        QVERIFY(view.viewportTransform().isInvertible());
    }

    /// @brief 縮尺候補、任意入力、不正値と確定ボタンの状態を検証する。
    void dialogPresetsAndValidation()
    {
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
        ScaleDialog dialog(&settings); dialog.show();
        // presets: 定型縮尺と任意入力を選ぶコンボボックス。
        auto *presets = dialog.findChild<QComboBox*>("scalePresets");
        // input: 数値やファイル名の入力欄。所有権は所属する画面にある。
        auto *input = dialog.findChild<QLineEdit*>("scaleDenominatorInput");
        // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
        auto *buttons = dialog.findChild<QDialogButtonBox*>("scaleDialogButtons");
        // ok: 数値変換の成否、またはダイアログの確定ボタン。
        auto *ok = buttons->button(QDialogButtonBox::Ok);
        QCOMPARE(dialog.scaleDenominator(), 1.0);
        QVERIFY(!input->isEnabled());
        // i: 頂点やボタンを走査するインデックス。
        for (int i = 0; i < 3; ++i) {
            presets->setCurrentIndex(i);
            QCOMPARE(dialog.scaleDenominator(), presets->itemData(i).toDouble());
            QVERIFY(ok->isEnabled());
        }
        presets->setCurrentIndex(3);
        QVERIFY(input->isEnabled());
        // bad: 拒否されることを確認する不正な値、または無効なPDFファイルパス。
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

    /// @brief 小数点の異なるロケールでも入力値と検証が正しく対応することを確認する。
    void dialogRespectsDecimalLocale()
    {
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        DrawingSettings settings;
        QVERIFY(settings.setScaleDenominator(1234.5));
        // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
        ScaleDialog dialog(&settings);
        // Restore before assertions so later tests do not inherit this locale.
        QLocale::setDefault(QLocale::c());
        // input: 数値やファイル名の入力欄。所有権は所属する画面にある。
        auto *input = dialog.findChild<QLineEdit*>("scaleDenominatorInput");
        // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
        auto *buttons = dialog.findChild<QDialogButtonBox*>("scaleDialogButtons");
        QCOMPARE(input->text(), QStringLiteral("1234,5"));
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
        input->setText("50,25");
        QCOMPARE(dialog.scaleDenominator(), 50.25);
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->isEnabled());
    }

    /// @brief レイヤー別縮尺の実ダイアログ、表示・選択・用紙換算とPDF実寸の保持を検証する。
    void perLayerScaleTransactions() {
        // startup: 起動時のPDF選択を取り消すタイマー。
        QTimer startup; answerFileDialog(startup);
        // window: 表示バーとナビゲーションを含む統合画面。
        MainWindow window; startup.stop(); window.show();
        // view: 実寸の文書と描画アイテムを所有するビュー。
        auto *view = window.findChild<DrawingView*>();
        // document: 各レイヤーの縮尺の正本。
        auto *document = view->document();
        // settings: アクティブレイヤーを用紙表示へ換算する設定。
        auto *settings = window.findChild<DrawingSettings*>();
        // list: 各レイヤーの縮尺・実効状態を表示する一覧。
        auto *list = window.findChild<QTreeWidget*>("layerList");
        // action: アクティブレイヤーだけに適用する既存の縮尺ボタン。
        auto *action = window.findChild<QAction*>("editScaleAction");
        // first: 1:100に変更する初期レイヤー。
        const auto first = document->activeLayerId();
        // line: 縮尺に関係なく実寸1000mmを保つ対象。
        const auto line = document->addShape(QLineF(0,0,1000,0));
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        // pdf: 表示換算で実寸配置を変えないPDFモデル。
        auto *pdf = window.findChild<PdfPlacement*>();
        // originalPdf: 補正・配置の変化がないことを比較する値。
        const auto originalPdf = pdf->geometry();
        // accept: 現在のレイヤーだけを1:100にする実ダイアログの応答。
        QTimer accept; answerScaleDialog(accept,"100",true); action->trigger(); accept.stop();
        QCOMPARE(document->layer(first)->scaleDenominator,100.0); QCOMPARE(settings->scaleDenominator(),100.0);
        QCOMPARE(QLineF(settings->modelToPaperMm({0,0}),settings->modelToPaperMm({1000,0})).length(),10.0);
        // second: 新規レイヤーは1:1で始まり、アクティブ切替時に表示倍率も切り替わる。
        const auto second = document->addLayer(); QCOMPARE(settings->scaleDenominator(),1.0);
        QCOMPARE(document->layer(first)->scaleDenominator,100.0); QVERIFY(!view->shapeItems().first()->isVisible());
        QVERIFY(!view->selectionModel()->selectOnly(line));
        // rectangle: 第2レイヤー上の形状。実寸と所属を保持する。
        const auto rectangle = document->addShape(QRectF(-2000,-1000,4000,2000));
        QVERIFY(settings->setScaleDenominator(50));
        QCOMPARE(document->layer(second)->scaleDenominator,50.0); QCOMPARE(document->layer(first)->scaleDenominator,100.0);
        QCOMPARE(list->topLevelItem(0)->text(3),QString("1:100")); QCOMPARE(list->topLevelItem(1)->text(3),QString("1:50"));
        QVERIFY(list->topLevelItem(0)->toolTip(0).contains(QString::fromUtf8("縮尺")));
        QVERIFY(view->selectionModel()->selectOnly(rectangle));
        // cancel: 別の縮尺を入力しても取消では正本と表示を変えない。
        QTimer cancel; answerScaleDialog(cancel,"25",false); action->trigger(); cancel.stop();
        QCOMPARE(document->layer(second)->scaleDenominator,50.0); QCOMPARE(settings->scaleDenominator(),50.0);
        QVERIFY(document->setLayerVisibility(first,LayerVisibility::Gray));
        QVERIFY(document->setLayerScale(second,100)); QVERIFY(view->shapeItems().first()->isVisible());
        QCOMPARE(document->effectiveLayerVisibility(first),LayerVisibility::Gray);
        QVERIFY(document->setLayerScale(second,50)); QVERIFY(!view->shapeItems().first()->isVisible());
        QCOMPARE(document->layer(first)->visibility,LayerVisibility::Gray);
        QCOMPARE(std::get<QRectF>(document->shape(rectangle)->geometryMm),QRectF(-2000,-1000,4000,2000));
        QCOMPARE(std::get<QLineF>(document->shape(line)->geometryMm).length(),1000.0);
        QCOMPARE(pdf->geometry().sizeMm,originalPdf.sizeMm); QCOMPARE(pdf->geometry().centerMm,originalPdf.centerMm);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step4-layer-scales.png"));
        QVERIFY(document->setActiveLayer(first)); QCOMPARE(settings->scaleDenominator(),100.0);
        QVERIFY(view->shapeItems().first()->isVisible()); QVERIFY(!view->shapeItems().last()->isVisible());
        QVERIFY(view->selectionModel()->selectedIds().isEmpty());
        QVERIFY(window.findChild<QLabel*>("drawingScaleLabel")->text().contains("1:100"));
        QVERIFY(document->setLayerScale(second,100)); QVERIFY(view->shapeItems().last()->isVisible());
        QCOMPARE(document->shape(rectangle)->layerId,second);
    }

    /// @brief 縮尺の確定・取消、座標表示、関連UIの更新を実際の画面で検証する。
    void mainWindowScaleTransactionAndCoordinates()
    {
        // startup: 起動時のPDF選択へ自動応答するタイマー。
        QTimer startup; answerFileDialog(startup);
        // window: テストで操作するメイン画面。スコープ終了時に破棄する。
        MainWindow window; startup.stop(); window.show();
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        auto *settings = window.findChild<DrawingSettings*>();
        // view: 対象のCADビュー。入力イベントと実寸座標の対応を確認する。
        auto *view = window.findChild<DrawingView*>();
        // action: 画面やツールの操作を表すアクション。所有権は所属するQtオブジェクトにある。
        auto *action = window.findChild<QAction*>("editScaleAction");
        // label: 現在の縮尺表示を確認するラベル。
        auto *label = window.findChild<QLabel*>("drawingScaleLabel");
        // coordinates: 実寸X/Y表示を検証する座標ラベル。
        auto *coordinates = window.findChild<QLabel*>("coordinatesLabel");
        QVERIFY(settings && view && action && label && coordinates);
        QVERIFY(action->isEnabled());
        // line: 対象の直線形状またはその描画アイテム。
        auto *line = view->scene()->addLine(0, 0, 1000, 0);
        // cancel: 入力値を反映せず閉じるキャンセルボタン。
        QTimer cancel; answerScaleDialog(cancel, "100", false);
        action->trigger(); cancel.stop();
        QCOMPARE(settings->scaleDenominator(), 1.0);
        // accept: ダイアログを確定するか、キャンセルするかのテスト指定。
        QTimer accept; answerScaleDialog(accept, "50", true);
        action->trigger(); accept.stop();
        QCOMPARE(settings->scaleDenominator(), 50.0);
        QCOMPARE(line->line().length(), 1000.0);
        QVERIFY(label->text().contains("1:50"));
        // coverage: 用紙が覆う実寸範囲を検証する表示ラベル。
        auto *coverage = window.findChild<QLabel*>("paperCoverageLabel");
        QVERIFY(coverage->text().contains("10500"));
        QTest::qWait(10);
        // position: 入力・表示位置を比較する画面座標（px）。
        const QPoint position = view->mapFromScene({500, -500});
        QTest::mouseMove(view->viewport(), position);
        // model: 用紙中心基準の実寸座標（mm）。
        const QPointF model = view->mapToScene(position);
        QTRY_VERIFY(coordinates->text().contains(QString::number(model.x(), 'f', 2)));
        QVERIFY(coordinates->text().contains(QString::number(model.y(), 'f', 2)));
        // changes: scaleChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changes(settings, &DrawingSettings::scaleChanged);
        // invalidInputBlocked: 不正な縮尺では確定ボタンを押せないことを確認した結果。
        bool invalidInputBlocked = false;
        // invalid: 拒否すべき入力値、または失敗時の状態を確認する対象。
        QTimer invalid; answerScaleDialog(invalid, "0", true);
        // Invalid OK is disabled; close it with Escape after checking the state.
        // コールバック: 不正値が入力された縮尺画面を確認し、閉じてテストを継続する。
        QTimer::singleShot(100, &window, [&invalidInputBlocked] {
            // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
            if (auto *dialog = qobject_cast<ScaleDialog*>(QApplication::activeModalWidget())) {
                // buttons: 所属ダイアログが所有する確定・取消のボタンボックス。
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

    /// @brief 既存のPDF参照表示と文書の読み込みが維持されることを検証する。
    void existingPdfViewerStillWorks()
    {
        // startup: 起動時のPDF選択へ自動応答するタイマー。
        QTimer startup; answerFileDialog(startup, QString::fromUtf8(SAMPLE_PDF_PATH));
        // window: テストで操作するメイン画面。スコープ終了時に破棄する。
        MainWindow window; startup.stop(); window.show();
        // document: PDFページ情報・画像を取得する文書。所有権は呼び出し側またはメイン画面にある。
        auto *document = window.findChild<QPdfDocument*>();
        // pdfView: 既存のPDF参照ビュー。文書の読み込み状態を検証する。
        auto *pdfView = window.findChild<QPdfView*>();
        // settings: 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
        auto *settings = window.findChild<DrawingSettings*>();
        // tabs: CAD作業領域とPDF参照を切り替えるタブ。
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        // scaleAction: 作業領域の縮尺設定を開くアクション。
        auto *scaleAction = window.findChild<QAction*>("editScaleAction");
        QVERIFY(document && pdfView && settings && tabs && scaleAction);
        QCOMPARE(document->status(), QPdfDocument::Status::Ready);
        QCOMPARE(document->pageCount(), 2);
        QCOMPARE(pdfView->document(), document);
        QCOMPARE(tabs->currentIndex(), 1);
        QVERIFY(!scaleAction->isEnabled());
        // originalSize: 操作前のPDF実寸寸法（mm）。
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
