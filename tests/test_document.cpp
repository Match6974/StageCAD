/**
 * @file test_document.cpp
 * @brief UIに依存しない図形モデルのID・形状・一括操作・通知の整合性を検証する。
 */

#include "drawingdocument.h"
#include <QSignalSpy>
#include <QTest>
#include <limits>

/// @brief 図形モデルの検証・ID・通知の回帰テストをまとめる。
class DocumentTests : public QObject {
    Q_OBJECT
private slots:
    /// @brief 初期レイヤー・追加採番・名前検証・所属UUIDとレイヤー履歴を検証する。
    void layerIdentityAndHistory() {
        // document: 常に1つ以上のレイヤーを所有する文書。
        DrawingDocument document; QCOMPARE(document.layers().size(),1); QCOMPARE(document.layers().first().name,QString("レイヤー1"));
        // first: 初期のアクティブレイヤーUUID。
        const auto first = document.activeLayerId();
        // existing: レイヤー追加前から存在する図形UUID。
        const auto existing = document.addShape(QLineF(0,0,10,0)); QCOMPARE(document.shape(existing)->layerId,first);
        // second: 採番した新規アクティブレイヤーのUUID。
        const auto second = document.addLayer(); QVERIFY(!second.isNull()); QCOMPARE(document.activeLayerId(),second);
        QCOMPARE(document.layer(second)->name,QString("レイヤー2"));
        // shape: 新規レイヤーに作成した図形。
        const auto shape = document.addShape(QLineF(20,0,30,0)); QCOMPARE(document.shape(shape)->layerId,second);
        QVERIFY(document.setLineEndpoints(shape,QLineF(20,0,40,0))); QCOMPARE(document.shape(shape)->layerId,second);
        QVERIFY(document.renameLayer(second,"照明")); QCOMPARE(document.layer(second)->name,QString("照明"));
        QVERIFY(!document.renameLayer(second,"レイヤー1")); QVERIFY(!document.renameLayer(second,"  ")); QVERIFY(!document.renameLayer(second,"照明\n回路"));
        QVERIFY(document.addLayer("レイヤー1").isNull()); QVERIFY(document.addLayer("   ").isNull());
        QVERIFY(document.undo()); QCOMPARE(document.layer(second)->name,QString("レイヤー2"));
        QVERIFY(document.undo()); QCOMPARE(std::get<QLineF>(document.shape(shape)->geometryMm).length(),10.0);
        QVERIFY(document.undo()); QVERIFY(!document.shape(shape)); QVERIFY(document.undo());
        QCOMPARE(document.layers().size(),1); QCOMPARE(document.activeLayerId(),first); QCOMPARE(document.shape(existing)->layerId,first);
        QVERIFY(document.redo()); QCOMPARE(document.activeLayerId(),second); QVERIFY(document.redo()); QCOMPARE(document.shape(shape)->layerId,second);
    }
    /// @brief 表示・グレイ・非表示の編集制御と、ビュー操作がRedoを壊さないことを検証する。
    void layerVisibilityGuards() {
        // document: 複数レイヤーの図形を保持する文書。
        DrawingDocument document;
        // first: 既存レイヤーのUUID。a: このレイヤーに所属する図形。
        const auto first = document.activeLayerId();
        const auto a = document.addShape(QRectF(0,0,20,20));
        // second: 新規レイヤーのUUID。b: その上に作成する図形。
        const auto second = document.addLayer();
        const auto b = document.addShape(CircleGeometry{{50,0},10});
        QVERIFY(document.setActiveLayer(first)); QVERIFY(document.canEditShape(b));
        QVERIFY(document.setLayerVisibility(second,LayerVisibility::Gray)); QVERIFY(!document.canEditShape(b));
        QVERIFY(!document.translateShapes({a,b},{10,0})); QVERIFY(!document.removeShapes({b}));
        QVERIFY(!document.setCircleRadius(b,20));
        // candidate: UI外からの更新もグレイ表示では拒否する候補。
        auto candidate = *document.shape(b); candidate.positionMm = {10,0}; QVERIFY(!document.updateShape(candidate));
        QVERIFY(document.setLayerVisibility(first,LayerVisibility::Hidden)); QVERIFY(!document.canCreateShape());
        QVERIFY(document.addShape(QLineF(0,0,10,0)).isNull());
        QVERIFY(document.setActiveLayer(first)); QCOMPARE(document.layer(first)->visibility,LayerVisibility::Visible);
        QVERIFY(document.setActiveLayer(second)); QCOMPARE(document.layer(second)->visibility,LayerVisibility::Visible);
        QVERIFY(document.setCircleRadius(b,20)); QVERIFY(document.undo()); QVERIFY(document.canRedo());
        QVERIFY(document.setLayerVisibility(second,LayerVisibility::Hidden)); QVERIFY(document.canRedo());
        QVERIFY(document.renameLayer(second,"照明")); QVERIFY(!document.canRedo());
        QVERIFY(document.undo()); QCOMPARE(document.layer(second)->visibility,LayerVisibility::Hidden);
        QVERIFY(document.redo()); QCOMPARE(document.layer(second)->name,QString("照明"));
        QVERIFY(!document.setLayerVisibility(QUuid::createUuid(),LayerVisibility::Visible));
        QVERIFY(!document.setLayerVisibility(first,static_cast<LayerVisibility>(99)));
        QVERIFY(!document.setActiveLayer(QUuid::createUuid()));
    }
    /// @brief 縮尺をレイヤーごとに保持し、実寸・表示設定・Redo分岐を壊さないことを検証する。
    void layerScalesAndVisibility() {
        // document: 共有表示とは独立してレイヤー縮尺を保持する文書。
        DrawingDocument document;
        // first: 初期レイヤー。a: 実寸1000mmの線。
        const auto first = document.activeLayerId();
        const auto a = document.addShape(QLineF(0,0,1000,0));
        // second: 初期1:1で追加する第2レイヤー。b: その上の円。
        const auto second = document.addLayer(); QCOMPARE(document.layer(second)->scaleDenominator,1.0);
        const auto b = document.addShape(CircleGeometry{{50,50},10});
        QVERIFY(document.setCircleRadius(b,20)); QVERIFY(document.undo()); QVERIFY(document.canRedo());
        // history: 縮尺操作は履歴を増やさず、Redoも維持することの監視。
        QSignalSpy history(&document,&DrawingDocument::historyChanged);
        QVERIFY(document.setLayerScale(first,100));
        QCOMPARE(document.effectiveLayerVisibility(first),LayerVisibility::Hidden); QVERIFY(!document.canEditShape(a));
        QCOMPARE(document.layer(first)->visibility,LayerVisibility::Visible); QVERIFY(document.canEditShape(b));
        QVERIFY(document.setActiveLayer(first)); QCOMPARE(document.effectiveLayerVisibility(second),LayerVisibility::Hidden);
        QVERIFY(document.canEditShape(a)); QVERIFY(!document.canEditShape(b));
        QCOMPARE(std::get<QLineF>(document.shape(a)->geometryMm).length(),1000.0);
        QVERIFY(document.setLayerScale(second,100)); QCOMPARE(document.effectiveLayerVisibility(second),LayerVisibility::Visible);
        QVERIFY(document.setLayerVisibility(second,LayerVisibility::Gray));
        QVERIFY(document.setLayerScale(first,50)); QCOMPARE(document.effectiveLayerVisibility(second),LayerVisibility::Hidden);
        QCOMPARE(document.layer(second)->visibility,LayerVisibility::Gray);
        QVERIFY(document.setLayerScale(first,100)); QCOMPARE(document.effectiveLayerVisibility(second),LayerVisibility::Gray);
        QVERIFY(document.setLayerVisibility(second,LayerVisibility::Hidden));
        QVERIFY(document.setLayerScale(first,50)); QVERIFY(document.setLayerScale(first,100));
        QCOMPARE(document.effectiveLayerVisibility(second),LayerVisibility::Hidden);
        QCOMPARE(history.count(),0); QVERIFY(document.canRedo());
        // invalid: 元の値・表示・履歴を変えてはいけない縮尺指定。
        for (const double invalid : {0.0,-1.0,10001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
            QVERIFY(!document.setLayerScale(first,invalid));
        QVERIFY(!document.setLayerScale(QUuid::createUuid(),50));
        QCOMPARE(document.layer(first)->scaleDenominator,100.0); QVERIFY(document.canRedo());
        QVERIFY(document.renameLayer(first,"照明")); QVERIFY(document.setLayerScale(first,25)); QVERIFY(document.undo());
        QCOMPARE(document.layer(first)->name,QString("レイヤー1")); QCOMPARE(document.layer(first)->scaleDenominator,25.0);
        // third: 追加を取り消しても、Redoでは履歴外の縮尺設定も復元するレイヤー。
        const auto third = document.addLayer(); QVERIFY(document.setLayerScale(third,75));
        QVERIFY(document.undo()); QVERIFY(!document.layer(third));
        QVERIFY(document.redo()); QCOMPARE(document.layer(third)->scaleDenominator,75.0);
    }
    /// @brief 作成・編集・複数移動・削除のUndo／Redoで正確な値と描画順を復元する。
    void historyRestoresValuesIdsAndOrder() {
        // document: 一連の確定操作の履歴を所有する文書。
        DrawingDocument document; QVERIFY(!document.canUndo()); QVERIFY(!document.canRedo());
        // first: 描画順の先頭へ復元する線のUUID。
        const auto first = document.addShape(QLineF(0.123456789,0,10,0));
        // middle: 描画順の中間へ復元する回転矩形のUUID。
        const auto middle = document.addShape(QRectF(0,0,30,40));
        // last: 描画順の末尾で維持する円のUUID。
        const auto last = document.addShape(CircleGeometry{{30,40},10});
        // rotated: 回転と位置を同時に編集する候補。
        auto rotated = *document.shape(middle); rotated.rectangleAngleDeg = 37; rotated.positionMm = {15,25};
        QVERIFY(document.updateShape(rotated));
        QVERIFY(document.translateShapes({first,middle,first},{100,200}));
        // beforeDelete: 配置・精度・描画順を確認する変更前のコピー。
        const auto beforeDelete = document.shapes();
        QVERIFY(document.removeShapes({first,middle})); QCOMPARE(document.shapes().size(),1);
        QVERIFY(document.undo()); QCOMPARE(document.shapes().size(),3);
        QCOMPARE(document.shapes()[0].id,first); QCOMPARE(document.shapes()[1].id,middle); QCOMPARE(document.shapes()[2].id,last);
        QCOMPARE(document.shape(first)->positionMm,beforeDelete[0].positionMm);
        QCOMPARE(document.shape(middle)->rectangleAngleDeg,37.0);
        QVERIFY(document.undo()); QCOMPARE(document.shape(first)->positionMm,QPointF()); QCOMPARE(document.shape(middle)->positionMm,QPointF(15,25));
        QVERIFY(document.undo()); QCOMPARE(document.shape(middle)->rectangleAngleDeg,0.0);
        QVERIFY(document.redo()); QCOMPARE(document.shape(middle)->rectangleAngleDeg,37.0);
        QVERIFY(document.redo()); QCOMPARE(document.shape(first)->positionMm,QPointF(100,200));
        QVERIFY(document.redo()); QVERIFY(!document.shape(first)); QVERIFY(!document.shape(middle));
        QVERIFY(document.undo());
        QCOMPARE(std::get<QLineF>(document.shape(first)->geometryMm).x1(),0.123456789);
        // redo破棄: Undo後の新しい有効変更だけがやり直し分岐を破棄する。
        QVERIFY(document.setCircleRadius(last,20)); QVERIFY(!document.canRedo()); QVERIFY(!document.redo());
        while (document.canUndo()) QVERIFY(document.undo());
        QVERIFY(document.shapes().isEmpty()); QVERIFY(!document.undo());
        while (document.canRedo()) QVERIFY(document.redo());
        QCOMPARE(document.shapes().size(),3); QCOMPARE(std::get<CircleGeometry>(document.shape(last)->geometryMm).radiusMm,20.0);
    }
    /// @brief 無効値・同値・再入を履歴へ追加せず、Redo分岐も維持することを検証する。
    void invalidAndNoopKeepHistory() {
        // document: 無効操作で履歴が壊れないことを検証する文書。
        DrawingDocument document;
        // id: 配置表現の違いだけでは履歴を増やさない対象UUID。
        const auto id = document.addShape(CircleGeometry{{10,20},5});
        QVERIFY(document.translateShapes({id},{100,200})); QVERIFY(document.setCircleRadius(id,10)); QVERIFY(document.undo());
        // notifications: 無効・無変更で履歴のindexが変わらないことを確認する監視。
        QSignalSpy notifications(&document,&DrawingDocument::historyChanged);
        QVERIFY(!document.setCircleRadius(id,0)); QVERIFY(!document.setCircleRadius(id,std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(document.translateShapes({id},{0,0}));
        // canonical: 同じ実寸中心と半径を、配置0へ統合した等価な候補。
        auto canonical = *document.shape(id); std::get<CircleGeometry>(canonical.geometryMm).centerMm += canonical.positionMm; canonical.positionMm = {};
        QVERIFY(document.updateShape(canonical)); QVERIFY(document.canRedo()); QCOMPARE(notifications.count(),0);
        // reentered: 復元中のUndo再入が拒否されるかを確認する結果。
        bool reentered = true;
        // コールバック: 復元通知内で履歴をもう1段階進めないことを検証する。
        connect(&document,&DrawingDocument::aboutToChange,&document,[&] { reentered = document.undo(); });
        QVERIFY(document.redo()); QVERIFY(!reentered); QCOMPARE(std::get<CircleGeometry>(document.shape(id)->geometryMm).radiusMm,10.0);
    }
    /// @brief 円弧・多角形の頂点と開閉属性を、Undo／Redoで完全に復元する。
    void extendedPropertiesHistory() {
        // document: 図形属性の履歴を検証する文書。
        DrawingDocument document;
        // circle: 円弧角を戻す対象のUUID。
        const auto circle = document.addShape(CircleGeometry{{10,20},5});
        // polygon: 頂点列と開閉を戻す対象のUUID。
        const auto polygon = document.addShape(QPolygonF({{0,0},{30,0},{10,20}}));
        // edited: 円弧と多角形の属性変更候補。
        auto edited = *document.shape(circle); std::get<CircleGeometry>(edited.geometryMm).startAngleDeg = 45;
        std::get<CircleGeometry>(edited.geometryMm).sweepAngleDeg = 90; QVERIFY(document.updateShape(edited));
        edited = *document.shape(polygon); edited.polygonClosed = false;
        std::get<QPolygonF>(edited.geometryMm).append({-10,10}); QVERIFY(document.updateShape(edited));
        QVERIFY(document.undo()); QVERIFY(document.shape(polygon)->polygonClosed); QCOMPARE(std::get<QPolygonF>(document.shape(polygon)->geometryMm).size(),3);
        QVERIFY(document.undo()); QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).sweepAngleDeg,360.0);
        QVERIFY(document.redo()); QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).startAngleDeg,45.0);
        QVERIFY(document.redo()); QVERIFY(!document.shape(polygon)->polygonClosed); QCOMPARE(std::get<QPolygonF>(document.shape(polygon)->geometryMm).size(),4);
    }
    /// @brief 拡張属性のコピー・範囲検証・再入拒否と型・UUIDの維持を検証する。
    void propertyUpdatesAreAtomic() {
        // document: 型と描画順を維持して更新する文書。
        DrawingDocument document;
        // id: 基準点・回転を保持する矩形UUID。
        const auto id = document.addShape(QRectF(0,0,40,20));
        // candidate: 位置と角度を同時に検証する更新候補。
        auto candidate = *document.shape(id); candidate.rectangleAngleDeg = 90; candidate.positionMm = {100,200};
        QVERIFY(document.updateShape(candidate)); QCOMPARE(document.shape(id)->rectangleAngleDeg,90.0);
        QCOMPARE(document.shape(id)->boundsMm(),QRectF(80,200,20,40));
        // changes: 拒否した候補では通知しないことを確認する監視。
        QSignalSpy changes(&document,&DrawingDocument::shapeChanged);
        candidate.rectangleAngleDeg = std::numeric_limits<double>::infinity(); QVERIFY(!document.updateShape(candidate));
        candidate = *document.shape(id); candidate.geometryMm = CircleGeometry{{0,0},10}; QVERIFY(!document.updateShape(candidate));
        candidate = *document.shape(id); candidate.positionMm = {10000000,10000000}; QVERIFY(!document.updateShape(candidate));
        QCOMPARE(changes.count(),0); QCOMPARE(document.shape(id)->boundsMm(),QRectF(80,200,20,40));
        // circle: 円弧に必要な開始角と円弧角を保持する円UUID。
        const auto circle = document.addShape(CircleGeometry{{0,0},10});
        candidate = *document.shape(circle); std::get<CircleGeometry>(candidate.geometryMm).sweepAngleDeg = 90;
        QVERIFY(document.updateShape(candidate)); QVERIFY(document.setCircleRadius(circle,20));
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).sweepAngleDeg,90.0);
        candidate = *document.shape(circle); std::get<CircleGeometry>(candidate.geometryMm).sweepAngleDeg = 361;
        QVERIFY(!document.updateShape(candidate));
        // polygon: 開いた形状を2点まで許容し、閉じるときには3点以上を要求する頂点列UUID。
        const auto polygon = document.addShape(QPolygonF({{0,0},{10,0},{10,10}}));
        candidate = *document.shape(polygon); candidate.polygonClosed = false;
        candidate.geometryMm = QPolygonF({{0,0},{10,0}}); QVERIFY(document.updateShape(candidate));
        candidate.polygonClosed = true; QVERIFY(!document.updateShape(candidate));
        candidate = *document.shape(polygon); std::get<QPolygonF>(candidate.geometryMm)[0] = {std::numeric_limits<double>::quiet_NaN(),0};
        QVERIFY(!document.updateShape(candidate));
        // reentered: 変更通知内の再入更新が拒否されることを検証する結果値。
        bool reentered = true;
        // コールバック: 外側の変更が確定する前に、同じ図形の更新を試みる。
        connect(&document,&DrawingDocument::aboutToChange,&document,[&] { reentered = document.updateShape(*document.shape(id)); });
        candidate = *document.shape(id); candidate.rectangleAngleDeg = 45;
        QVERIFY(document.updateShape(candidate)); QVERIFY(!reentered);
        QCOMPARE(document.shapes()[0].id,id); QCOMPARE(document.shapes()[1].id,circle); QCOMPARE(document.shapes()[2].id,polygon);
    }
    /// @brief 矩形・円の更新で実寸・種類・IDを維持し、不正値や無変更を通知しないことを検証する。
    void rectangleAndCircleUpdates() {
        // document: 移動済み形状へ寸法更新を適用する文書。
        DrawingDocument document;
        // rect: 更新後も同じUUIDと描画順を保つ四角形。
        const auto rect = document.addShape(QRectF(10,20,30,40));
        // circle: 中心位置を変えずに半径だけを変える円。
        const auto circle = document.addShape(CircleGeometry{{30,40},5});
        QVERIFY(document.translateShapes({rect,circle},{100,200}));
        // changed: 有効な寸法更新だけを通知することを確認する監視。
        QSignalSpy changed(&document,&DrawingDocument::shapeChanged);
        // about: 無変更・不正値で更新準備の通知も発生しないことを確認する監視。
        QSignalSpy about(&document,&DrawingDocument::aboutToChange);
        QVERIFY(document.setRectangleBounds(rect,QRectF(110,220,30,40)));
        QVERIFY(document.setCircleRadius(circle,5)); QCOMPARE(changed.count(),0); QCOMPARE(about.count(),0);
        QVERIFY(document.setRectangleBounds(rect,QRectF(110,220,60,40)));
        QVERIFY(document.setCircleRadius(circle,12.5)); QCOMPARE(changed.count(),2); QCOMPARE(about.count(),2);
        QCOMPARE(document.shape(rect)->boundsMm(),QRectF(110,220,60,40));
        QCOMPARE(document.shape(rect)->positionMm,QPointF());
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).centerMm,QPointF(130,240));
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).radiusMm,12.5);
        QCOMPARE(document.shape(circle)->boundsMm(),QRectF(117.5,227.5,25,25));
        QCOMPARE(document.shapes()[0].id,rect); QCOMPARE(document.shapes()[1].id,circle);
        QVERIFY(!document.setRectangleBounds(rect,QRectF(110,220,0,40)));
        QVERIFY(!document.setRectangleBounds(rect,QRectF(110,220,-1,40)));
        QVERIFY(!document.setRectangleBounds(rect,QRectF(110,220,10000000,40)));
        QVERIFY(!document.setRectangleBounds(rect,QRectF(0,0,std::numeric_limits<double>::quiet_NaN(),40)));
        QVERIFY(!document.setRectangleBounds(circle,QRectF(0,0,10,10)));
        // bad: 有限正値の検証と、中心を含めた外接範囲の検証に使う不正な半径（mm）。
        for (const double bad : {0.0,-1.0,10000000.0,std::numeric_limits<double>::infinity(),
                                std::numeric_limits<double>::quiet_NaN()})
            QVERIFY(!document.setCircleRadius(circle,bad));
        QVERIFY(!document.setCircleRadius(rect,10));
        QVERIFY(!document.setCircleRadius(QUuid::createUuid(),10));
        QVERIFY(!document.setRectangleBounds(QUuid::createUuid(),QRectF(0,0,10,10)));
        QCOMPARE(changed.count(),2); QCOMPARE(about.count(),2);
        QCOMPARE(document.shape(rect)->boundsMm(),QRectF(110,220,60,40));
        QCOMPARE(document.shape(circle)->boundsMm(),QRectF(117.5,227.5,25,25));
    }
    /// @brief 直線端点の更新でID・順序を保ち、不正値や同じ値の通知を抑えることを検証する。
    void lineEndpointUpdates() {
        // document: 直線の実寸更新を適用するモデル。
        DrawingDocument document;
        // line: 移動後の線を編集してもUUIDが変わらないことを確認する対象。
        const auto line = document.addShape(QLineF(0,0,10,0));
        // rect: 直線以外を誤って編集しないことを確認する矩形UUID。
        const auto rect = document.addShape(QRectF(20,20,10,10));
        QVERIFY(document.translateShapes({line},{100,200}));
        // changed: 有効な実寸変更だけが1回通知されることを確認する監視。
        QSignalSpy changed(&document, &DrawingDocument::shapeChanged);
        // about: 無効値や同じ値ではドラッグ取消などの準備通知も出さないことを確認する。
        QSignalSpy about(&document, &DrawingDocument::aboutToChange);
        QVERIFY(document.setLineEndpoints(line, QLineF(100,200,110,200)));
        QCOMPARE(changed.count(), 0); QCOMPARE(about.count(), 0);
        QVERIFY(document.setLineEndpoints(line, QLineF(-10,20,30,50)));
        QCOMPARE(changed.count(), 1); QCOMPARE(about.count(), 1);
        QCOMPARE(document.shape(line)->positionMm, QPointF());
        QCOMPARE(std::get<QLineF>(document.shape(line)->geometryMm), QLineF(-10,20,30,50));
        QCOMPARE(document.shapes()[0].id,line); QCOMPARE(document.shapes()[1].id,rect);
        QVERIFY(!document.setLineEndpoints(line, QLineF(0,0,0,0)));
        QVERIFY(!document.setLineEndpoints(line, QLineF(0,0,10000001,0)));
        QVERIFY(!document.setLineEndpoints(line, QLineF(0,0,std::numeric_limits<double>::quiet_NaN(),0)));
        QVERIFY(!document.setLineEndpoints(rect, QLineF(0,0,10,10)));
        QVERIFY(!document.setLineEndpoints(QUuid::createUuid(), QLineF(0,0,10,10)));
        QCOMPARE(changed.count(), 1); QCOMPARE(about.count(), 1);
        QCOMPARE(std::get<QLineF>(document.shape(line)->geometryMm), QLineF(-10,20,30,50));
        // requested: 通知を受けたUIが書き換えることを想定した、呼び出し元側の可変UUID。
        QUuid requested = line;
        // コールバック: 文書自体は変更せず、呼び出し元の選択IDだけを切り替える。
        connect(&document,&DrawingDocument::aboutToChange,&document,[&requested,rect] { requested = rect; });
        QVERIFY(document.setLineEndpoints(requested,QLineF(1,2,3,4)));
        QCOMPARE(requested,rect); QCOMPARE(document.shape(rect)->kind(),ShapeKind::Rectangle);
        QCOMPARE(std::get<QLineF>(document.shape(line)->geometryMm),QLineF(1,2,3,4));
        QCOMPARE(changed.last().at(0).toUuid(),line);
    }
    /// @brief 4種類の形状、UUID、移動後の形状維持、コピー、削除後の描画順を検証する。
    void independentDataAndStableIds() {
        // document: 確定済み図形のID・形状・配置を管理する共通モデル。
        DrawingDocument document;
        // polygon: 頂点順を保持した多角形の形状または描画アイテム。
        const QPolygonF polygon({{-30,-20}, {30,-20}, {0,40}});
        // line: 共通モデルへ追加した図形のUUID。
        const auto line = document.addShape(QLineF(-20,10,40,-10));
        // rect: 共通モデルへ追加した図形のUUID。
        const auto rect = document.addShape(QRectF(-10,-20,30,40));
        // circle: 共通モデルへ追加した図形のUUID。
        const auto circle = document.addShape(CircleGeometry{{10,20}, 25});
        // poly: 共通モデルへ追加した図形のUUID。
        const auto poly = document.addShape(polygon);
        QCOMPARE(document.shapes().size(), 4);
        QCOMPARE(document.shape(line)->kind(), ShapeKind::Line);
        QCOMPARE(document.shape(rect)->kind(), ShapeKind::Rectangle);
        QCOMPARE(document.shape(circle)->kind(), ShapeKind::Circle);
        QCOMPARE(document.shape(poly)->kind(), ShapeKind::Polygon);
        // unique: 発行済みUUIDの集合。一意性と削除後に再利用しないことを確認する。
        QSet<QUuid> unique;
        // shape: 描画順に処理する確定済み図形データ。
        for (const auto &shape : document.shapes()) { QVERIFY(!shape.id.isNull()); unique.insert(shape.id); }
        QCOMPARE(unique.size(), 4);
        QVERIFY(document.translateShapes({line,rect,circle,poly}, {123.125,-456.5}));
        QCOMPARE(document.shape(line)->positionMm, QPointF(123.125,-456.5));
        QCOMPARE(std::get<QLineF>(document.shape(line)->geometryMm), QLineF(-20,10,40,-10));
        QCOMPARE(std::get<QPolygonF>(document.shape(poly)->geometryMm), polygon);
        QCOMPARE(document.shape(circle)->boundsMm(), QRectF(108.125,-461.5,50,50));
        // snapshot: モデルから取得した値データのコピー。変更が元のモデルへ漏れないことを確認する。
        auto snapshot = document.shape(poly); std::get<QPolygonF>(snapshot->geometryMm)[0] = {999,999};
        QCOMPARE(std::get<QPolygonF>(document.shape(poly)->geometryMm), polygon);
        QVERIFY(document.removeShapes({rect})); QVERIFY(!document.shape(rect));
        QCOMPARE(document.shapes()[0].id,line); QCOMPARE(document.shapes()[1].id,circle);
        QCOMPARE(document.shapes()[2].id,poly);
        // replacement: 共通モデルへ追加した図形のUUID。
        const auto replacement = document.addShape(QRectF(0,0,10,10)); QVERIFY(!unique.contains(replacement));
    }
    /// @brief 不正操作の拒否、一括更新の整合性、重複IDと通知中の再入防止を検証する。
    void validationAndAtomicChanges() {
        // document: 確定済み図形のID・形状・配置を管理する共通モデル。
        // added: shapeAddedの通知回数と引数を記録するシグナル監視。
        DrawingDocument document; QSignalSpy added(&document, &DrawingDocument::shapeAdded);
        // changed: shapeChangedの通知回数と引数を記録するシグナル監視。
        QSignalSpy changed(&document, &DrawingDocument::shapeChanged);
        // removed: shapeRemovedの通知回数と引数を記録するシグナル監視。
        QSignalSpy removed(&document, &DrawingDocument::shapeRemoved);
        // about: aboutToChangeの通知回数と引数を記録するシグナル監視。
        QSignalSpy about(&document, &DrawingDocument::aboutToChange);
        // nan: 有限値として受け付けないことを検証するNaN。
        const double nan = std::numeric_limits<double>::quiet_NaN();
        // inf: 有限値として受け付けないことを検証する無限大。
        const double inf = std::numeric_limits<double>::infinity();
        QVERIFY(document.addShape(QLineF(0,0,0,0)).isNull());
        QVERIFY(document.addShape(QLineF(nan,0,1,1)).isNull());
        QVERIFY(document.addShape(QRectF(0,0,-10,20)).isNull());
        QVERIFY(document.addShape(CircleGeometry{{0,0}, inf}).isNull());
        QVERIFY(document.addShape(CircleGeometry{{9999999,0}, 10}).isNull());
        QVERIFY(document.addShape(QPolygonF({{0,0},{10,0},{20,0}})).isNull());
        QCOMPARE(added.count(), 0); QCOMPARE(about.count(), 0);
        // a: 共通モデルへ追加した図形のUUID。
        const auto a = document.addShape(QLineF(9999900,0,10000000,0));
        // b: 共通モデルへ追加した図形のUUID。
        const auto b = document.addShape(QRectF(-20,-20,40,40));
        about.clear();
        QVERIFY(!document.translateShapes({a,b},{1,0}));
        QVERIFY(!document.translateShapes({a,b},{nan,0}));
        QVERIFY(!document.translateShapes({b,QUuid::createUuid()},{10,10}));
        QVERIFY(!document.removeShapes({a,QUuid::createUuid()}));
        QVERIFY(!document.removeShapes({}));
        QCOMPARE(changed.count(), 0); QCOMPARE(removed.count(), 0); QCOMPARE(about.count(), 0);
        QCOMPARE(document.shape(a)->positionMm, QPointF()); QCOMPARE(document.shape(b)->positionMm, QPointF());
        // sawAtomicState: 通知時に一括移動の全図形が更新済みであることを確認した結果。
        bool sawAtomicState = false;
        // コールバック: 一括更新が全件反映済みで、通知中の入れ子変更が拒否されることを検証する。
        connect(&document, &DrawingDocument::shapeChanged, &document, [&] {
            sawAtomicState = document.shape(a)->positionMm == QPointF(-10,20)
                && document.shape(b)->positionMm == QPointF(-10,20);
            QVERIFY(sawAtomicState);
            // Notification handlers may inspect but cannot nest mutations.
            QVERIFY(!document.removeShapes({a}));
        });
        QVERIFY(document.translateShapes({a,a,b},{-10,20})); QVERIFY(sawAtomicState);
        QCOMPARE(changed.count(), 2); QCOMPARE(about.count(), 1);
        QVERIFY(document.removeShapes({a,a,b})); QCOMPARE(removed.count(), 2);
        QVERIFY(document.shapes().isEmpty());
    }
};
QTEST_GUILESS_MAIN(DocumentTests)
#include "test_document.moc"
