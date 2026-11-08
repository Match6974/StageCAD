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
