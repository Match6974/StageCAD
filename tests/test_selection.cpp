/**
 * @file test_selection.cpp
 * @brief UIなしで選択UUIDの検証・変更通知・削除・文書寿命を検証する。
 */
#include "shapeselectionmodel.h"
#include "drawingdocument.h"
#include <QSignalSpy>
#include <QTest>

/// @brief 選択モデルの状態遷移と、正本データからの独立性を検証する。
class SelectionTests : public QObject {
    Q_OBJECT
private slots:
    /// @brief 順序・重複・不正ID・単独選択と、変更時だけの通知を検証する。
    void validatedSelectionAndNotifications() {
        // document: 形状とUUIDを保持するテスト用の確定図形モデル。
        DrawingDocument document;
        // first: 描画順で先頭になる直線のUUID。
        const auto first = document.addShape(QLineF(0,0,10,10));
        // second: 描画順で次に置く円のUUID。
        const auto second = document.addShape(CircleGeometry{{30,0},10});
        // selection: 描画アイテムなしで操作する選択状態の正本。
        ShapeSelectionModel selection(&document);
        // changed: 選択集合が変わったときだけ発生する通知を監視する。
        QSignalSpy changed(&selection, &ShapeSelectionModel::selectionChanged);
        QCOMPARE(selection.count(), 0); QVERIFY(!selection.singleShape());
        QVERIFY(selection.setSelectedIds({second,first,second}));
        QCOMPARE(selection.selectedIds(), QList<QUuid>({first,second}));
        QCOMPARE(selection.count(), 2); QVERIFY(!selection.singleShape()); QCOMPARE(changed.count(), 1);
        QVERIFY(selection.setSelectedIds({first,second})); QCOMPARE(changed.count(), 1);
        QVERIFY(!selection.setSelectedIds({first,QUuid::createUuid()}));
        QVERIFY(!selection.selectOnly({})); QVERIFY(!selection.toggle(QUuid::createUuid()));
        QCOMPARE(selection.count(), 2); QCOMPARE(changed.count(), 1);
        QVERIFY(selection.selectOnly(second)); QCOMPARE(changed.count(), 2);
        QCOMPARE(selection.singleShape()->id, second); QCOMPARE(selection.singleShape()->kind(), ShapeKind::Circle);
        QVERIFY(selection.selectOnly(second)); QCOMPARE(changed.count(), 2);
        QVERIFY(selection.toggle(first)); QCOMPARE(selection.count(), 2);
        QVERIFY(selection.toggle(second)); QCOMPARE(selection.singleShape()->id, first);
        selection.clear(); QCOMPARE(changed.count(), 5); QVERIFY(selection.selectedIds().isEmpty());
        selection.clear(); QCOMPARE(changed.count(), 5);
        QCOMPARE(document.shapes().size(), 2); QCOMPARE(document.shape(first)->positionMm, QPointF());
    }
    /// @brief 移動では選択を維持し、一括削除では有効なIDだけを1回で通知する。
    void documentChangesKeepSelectionConsistent() {
        // document: 追加・移動・一括削除を適用するモデル。
        DrawingDocument document;
        // first: 一括削除する選択図形のUUID。
        const auto first = document.addShape(QRectF(0,0,10,10));
        // second: 一括削除に含める、もう1つの選択図形のUUID。
        const auto second = document.addShape(QLineF(20,0,30,0));
        // kept: 削除後も選択を維持する円のUUID。
        const auto kept = document.addShape(CircleGeometry{{50,0},5});
        // selection: 文書の削除通知に追従する選択モデル。
        ShapeSelectionModel selection(&document);
        QVERIFY(selection.setSelectedIds({first,second,kept}));
        // changed: 文書の操作で余分な選択通知が発生しないことを確認する。
        QSignalSpy changed(&selection, &ShapeSelectionModel::selectionChanged);
        QVERIFY(document.translateShapes({first,second,kept},{10,20})); QCOMPARE(changed.count(), 0);
        QVERIFY(document.removeShapes({first,second})); QCOMPARE(changed.count(), 1);
        QCOMPARE(selection.selectedIds(), QList<QUuid>({kept}));
        QCOMPARE(selection.singleShape()->positionMm, QPointF(10,20));
        QVERIFY(!selection.contains(first)); QVERIFY(!selection.contains(second));
        // extra: 選択されていない追加図形。追加・削除で選択通知が出ないことを検証する。
        const auto extra = document.addShape(QRectF(-20,-20,5,5));
        QVERIFY(document.removeShapes({extra})); QCOMPARE(changed.count(), 1);
        QVERIFY(document.removeShapes({kept})); QCOMPARE(changed.count(), 2);
        QVERIFY(selection.selectedIds().isEmpty()); QVERIFY(!selection.singleShape());
        QVERIFY(!selection.selectOnly(kept)); QCOMPARE(changed.count(), 2);
    }
    /// @brief 参照文書の破棄後と、初めから文書がない場合の安全性を検証する。
    void documentLifetime() {
        // document: このテスト内で明示的に破棄する、選択モデルが所有しない文書。
        auto *document = new DrawingDocument;
        // id: 文書とともに無効になる選択図形のUUID。
        const auto id = document->addShape(QLineF(0,0,10,10));
        // selection: 文書より長く生存する選択モデル。
        ShapeSelectionModel selection(document);
        QVERIFY(selection.selectOnly(id));
        // changed: 文書破棄による選択解除が1回だけ通知されることを確認する。
        QSignalSpy changed(&selection, &ShapeSelectionModel::selectionChanged);
        delete document;
        QCOMPARE(changed.count(), 1); QCOMPARE(selection.count(), 0);
        QVERIFY(!selection.contains(id)); QVERIFY(!selection.singleShape()); QVERIFY(!selection.toggle(id));
        selection.clear(); QCOMPARE(changed.count(), 1);
        // detached: 初めから文書を持たないモデル。空の選択だけを受け付ける。
        ShapeSelectionModel detached(nullptr);
        QVERIFY(detached.setSelectedIds({})); QVERIFY(!detached.selectOnly(id));
    }
};
QTEST_GUILESS_MAIN(SelectionTests)
#include "test_selection.moc"
