#include "drawingdocument.h"
#include <QSignalSpy>
#include <QTest>
#include <limits>

class DocumentTests : public QObject {
    Q_OBJECT
private slots:
    void independentDataAndStableIds() {
        DrawingDocument document;
        const QPolygonF polygon({{-30,-20}, {30,-20}, {0,40}});
        const auto line = document.addShape(QLineF(-20,10,40,-10));
        const auto rect = document.addShape(QRectF(-10,-20,30,40));
        const auto circle = document.addShape(CircleGeometry{{10,20}, 25});
        const auto poly = document.addShape(polygon);
        QCOMPARE(document.shapes().size(), 4);
        QCOMPARE(document.shape(line)->kind(), ShapeKind::Line);
        QCOMPARE(document.shape(rect)->kind(), ShapeKind::Rectangle);
        QCOMPARE(document.shape(circle)->kind(), ShapeKind::Circle);
        QCOMPARE(document.shape(poly)->kind(), ShapeKind::Polygon);
        QSet<QUuid> unique;
        for (const auto &shape : document.shapes()) { QVERIFY(!shape.id.isNull()); unique.insert(shape.id); }
        QCOMPARE(unique.size(), 4);
        QVERIFY(document.translateShapes({line,rect,circle,poly}, {123.125,-456.5}));
        QCOMPARE(document.shape(line)->positionMm, QPointF(123.125,-456.5));
        QCOMPARE(std::get<QLineF>(document.shape(line)->geometryMm), QLineF(-20,10,40,-10));
        QCOMPARE(std::get<QPolygonF>(document.shape(poly)->geometryMm), polygon);
        QCOMPARE(document.shape(circle)->boundsMm(), QRectF(108.125,-461.5,50,50));
        auto snapshot = document.shape(poly); std::get<QPolygonF>(snapshot->geometryMm)[0] = {999,999};
        QCOMPARE(std::get<QPolygonF>(document.shape(poly)->geometryMm), polygon);
        QVERIFY(document.removeShapes({rect})); QVERIFY(!document.shape(rect));
        QCOMPARE(document.shapes()[0].id,line); QCOMPARE(document.shapes()[1].id,circle);
        QCOMPARE(document.shapes()[2].id,poly);
        const auto replacement = document.addShape(QRectF(0,0,10,10)); QVERIFY(!unique.contains(replacement));
    }
    void validationAndAtomicChanges() {
        DrawingDocument document; QSignalSpy added(&document, &DrawingDocument::shapeAdded);
        QSignalSpy changed(&document, &DrawingDocument::shapeChanged);
        QSignalSpy removed(&document, &DrawingDocument::shapeRemoved);
        QSignalSpy about(&document, &DrawingDocument::aboutToChange);
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        QVERIFY(document.addShape(QLineF(0,0,0,0)).isNull());
        QVERIFY(document.addShape(QLineF(nan,0,1,1)).isNull());
        QVERIFY(document.addShape(QRectF(0,0,-10,20)).isNull());
        QVERIFY(document.addShape(CircleGeometry{{0,0}, inf}).isNull());
        QVERIFY(document.addShape(CircleGeometry{{9999999,0}, 10}).isNull());
        QVERIFY(document.addShape(QPolygonF({{0,0},{10,0},{20,0}})).isNull());
        QCOMPARE(added.count(), 0); QCOMPARE(about.count(), 0);
        const auto a = document.addShape(QLineF(9999900,0,10000000,0));
        const auto b = document.addShape(QRectF(-20,-20,40,40));
        about.clear();
        QVERIFY(!document.translateShapes({a,b},{1,0}));
        QVERIFY(!document.translateShapes({a,b},{nan,0}));
        QVERIFY(!document.translateShapes({b,QUuid::createUuid()},{10,10}));
        QVERIFY(!document.removeShapes({a,QUuid::createUuid()}));
        QVERIFY(!document.removeShapes({}));
        QCOMPARE(changed.count(), 0); QCOMPARE(removed.count(), 0); QCOMPARE(about.count(), 0);
        QCOMPARE(document.shape(a)->positionMm, QPointF()); QCOMPARE(document.shape(b)->positionMm, QPointF());
        bool sawAtomicState = false;
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
