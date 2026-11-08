/**
 * @file test_drawing.cpp
 * @brief 図形の描画、選択、移動、削除とモデル・表示の同期を検証する。
 */

#include "drawingview.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "drawingsettings.h"
#include "mainwindow.h"
#include "toolpalette.h"
#include "pdfplacement.h"
#include "objectinfopanel.h"
#include "navigationpanel.h"
#include <QTreeWidget>
#include <QHeaderView>
#include "rectangleanchorselector.h"
#include <QGraphicsRectItem>
#include <QDockWidget>
#include <QCheckBox>
#include <QToolButton>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QLabel>
#include <QSignalSpy>
#include "pdfcalibrationdialog.h"
#include <QDoubleSpinBox>
#include <QLineEdit>
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
    /// @brief パネルでの直線編集が、同じUUIDの表示・選択判定へ反映されることを検証する。
    void linePropertiesUpdateCanvas() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のファイル選択を閉じる自動応答タイマー。
        QTimer startup;
        // コールバック: テストでは起動時のPDFを選ばず、空の作業領域を開く。
        connect(&startup,&QTimer::timeout,[] {
            // widget: 起動中のトップレベル画面。所有権は取得しない。
            for (auto *widget : QApplication::topLevelWidgets())
                // dialog: 表示中のファイル選択ダイアログ。
                if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        startup.start(20);
        // window: パネル・文書・ビューを統合して操作するメイン画面。
        MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        // view: 図形モデルを所有し、編集後の形状を描画するCADビュー。
        auto *view = window.findChild<DrawingView*>();
        // info: 直線の編集欄を所有するプロパティパネル。
        auto *info = window.findChild<ObjectInfoPanel*>(); QVERIFY(view && info);
        // id: 編集前後で選択を維持する直線のUUID。
        const auto id = view->document()->addShape(QLineF(-30,-20,30,-20));
        QVERIFY(view->document()->translateShapes({id},{10,5})); QVERIFY(view->selectionModel()->selectOnly(id));
        // item: 形状変更でも作り直されない、シーン所有の直線アイテム。
        auto *item = qgraphicsitem_cast<QGraphicsLineItem*>(view->shapeItems().first()); QVERIFY(item);
        // length: 始点を固定して長さを変更する数値入力欄。
        auto *length = info->findChild<QLineEdit*>("edit_length"); QVERIFY(length);
        length->setFocus(); length->selectAll(); QTest::keyClicks(length,"120"); QTest::keyClick(length,Qt::Key_Return);
        QCOMPARE(item->line(),QLineF(-20,-15,100,-15)); QCOMPARE(item->pos(),QPointF());
        // angle: 長さと始点を保って角度を変更する数値入力欄。
        auto *angle = info->findChild<QLineEdit*>("edit_angle"); QVERIFY(angle);
        angle->setFocus(); angle->selectAll(); QTest::keyClicks(angle,"90"); QTest::keyClick(angle,Qt::Key_Return);
        QCOMPARE(item->line(),QLineF(-20,-15,-20,105)); QCOMPARE(view->selectionModel()->selectedIds(),QList<QUuid>({id}));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({10,-15}));
        QVERIFY(view->selectedShapeItems().isEmpty());
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({-20,45}));
        QCOMPARE(view->selectedShapeItems(),QList<QGraphicsItem*>({item}));
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        // start: 補正中の無効化で未確定入力が破棄されることを確認する始点X入力欄。
        auto *start = info->findChild<QLineEdit*>("edit_startX"); QVERIFY(start);
        start->setText("99"); start->setModified(true);
        window.findChild<QAction*>("calibratePdfAction")->trigger();
        QCOMPARE(start->text(),QString("-20")); QVERIFY(!info->isEnabled());
        window.findChild<PdfCalibrationDialog*>()->reject();
        QCOMPARE(item->line(),QLineF(-20,-15,-20,105));
        QVERIFY(view->setDisplayZoom(2)); QCOMPARE(item->line(),QLineF(-20,-15,-20,105));
        view->resetView();
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-line-editing.png"));
    }
    /// @brief 9点固定での寸法変更と円の半径変更が、選択を保って図面へ反映されることを検証する。
    void dimensionPropertiesUpdateCanvas() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のPDF選択を閉じる自動応答タイマー。
        QTimer startup;
        // コールバック: 図形編集だけを検証するため、起動時のPDF選択を取り消す。
        connect(&startup,&QTimer::timeout,[] {
            // widget: Qtが所有する、起動中のトップレベル画面。
            for (auto *widget : QApplication::topLevelWidgets())
                // dialog: 表示中の場合だけ閉じるファイル選択画面。
                if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        startup.start(20);
        // window: 編集パネルと図面を統合して検証するメイン画面。
        MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        // view: 図形文書と表示アイテムを所有するCADビュー。
        auto *view = window.findChild<DrawingView*>();
        // info: 入力欄と固定点選択を所有する情報パネル。
        auto *info = window.findChild<ObjectInfoPanel*>(); QVERIFY(view && info);
        // rectangle: 移動済みの矩形のUUID。右上(40,30)を固定して伸縮する。
        const auto rectangle = view->document()->addShape(QRectF(-30,-20,60,40));
        QVERIFY(view->document()->translateShapes({rectangle},{10,10}));
        QVERIFY(view->selectionModel()->selectOnly(rectangle));
        QVERIFY(info->findChild<RectangleAnchorSelector*>()->setAnchorIndex(2));
        // width: Enterで幅だけを確定する入力欄。
        auto *width = info->findChild<QLineEdit*>("edit_width");
        width->setFocus(); width->selectAll(); QTest::keyClicks(width,"100"); QTest::keyClick(width,Qt::Key_Return);
        // height: 幅の変更後も同じ右上を固定する高さ入力欄。
        auto *height = info->findChild<QLineEdit*>("edit_height");
        height->setFocus(); height->selectAll(); QTest::keyClicks(height,"80"); QTest::keyClick(height,Qt::Key_Return);
        // rectItem: 同じUUIDを描画し続ける、シーン所有の矩形アイテム。
        auto *rectItem = qgraphicsitem_cast<QGraphicsRectItem*>(view->shapeItems().first()); QVERIFY(rectItem);
        QCOMPARE(rectItem->rect(),QRectF(-60,-50,100,80)); QCOMPARE(rectItem->pos(),QPointF());
        QCOMPARE(view->selectionModel()->selectedIds(),QList<QUuid>({rectangle}));
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-rectangle-anchor.png"));
        // circle: 中心(70,30)を保持して半径を編集する、移動済みの円のUUID。
        const auto circle = view->document()->addShape(CircleGeometry{{60,20},10});
        QVERIFY(view->document()->translateShapes({circle},{10,10})); QVERIFY(view->selectionModel()->selectOnly(circle));
        // radius: 真円のまま拡大する半径入力欄。
        auto *radius = info->findChild<QLineEdit*>("edit_radius");
        radius->setFocus(); radius->selectAll(); QTest::keyClicks(radius,"25"); QTest::keyClick(radius,Qt::Key_Return);
        // ellipseItem: モデルの円を表示する、シーン所有の楕円アイテム。
        auto *ellipseItem = qgraphicsitem_cast<QGraphicsEllipseItem*>(view->shapeItems().last()); QVERIFY(ellipseItem);
        QCOMPARE(ellipseItem->rect(),QRectF(45,5,50,50)); QCOMPARE(ellipseItem->pos(),QPointF());
        QCOMPARE(view->selectionModel()->selectedIds(),QList<QUuid>({circle}));
        QCOMPARE(rectItem->rect(),QRectF(-60,-50,100,80));
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-circle-radius.png"));
        // diameter: 中心座標を保持して直径からも寸法を変更できる入力欄。
        auto *diameter = info->findChild<QLineEdit*>("edit_diameter");
        diameter->setFocus(); diameter->selectAll(); QTest::keyClicks(diameter,"80"); QTest::keyClick(diameter,Qt::Key_Return);
        QCOMPARE(ellipseItem->rect(),QRectF(30,-10,80,80));
        // sweep: 図面上の円を開いた円弧へ変更する入力欄。
        auto *sweep = info->findChild<QLineEdit*>("edit_arcSweep");
        sweep->setFocus(); sweep->selectAll(); QTest::keyClicks(sweep,"120"); QTest::keyClick(sweep,Qt::Key_Return);
        QVERIFY(!view->document()->shape(circle)->isFilled());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-arc-properties.png"));
        QVERIFY(view->selectionModel()->selectOnly(rectangle));
        // rotation: 選択した右上を固定して回転する角度入力欄。
        auto *rotation = info->findChild<QLineEdit*>("edit_rectangleAngle");
        rotation->setFocus(); rotation->selectAll(); QTest::keyClicks(rotation,"30"); QTest::keyClick(rotation,Qt::Key_Return);
        QCOMPARE(view->shapeItems().first(),rectItem);
        QVERIFY(QLineF(view->document()->shape(rectangle)->referencePoint({1,1}),QPointF(40,30)).length()<1e-8);
        QVERIFY(!rectItem->transform().isIdentity());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-rotated-properties.png"));
        // polygon: マーカー表示と個別頂点編集をメイン画面で検証する多角形UUID。
        const auto polygon = view->document()->addShape(QPolygonF({{-90,-70},{30,-70},{-30,10}}));
        QVERIFY(view->selectionModel()->selectOnly(polygon));
        QTest::mouseClick(info->findChild<QPushButton*>("vertex_back"),Qt::LeftButton);
        // vertex: 選択頂点だけを動かす実寸X入力欄。
        auto *vertex = info->findChild<QLineEdit*>("edit_vertexX");
        vertex->setFocus(); vertex->selectAll(); QTest::keyClicks(vertex,"40"); QTest::keyClick(vertex,Qt::Key_Return);
        QCOMPARE(std::get<QPolygonF>(view->document()->shape(polygon)->geometryMm)[1],QPointF(40,-70));
        QTest::mouseClick(info->findChild<QCheckBox*>("polygonClosed"),Qt::LeftButton);
        QVERIFY(!view->document()->shape(polygon)->polygonClosed);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-polygon-properties.png"));
        // palette: 編集後に実際のツールボタンから描画を再開する基本パレット。
        auto *palette = window.findChild<ToolPalette*>();
        // tool: 編集済み図形の選択を残したまま、新たに描く図形の種類。
        for (const auto tool : {DrawingTool::Line,DrawingTool::Rectangle,DrawingTool::Circle,DrawingTool::Polygon}) {
            // button: 対象アクションを表示する、パレット所有の実ボタン。
            QToolButton *button = nullptr;
            // candidate: 基本パレット内の各ツールボタン。
            for (auto *candidate : palette->findChildren<QToolButton*>())
                if (candidate->defaultAction() == palette->toolAction(tool)) button = candidate;
            QVERIFY(button); QTest::mouseClick(button,Qt::LeftButton);
            QCOMPARE(view->activeTool(),tool); QVERIFY(view->drawingEnabled());
            // count: この操作で図形が1件増えることを確認する変更前の件数。
            const auto count = view->document()->shapes().size();
            QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({-70,70}));
            QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({-30,50}));
            if (tool == DrawingTool::Polygon) {
                QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({-60,30}));
                QVERIFY(view->finishPolygon());
            }
            QCOMPARE(view->document()->shapes().size(),count+1);
        }


    }
    /// @brief 回転矩形・開いた円弧と多角形の描画・選択判定、および移動の整合性を検証する。
    void extendedGeometrySelection() {
        // settings: 実寸と画面座標の変換設定。
        DrawingSettings settings;
        // view: 拡張属性を持つ図形の表示とマウス入力を検証するCADビュー。
        DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        // document: 表示アイテムの正本となる文書。
        auto *document = view.document();
        // rectangle: 90度回転した矩形のUUID。
        const auto rectangle = document->addShape(QRectF(0,0,60,20));
        // data: 各図形の拡張属性を設定するコピー。
        auto data = *document->shape(rectangle); data.rectangleAngleDeg = 90; QVERIFY(document->updateShape(data));
        // item: UUIDと描画順を維持して回転した矩形のアイテム。
        auto *item = view.shapeItems().first();
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({-10,30}));
        QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({rectangle}));
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({30,10}));
        QVERIFY(view.selectionModel()->selectedIds().isEmpty());
        QVERIFY(document->translateShapes({rectangle},{-40,-30})); QCOMPARE(view.shapeItems().first(),item);
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({-50,0}));
        QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({rectangle}));
        // circle: 第1象限の四分円に変更するUUID。
        const auto circle = document->addShape(CircleGeometry{{40,40},25});
        data = *document->shape(circle); std::get<CircleGeometry>(data.geometryMm).sweepAngleDeg = 90;
        QVERIFY(document->updateShape(data));
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({40,40}));
        QVERIFY(view.selectionModel()->selectedIds().isEmpty());
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({65,40}));
        QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({circle}));
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({15,40}));
        QVERIFY(view.selectionModel()->selectedIds().isEmpty());
        // polygon: 左辺だけが開いた四角い多角形のUUID。
        const auto polygon = document->addShape(QPolygonF({{0,-60},{60,-60},{60,-20},{0,-20}}));
        data = *document->shape(polygon); data.polygonClosed = false; QVERIFY(document->updateShape(data));
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({0,-40}));
        QVERIFY(view.selectionModel()->selectedIds().isEmpty());
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({30,-40}));
        QVERIFY(view.selectionModel()->selectedIds().isEmpty());
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({30,-60}));
        QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({polygon}));
        data.polygonClosed = true; QVERIFY(document->updateShape(data));
        QTest::mouseClick(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene({30,-40}));
        QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({polygon}));
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(view.grab().save("step3-extended-geometry.png"));
    }
    /// @brief 表示と数値の双方向同期を確認する7種類の図形状態を用意する。
    void bidirectionalPropertySync_data() {
        QTest::addColumn<QString>("kind");
        // name: 水平線・斜線・回転四角・真円・円弧・開閉多角形のケース名。
        for (const char *name : {"line","diagonal","rectangle","circle","arc","polygon","open-polygon"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    /// @brief 連続数値編集、図面での移動・取消、縮尺変更後の実寸と再選択を検証する。
    void bidirectionalPropertySync() {
        // kind: 検証する図形状態の名前。
        QFETCH(QString,kind);
        // settings: 縮尺を変更してもモデルの実寸を保つ設定。
        DrawingSettings settings;
        // view: 図形文書・選択と表示アイテムを所有するビュー。
        DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        // document: 数値と描画で共有する形状の正本。
        auto *document = view.document();
        // geometry: 対象ケースの初期形状（実寸mm）。
        ShapeGeometry geometry = QLineF(-20,-10,20,kind == "line" ? -10 : 10);
        if (kind == "rectangle") geometry = QRectF(-20,-10,40,20);
        else if (kind == "circle" || kind == "arc") geometry = CircleGeometry{{0,0},20};
        else if (kind.contains("polygon")) geometry = QPolygonF({{-20,-10},{20,-10},{5,20}});
        // id: 全操作で維持する単一図形のUUID。
        const auto id = document->addShape(geometry);
        // initial: 移動済み・回転済み・開いた状態も含める初期値のコピー。
        auto initial = *document->shape(id); initial.positionMm = {5,10};
        if (kind == "rectangle") initial.rectangleAngleDeg = 30;
        if (kind == "arc") std::get<CircleGeometry>(initial.geometryMm).sweepAngleDeg = 120;
        if (kind == "open-polygon") initial.polygonClosed = false;
        QVERIFY(document->updateShape(initial));
        // panel: 数値を入力し、図面からの変更を表示するパレット。
        ObjectInfoPanel panel(document,view.selectionModel()); panel.show();
        connect(&panel,&ObjectInfoPanel::vertexHighlighted,&view,&DrawingView::highlightVertex);
        QVERIFY(view.selectionModel()->selectOnly(id));
        // item: 数値編集・移動・ズームで作り直さない表示アイテム。
        auto *item = view.shapeItems().first();
        // changes: 確定1回に対して文書変更通知が1回だけであることの監視。
        QSignalSpy changes(document,&DrawingDocument::shapeChanged);
        // selections: 形状変更だけでは選択を変えないことの監視。
        QSignalSpy selections(view.selectionModel(),&ShapeSelectionModel::selectionChanged);
        // edits: 種類に対応する連続入力。角度は度、他は実寸mm。
        QList<QPair<QString,QString>> edits;
        if (initial.kind() == ShapeKind::Line) edits = {{"length","60"},{"angle","45"},{"startX","-25"},{"endY","50"}};
        else if (initial.kind() == ShapeKind::Rectangle) edits = {{"width","60"},{"height","40"},{"rectangleAngle","60"},{"originX","25"},{"originY","-20"}};
        else if (initial.kind() == ShapeKind::Circle) edits = {{"centerX","-20"},{"centerY","15"},{"diameter","60"},{"radius","25"},{"arcStart","30"},{"arcSweep","150"}};
        else edits = {{"width","60"},{"height","45"},{"originX","-20"},{"originY","-10"},{"vertexX","-15"},{"vertexY","-5"}};
        // edit: 各項目をEnterで確定し、その直後の表示を検査する入力。
        for (const auto &edit : edits) {
            // input: パレットが所有する対象の数値欄。
            auto *input = panel.findChild<QLineEdit*>("edit_"+edit.first); QVERIFY(input && input->isVisible());
            // before: 確定前の通知数。再入や循環による多重通知も検出する。
            const auto before = changes.count();
            input->setFocus(); input->selectAll(); QTest::keyClicks(input,edit.second); QTest::keyClick(input,Qt::Key_Return);
            QCOMPARE(changes.count(),before+1); QCOMPARE(selections.count(),0);
            QCOMPARE(view.shapeItems().first(),item); QCOMPARE(view.selectionModel()->selectedIds(),QList<QUuid>({id}));
            // data: 確定した現在のモデル形状。表示アイテムの実データと比較する。
            const auto data = *document->shape(id);
            QCOMPARE(item->pos(),data.positionMm);
            if (data.kind() == ShapeKind::Line) QCOMPARE(qgraphicsitem_cast<QGraphicsLineItem*>(item)->line(),std::get<QLineF>(data.geometryMm));
            else if (data.kind() == ShapeKind::Rectangle) {
                // drawn: 表示アイテムに設定された辺長・回転・配置を反映したパス。
                QPainterPath drawn; drawn.addRect(qgraphicsitem_cast<QGraphicsRectItem*>(item)->rect());
                QCOMPARE(item->mapToScene(drawn),data.localPath().translated(data.positionMm));
            } else if (data.kind() == ShapeKind::Circle) {
                // circle: パレット編集後の中心と半径（実寸mm）。
                const auto circle = std::get<CircleGeometry>(data.geometryMm);
                QCOMPARE(qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->rect().center(),circle.centerMm);
                QCOMPARE(qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->rect().width(),2*circle.radiusMm);
                QVERIFY(item->shape().contains(data.localPath().pointAtPercent(0.5)));
                if (!data.isFilled()) QVERIFY(!item->shape().contains(circle.centerMm));
            } else QCOMPARE(qgraphicsitem_cast<QGraphicsPolygonItem*>(item)->polygon(),std::get<QPolygonF>(data.geometryMm));
        }
        // old: マウス移動前の確定済み形状。移動中と取消時は数値に書き戻さない。
        const auto old = *document->shape(id);
        // key: マウス移動で変化する位置を表示する項目名。
        const QString key = old.kind() == ShapeKind::Line ? "startX" : old.kind() == ShapeKind::Circle ? "centerX" : "originX";
        // coordinate: 図面操作の結果を確認するX入力欄。
        auto *coordinate = panel.findChild<QLineEdit*>("edit_"+key);
        // previousText: 移動プレビューで上書きしない確定値表示。
        const QString previousText = coordinate->text();
        // vertical: 同じ位置のY成分を表示する入力欄。
        auto *vertical = panel.findChild<QLineEdit*>("edit_"+key.left(key.size()-1)+"Y");
        // previousY: 移動前のY表示値（実寸mm）。
        const double previousY = vertical->text().toDouble();
        // start: 確実に線上を選択する、最初の描画頂点の画面位置。
        const QPoint start = view.mapFromScene(old.localPath().pointAtPercent(0)+old.positionMm);
        // end: 画面上で移動先にする点（px）。
        const QPoint end = start+QPoint(30,-25);
        // delta: 画面の移動距離を換算した実寸移動量（mm）。
        const QPointF delta = view.mapToScene(end)-view.mapToScene(start);
        QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,start);
        // move: ドラッグ距離を超えているマウス移動イベント。
        QMouseEvent move(QEvent::MouseMove,end,view.viewport()->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view.viewport(),&move);
        QCOMPARE(coordinate->text(),previousText); QCOMPARE(document->shape(id)->positionMm,old.positionMm);
        QTest::mouseRelease(view.viewport(),Qt::LeftButton,Qt::NoModifier,end);
        QVERIFY(QLineF(document->shape(id)->positionMm,old.positionMm+delta).length()<1e-8);
        QVERIFY(std::abs(coordinate->text().toDouble()-(previousText.toDouble()+delta.x()))<0.000002);
        QVERIFY(std::abs(vertical->text().toDouble()-(previousY+delta.y()))<0.000002);
        // committed: 縮尺・ズーム・移動取消で変わってはならない確定位置。
        const QPointF committed = document->shape(id)->positionMm;
        QVERIFY(view.setDisplayZoom(2)); QVERIFY(settings.setScaleDenominator(50));
        QCOMPARE(document->shape(id)->positionMm,committed); QCOMPARE(document->shape(id)->localPath(),old.localPath());
        QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,view.mapFromScene(document->shape(id)->localPath().pointAtPercent(0)+committed));
        QApplication::sendEvent(view.viewport(),&move); QTest::keyClick(&view,Qt::Key_Escape);
        QCOMPARE(document->shape(id)->positionMm,committed); QCOMPARE(item->pos(),committed);
        view.selectionModel()->clear(); QVERIFY(coordinate->text().isEmpty());
        QVERIFY(view.selectionModel()->selectOnly(id)); QVERIFY(!coordinate->text().isEmpty());
        QVERIFY(document->removeShapes({id})); QVERIFY(view.shapeItems().isEmpty()); QVERIFY(coordinate->text().isEmpty());
    }
    /// @brief Undoが作成途中・移動途中を取り消して、表示と確定値だけを復元する。
    void historyCancelsPreviews() {
        // settings: CAD表示の座標設定。
        DrawingSettings settings;
        // view: モデルの復元通知から一時操作を破棄するビュー。
        DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        // document: 作成・移動の2操作を記録する文書。
        auto *document = view.document();
        // id: 同じUUIDで履歴を移動する対象の線。
        const auto id = document->addShape(QLineF(-40,-10,0,-10));
        QVERIFY(view.selectionModel()->selectOnly(id)); QVERIFY(document->translateShapes({id},{10,0}));
        view.setActiveTool(DrawingTool::Line); QVERIFY(view.submitDrawingPoint({-20,-20})); QCOMPARE(view.draftPointCount(),1);
        QVERIFY(document->undo()); QCOMPARE(view.draftPointCount(),0); QCOMPARE(document->shape(id)->positionMm,QPointF());
        QVERIFY(document->redo()); view.setActiveTool(DrawingTool::Selection);
        // start: 移動済み線上のドラッグ開始位置（px）。end: 移動プレビューの終点（px）。
        const QPoint start = view.mapFromScene({-10,-10}), end = start+QPoint(30,-20);
        QTest::mousePress(view.viewport(),Qt::LeftButton,Qt::NoModifier,start);
        // move: モデル未確定の一時移動を開始するマウスイベント。
        QMouseEvent move(QEvent::MouseMove,end,view.viewport()->mapToGlobal(end),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(view.viewport(),&move);
        QVERIFY(view.shapeItems().first()->pos() != document->shape(id)->positionMm);
        QVERIFY(document->undo()); QCOMPARE(view.shapeItems().first()->pos(),QPointF());
        QCOMPARE(document->shape(id)->positionMm,QPointF());
        QTest::mouseRelease(view.viewport(),Qt::LeftButton,Qt::NoModifier,end);
        QCOMPARE(document->shape(id)->positionMm,QPointF());
        QVERIFY(document->undo()); QVERIFY(view.shapeItems().isEmpty());
        QVERIFY(document->redo()); QCOMPARE(document->shapes().first().id,id); QCOMPARE(view.shapeItems().size(),1);
    }
    /// @brief メニュー・表示バー・標準キーの履歴と、数値欄・表示・選択の復元を検証する。
    void undoRedoWindowIntegration() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のPDF選択を閉じるタイマー。
        QTimer startup;
        // コールバック: 空の図面で文書履歴を検証する。
        connect(&startup,&QTimer::timeout,[] {
            // widget: 起動中のトップレベル画面。
            for (auto *widget : QApplication::topLevelWidgets())
                // dialog: 起動時にだけ閉じるファイル選択画面。
                if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        startup.start(20);
        // window: 履歴UIと図形を統合するメイン画面。
        MainWindow window; startup.stop(); window.show(); window.activateWindow(); QTest::qWait(10);
        // view: 文書と表示アイテムの所有者。
        auto *view = window.findChild<DrawingView*>();
        // document: 同じ履歴を参照する正本モデル。
        auto *document = view->document();
        // undo: メニューと表示バーで共有する取り消しアクション。
        auto *undo = window.findChild<QAction*>("undoAction");
        // redo: 同じ文書のやり直しアクション。
        auto *redo = window.findChild<QAction*>("redoAction");
        QVERIFY(undo && redo); QVERIFY(!undo->isEnabled()); QVERIFY(!redo->isEnabled());
        // line: 描画順先頭の図形。rectangle: 復元時に中間へ戻す図形。circle: 末尾の図形。
        const auto line = document->addShape(QLineF(-20,0,20,0));
        const auto rectangle = document->addShape(QRectF(-30,-20,60,40));
        const auto circle = document->addShape(CircleGeometry{{80,0},10});
        QVERIFY(view->selectionModel()->selectOnly(rectangle));
        // width: Enter確定・不正値拒否・文字Undoを確認する数値欄。
        auto *width = window.findChild<QLineEdit*>("edit_width");
        width->setFocus(); width->selectAll(); QTest::keyClicks(width,"100"); QTest::keyClick(width,Qt::Key_Return);
        QCOMPARE(width->text(),QString("100")); QVERIFY(undo->isEnabled()); QVERIFY(!redo->isEnabled());
        undo->trigger(); QCOMPARE(width->text(),QString("60")); QVERIFY(redo->isEnabled());
        QCOMPARE(view->selectionModel()->selectedIds(),QList<QUuid>({rectangle}));
        width->setFocus(); width->selectAll(); QTest::keyClicks(width,"0"); QTest::keyClick(width,Qt::Key_Return);
        QCOMPARE(std::get<QRectF>(document->shape(rectangle)->geometryMm).width(),60.0); QVERIFY(redo->isEnabled());
        QTest::keyClick(width,Qt::Key_Escape);
        // noChange: 同じ値の再確定では履歴・Redo分岐を変えないことの監視。
        QSignalSpy noChange(document,&DrawingDocument::historyChanged);
        width->setFocus(); width->selectAll(); QTest::keyClicks(width,"60"); QTest::keyClick(width,Qt::Key_Return);
        QCOMPARE(noChange.count(),0); QVERIFY(redo->isEnabled());
        // textHistory: 入力途中の文字Undoで文書履歴を進めないことの監視。
        QSignalSpy textHistory(document,&DrawingDocument::historyChanged);
        width->setFocus(); width->selectAll(); QTest::keyClicks(width,"77"); QTest::keyClick(width,Qt::Key_Z,Qt::ControlModifier);
        QCOMPARE(textHistory.count(),0); QVERIFY(document->shape(circle)); QVERIFY(width->text() != "77");
        QTest::keyClick(width,Qt::Key_Z,Qt::ControlModifier|Qt::ShiftModifier);
        QCOMPARE(width->text(),QString("77")); QCOMPARE(textHistory.count(),0);
        QCOMPARE(std::get<QRectF>(document->shape(rectangle)->geometryMm).width(),60.0); QVERIFY(document->canRedo());
        QTest::keyClick(width,Qt::Key_Escape); redo->trigger(); QCOMPARE(width->text(),QString("100"));
        width->setFocus(); QVERIFY(!width->isModified());
        QTest::keyClick(width,Qt::Key_Z,Qt::ControlModifier); QCOMPARE(width->text(),QString("60"));
        QVERIFY(document->canRedo());
        // PDF参照・PDF補正中は文書の履歴操作を無効にする。
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(1); QVERIFY(!undo->isEnabled()); QVERIFY(!redo->isEnabled());
        window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0); QVERIFY(undo->isEnabled()); QVERIFY(redo->isEnabled());
        window.findChild<QAction*>("calibratePdfAction")->trigger(); QVERIFY(!undo->isEnabled());
        window.findChild<PdfCalibrationDialog*>()->reject(); QVERIFY(undo->isEnabled());
        // 削除を取り消して元の描画順とヒット判定の手前・奥を復元する。
        QVERIFY(document->removeShapes({line,rectangle})); undo->trigger();
        QCOMPARE(view->shapeItems().size(),3);
        QVERIFY(qgraphicsitem_cast<QGraphicsLineItem*>(view->shapeItems()[0]));
        QVERIFY(qgraphicsitem_cast<QGraphicsRectItem*>(view->shapeItems()[1]));
        QVERIFY(qgraphicsitem_cast<QGraphicsEllipseItem*>(view->shapeItems()[2]));
        QVERIFY(view->shapeItems()[0]->zValue()<view->shapeItems()[1]->zValue());
        view->setActiveTool(DrawingTool::Selection);
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({0,0}));
        QCOMPARE(view->selectionModel()->selectedIds(),QList<QUuid>({rectangle}));
        QVERIFY(document->shape(circle));
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-undo-redo.png"));
    }
    /// @brief ナビゲーションの実操作で追加・名前・表示・作成先と図形所属を検証する。
    void layerNavigationIntegration() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のPDF選択を閉じるタイマー。
        QTimer startup;
        // コールバック: 空の作業領域でレイヤーと図形の連携を検証する。
        connect(&startup,&QTimer::timeout,[] {
            // widget: 起動中のトップレベル画面。
            for (auto *widget : QApplication::topLevelWidgets())
                // dialog: 起動時に取り消すファイル選択。
                if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
        });
        startup.start(20);
        // window: ナビゲーションと情報パレットを表示するメイン画面。
        MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        // view: レイヤーごとの図形を表示するCADビュー。
        auto *view = window.findChild<DrawingView*>();
        // document: レイヤー・図形の正本。
        auto *document = view->document();
        // navigation: レイヤー操作のパネル。
        auto *navigation = window.findChild<NavigationPanel*>(); QVERIFY(navigation);
        // list: UUIDを保持する4列の一覧。
        auto *list = navigation->findChild<QTreeWidget*>("layerList"); QVERIFY(list);
        QCOMPARE(list->topLevelItemCount(),1); QCOMPARE(list->topLevelItem(0)->text(2),QString("レイヤー1"));
        // first: 初期レイヤーUUID。rectangle: 初期レイヤーの図形UUID。
        const auto first = document->activeLayerId();
        const auto rectangle = document->addShape(QRectF(-60,-20,40,40));
        QTest::mouseClick(navigation->findChild<QPushButton*>("addLayerButton"),Qt::LeftButton);
        QCOMPARE(list->topLevelItemCount(),2);
        // second: 追加したアクティブレイヤーUUID。circle: そこに作成する図形UUID。
        const auto second = document->activeLayerId(); QVERIFY(second != first);
        const auto circle = document->addShape(CircleGeometry{{40,0},15}); QCOMPARE(document->shape(circle)->layerId,second);
        // cell: 対象セルの中央をクリックする画面位置生成器。
        // row: レイヤー行。column: 表示状態・作成先・名前の列番号。
        const auto cell = [list](int row,int column) { return QPoint(list->header()->sectionViewportPosition(column)+list->header()->sectionSize(column)/2,list->visualItemRect(list->topLevelItem(row)).center().y()); };
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(0,1)); QCOMPARE(document->activeLayerId(),first);
        QVERIFY(view->selectionModel()->selectOnly(circle));
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,0));
        QCOMPARE(document->layer(second)->visibility,LayerVisibility::Hidden); QVERIFY(!view->shapeItems().last()->isVisible());
        QVERIFY(view->selectionModel()->selectedIds().isEmpty());
        QVERIFY(window.findChild<QLineEdit*>("edit_radius")->text().isEmpty());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,0));
        QCOMPARE(document->layer(second)->visibility,LayerVisibility::Gray); QVERIFY(view->shapeItems().last()->isVisible());
        QVERIFY(!view->selectionModel()->selectOnly(circle));
        QCOMPARE(qgraphicsitem_cast<QGraphicsEllipseItem*>(view->shapeItems().last())->pen().color(),QColor("#999999"));
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene({40,0})); QVERIFY(view->selectedShapeItems().isEmpty());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,1));
        QCOMPARE(document->activeLayerId(),second); QCOMPARE(document->layer(second)->visibility,LayerVisibility::Visible);
        QVERIFY(view->selectionModel()->selectOnly(rectangle));
        QVERIFY(document->setRectangleBounds(rectangle,QRectF(-60,-20,50,40))); // 通常表示なら非アクティブでも編集できる。
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,0));
        QVERIFY(!document->canCreateShape()); view->setActiveTool(DrawingTool::Line); QVERIFY(!view->submitDrawingPoint({0,0}));
        QVERIFY(!window.findChild<QWidget*>("drawingPointPanel")->isEnabled());
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,1));
        QVERIFY(view->submitDrawingPoint({0,60})); QVERIFY(view->submitDrawingPoint({40,60}));
        QCOMPARE(document->shapes().last().layerId,second);
        QVERIFY(view->submitDrawingPoint({0,80})); QCOMPARE(view->draftPointCount(),1);
        QVERIFY(document->setActiveLayer(first)); QCOMPARE(view->draftPointCount(),0); QVERIFY(document->setActiveLayer(second));
        // editor: レイヤー名のダブルクリックで開くインライン入力欄。
        QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,2));
        QTest::mouseDClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,cell(1,2));
        QTRY_VERIFY(list->findChild<QLineEdit*>());
        auto *editor = list->findChild<QLineEdit*>(); editor->setText(QString::fromUtf8("照明")); QTest::keyClick(editor,Qt::Key_Return);
        QTRY_COMPARE(document->layer(second)->name,QString("照明")); QCOMPARE(document->shape(circle)->layerId,second);
        QVERIFY(document->undo()); QCOMPARE(list->topLevelItem(1)->text(2),QString("レイヤー2"));
        QVERIFY(document->redo()); QCOMPARE(list->topLevelItem(1)->text(2),QString("照明"));
        list->topLevelItem(1)->setText(2,QString("レイヤー1"));
        QCOMPARE(document->layer(second)->name,QString("照明"));
        QVERIFY(!navigation->findChild<QLabel*>("layerError")->text().isEmpty());
        // dock: 閉じても表示メニューから再表示できるナビゲーションドック。
        auto *dock = window.findChild<QDockWidget*>("navigationDock"); dock->close(); dock->toggleViewAction()->trigger(); QVERIFY(dock->isVisible());
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step4-layers.png"));
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(1); QVERIFY(!navigation->isEnabled());
        window.findChild<QTabWidget*>("workspaceTabs")->setCurrentIndex(0); QVERIFY(navigation->isEnabled());
    }
    /// @brief UUIDの正本からの表示同期、通知の一意性、移動中の選択変更を検証する。
    void selectionIdsDriveDisplayAndNotifications() {
        // settings: 実寸座標と用紙の換算に使う設定モデル。
        DrawingSettings settings;
        // view: 選択モデルと表示アイテムを接続して検証するビュー。
        DrawingView view(&settings); view.resize(900,800); view.show(); QTest::qWait(10);
        // document: ビューが所有する確定図形モデル。
        auto *document = view.document();
        // first: 先頭の矩形のUUID。
        const auto first = document->addShape(QRectF(-60,-20,40,40));
        // second: 次に描画する円のUUID。
        const auto second = document->addShape(CircleGeometry{{40,0},10});
        // selection: 表示フラグではなくUUID集合を正本とする選択モデル。
        auto *selection = view.selectionModel();
        // changed: ビュー互換の通知も、実際の選択変更と1対1であることを確認する。
        QSignalSpy changed(&view, &DrawingView::shapeSelectionChanged);
        QVERIFY(selection->setSelectedIds({second,first})); QCOMPARE(changed.count(), 1);
        QCOMPARE(selection->selectedIds(), QList<QUuid>({first,second}));
        // item: 選択表示に反映されていることを確認するシーン所有アイテム。
        for (auto *item : view.shapeItems()) QVERIFY(item->isSelected());
        QVERIFY(selection->setSelectedIds({first,second,first})); QCOMPARE(changed.count(), 1);
        view.shapeItems().first()->setSelected(false);
        QVERIFY(view.shapeItems().first()->isSelected()); QCOMPARE(changed.count(), 1);
        // start: 選択済み矩形の内部を押す位置（画面px）。
        const QPoint start = view.mapFromScene({-40,0});
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(changed.count(), 1); QCOMPARE(selection->count(), 2);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(view.viewport(), start+QPoint(40,20));
        QVERIFY(view.shapeItems().first()->pos() != QPointF());
        QVERIFY(selection->selectOnly(second)); QCOMPARE(changed.count(), 2);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start+QPoint(40,20));
        QCOMPARE(document->shape(first)->positionMm, QPointF());
        QCOMPARE(view.shapeItems().first()->pos(), QPointF());
        QVERIFY(document->removeShapes({first})); QCOMPARE(changed.count(), 2);
        QVERIFY(document->removeShapes({second})); QCOMPARE(changed.count(), 3);
        QVERIFY(selection->selectedIds().isEmpty()); QVERIFY(view.selectedShapeItems().isEmpty());
    }
    /// @brief 選択件数・種類の表示と、PDF参照・補正中に選択が維持されることを検証する。
    void selectionStatusAndPdfGuards() {
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        // startup: 起動時のPDF選択をキャンセルする自動応答タイマー。
        QTimer startup;
        // コールバック: 起動ダイアログを閉じ、空の作業領域で状態表示を検証する。
        connect(&startup, &QTimer::timeout, [] {
            // widget: トップレベルの画面を走査する、所有しない参照。
            for (auto *widget : QApplication::topLevelWidgets()) {
                // dialog: 見つかったファイル選択ダイアログ。所有権は取得しない。
                if (auto *dialog = qobject_cast<QFileDialog*>(widget); dialog && dialog->isVisible()) dialog->reject();
            }
        });
        startup.start(20);
        // window: 選択状態欄とPDF操作を統合して検証するメイン画面。
        MainWindow window; startup.stop(); window.show(); QTest::qWait(10);
        // view: メイン画面が所有するCADビュー。
        auto *view = window.findChild<DrawingView*>();
        // status: 選択モデルの件数・種類を表示する状態欄。
        auto *status = window.findChild<QLabel*>("selectionStatusLabel");
        // tabs: 作業領域とPDF参照を切り替えるタブ。
        auto *tabs = window.findChild<QTabWidget*>("workspaceTabs");
        QVERIFY(view && status && tabs); QVERIFY(status->text().contains("0"));
        // info: 右側の情報パレット。選択モデルの変更へ追従する。
        auto *info = window.findChild<ObjectInfoPanel*>();
        // dock: 表示・切り離し・再表示を検証する、メイン画面所有の情報ドック。
        auto *dock = window.findChild<QDockWidget*>("objectInfoDock");
        QVERIFY(info && dock); QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
        // first: 単独選択時に種類が表示される円のUUID。
        const auto first = view->document()->addShape(CircleGeometry{{-40,0},15});
        // second: 複数選択状態を作る矩形のUUID。
        const auto second = view->document()->addShape(QRectF(20,-20,40,40));
        QVERIFY(view->selectionModel()->selectOnly(first)); QVERIFY(status->text().contains(QString::fromUtf8("円")));
        QCOMPARE(info->findChild<QLabel*>("property_radius")->text(), QString("15 mm"));
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-properties.png"));
        QVERIFY(view->selectionModel()->toggle(second)); QVERIFY(status->text().contains("2"));
        QVERIFY(info->findChild<QLabel*>("property_radius")->text().isEmpty());
        dock->setFloating(true); QTRY_VERIFY(dock->isFloating());
        dock->resize(300,400); dock->setFloating(false); QTRY_VERIFY(!dock->isFloating());
        dock->close(); QVERIFY(!dock->isVisible()); dock->toggleViewAction()->trigger(); QTRY_VERIFY(dock->isVisible());
        QCOMPARE(view->selectionModel()->count(), 2);
        if (qEnvironmentVariableIsSet("STAGECAD_CAPTURE")) QVERIFY(window.grab().save("step3-selection.png"));
        tabs->setCurrentIndex(1); QVERIFY(!status->isVisible()); QVERIFY(!info->isEnabled()); QVERIFY(!view->deleteSelectedShapes());
        QCOMPARE(view->selectionModel()->count(), 2); tabs->setCurrentIndex(0); QVERIFY(status->isVisible()); QVERIFY(info->isEnabled());
        QVERIFY(window.importPdf(QString::fromUtf8(SAMPLE_PDF_PATH)));
        window.findChild<QAction*>("calibratePdfAction")->trigger();
        // dialog: 開いている補正画面。操作制限を解除するためにキャンセルする。
        auto *dialog = window.findChild<PdfCalibrationDialog*>(); QVERIFY(dialog);
        QVERIFY(!view->deleteSelectedShapes()); QVERIFY(!info->isEnabled()); QCOMPARE(view->selectionModel()->count(), 2);
        dialog->reject(); QVERIFY(info->isEnabled()); QCOMPARE(view->selectionModel()->count(), 2);
        QTest::keyClick(view, Qt::Key_Escape); QVERIFY(status->text().contains("0"));
        QVERIFY(view->selectedShapeItems().isEmpty());
    }
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
