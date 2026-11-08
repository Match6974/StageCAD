/**
 * @file test_objectinfo.cpp
 * @brief 選択図形の情報表示・更新・ロケール・参照先の寿命を検証する。
 */
#include "objectinfopanel.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>

namespace {
/// @brief 表示項目のラベルを取得する。項目がなければテスト失敗として記録する。
/// @param panel 読み取り対象のパネル。所有権は取得しない。
/// @param key 項目名。実際のobjectNameにはproperty_が付く。
QLabel *field(ObjectInfoPanel &panel, const char *key)
{
    // label: パネルが所有する対象の値ラベル。
    auto *label = panel.findChild<QLabel*>(QStringLiteral("property_") + QString::fromLatin1(key));
    if (!label) QTest::qFail("Property field was not found", __FILE__, __LINE__);
    return label;
}
}

/// @brief モデルを変更しない情報表示と、選択・形状変更への追従を検証する。
class ObjectInfoTests : public QObject {
    Q_OBJECT
private slots:
    /// @brief 表示文字列の比較に使う小数点形式を統一する。
    void init() { QLocale::setDefault(QLocale::c()); }
    /// @brief 4種類の実寸値、配置移動量、不要な項目の非表示を確認する。
    void shapeValuesAndSelection() {
        // document: 実寸形状を保持する検証用文書。
        DrawingDocument document;
        // selection: 表示する図形をUUIDで指定する選択モデル。
        ShapeSelectionModel selection(&document);
        // panel: 読み取り専用の実寸情報パネル。
        ObjectInfoPanel panel(&document, &selection); panel.show();
        // summary: 未選択・単独・複数の説明を表示するラベル。
        auto *summary = panel.findChild<QLabel*>("objectInfoSummary"); QVERIFY(summary);
        QVERIFY(summary->text().contains(QString::fromUtf8("選択")));
        QVERIFY(!field(panel,"length")->isVisible());
        // line: 長さ50mmの直線に割り当てたUUID。
        const auto line = document.addShape(QLineF(-10,20,20,60));
        // rect: 幅30mm、高さ40mmの矩形のUUID。
        const auto rect = document.addShape(QRectF(10,20,30,40));
        // circle: 半径12.5mmの円のUUID。
        const auto circle = document.addShape(CircleGeometry{{5,10},12.5});
        // polygon: 向きと頂点数を保持した三角形のUUID。
        const auto polygon = document.addShape(QPolygonF({{-10,-20},{20,-20},{0,30}}));
        QVERIFY(document.translateShapes({line},{100,-200}));
        QVERIFY(document.translateShapes({rect},{-100,10}));
        QVERIFY(document.translateShapes({circle},{-20,30}));
        QVERIFY(document.translateShapes({polygon},{100,200}));
        // changed: 選択や情報表示が文書へ書き込みを行わないことを確認する。
        QSignalSpy changed(&document, &DrawingDocument::shapeChanged);
        QVERIFY(selection.selectOnly(line));
        QCOMPARE(field(panel,"startX")->text(), QString("90 mm"));
        QCOMPARE(field(panel,"startY")->text(), QString("-180 mm"));
        QCOMPARE(field(panel,"endX")->text(), QString("120 mm"));
        QCOMPARE(field(panel,"endY")->text(), QString("-140 mm"));
        QCOMPARE(field(panel,"length")->text(), QString("50 mm"));
        QCOMPARE(field(panel,"angle")->text(), QString::fromUtf8("53.130102°"));
        QVERIFY(selection.selectOnly(rect));
        QCOMPARE(field(panel,"originX")->text(), QString("-90 mm"));
        QCOMPARE(field(panel,"originY")->text(), QString("30 mm"));
        QCOMPARE(field(panel,"width")->text(), QString("30 mm"));
        QCOMPARE(field(panel,"height")->text(), QString("40 mm"));
        QVERIFY(field(panel,"length")->text().isEmpty()); QVERIFY(!field(panel,"length")->isVisible());
        QVERIFY(selection.selectOnly(circle));
        QCOMPARE(field(panel,"centerX")->text(), QString("-15 mm"));
        QCOMPARE(field(panel,"centerY")->text(), QString("40 mm"));
        QCOMPARE(field(panel,"radius")->text(), QString("12.5 mm"));
        QCOMPARE(field(panel,"diameter")->text(), QString("25 mm"));
        QVERIFY(selection.selectOnly(polygon));
        QCOMPARE(field(panel,"originX")->text(), QString("90 mm"));
        QCOMPARE(field(panel,"originY")->text(), QString("180 mm"));
        QCOMPARE(field(panel,"width")->text(), QString("30 mm"));
        QCOMPARE(field(panel,"height")->text(), QString("50 mm"));
        QCOMPARE(field(panel,"vertices")->text(), QString("3"));
        QVERIFY(selection.toggle(circle)); QVERIFY(summary->text().contains("2"));
        QVERIFY(field(panel,"width")->text().isEmpty()); QVERIFY(!field(panel,"width")->isVisible());
        selection.clear(); QVERIFY(field(panel,"vertices")->text().isEmpty());
        QCOMPARE(changed.count(), 0); QCOMPARE(document.shapes().size(), 4);
        QVERIFY(panel.findChildren<QLineEdit*>().isEmpty());
    }
    /// @brief 移動・削除で表示が更新され、同じ選択でも現在の配置を参照することを確認する。
    void followsMoveAndRemoval() {
        // document: 表示後に移動・削除を適用する文書。
        DrawingDocument document;
        // selection: 同じUUIDの選択を維持するモデル。
        ShapeSelectionModel selection(&document);
        // panel: 文書の変更通知へ追従するパネル。
        ObjectInfoPanel panel(&document,&selection);
        // id: 位置と長さの更新を検証する直線のUUID。
        const auto id = document.addShape(QLineF(0,0,10,0));
        QVERIFY(selection.selectOnly(id)); QCOMPARE(field(panel,"startX")->text(), QString("0 mm"));
        QVERIFY(document.translateShapes({id},{2.5,-3.75}));
        QCOMPARE(field(panel,"startX")->text(), QString("2.5 mm"));
        QCOMPARE(field(panel,"startY")->text(), QString("-3.75 mm"));
        QCOMPARE(field(panel,"length")->text(), QString("10 mm"));
        QCOMPARE(field(panel,"angle")->text(), QString::fromUtf8("0°"));
        QVERIFY(document.removeShapes({id})); QVERIFY(field(panel,"startX")->text().isEmpty());
    }
    /// @brief ロケールの小数点と数字0が、単位付き表示にも適用されることを確認する。
    void localizedValues() {
        // document: 小数値を持つ円を保持する文書。
        DrawingDocument document;
        // selection: 単独選択により表示更新を発生させるモデル。
        ShapeSelectionModel selection(&document);
        // panel: ロケール変更後の値を確認するパネル。
        ObjectInfoPanel panel(&document,&selection);
        // id: 表示精度で0に丸められる中心と半径1.25mmを持つ円のUUID。
        const auto id = document.addShape(CircleGeometry{{-0.00000001,0},1.25});
        QLocale::setDefault(QLocale("de_DE")); QVERIFY(selection.selectOnly(id));
        QCOMPARE(field(panel,"radius")->text(), QString("1,25 mm"));
        QCOMPARE(field(panel,"centerX")->text(), QString("0 mm"));
        selection.clear(); QLocale::setDefault(QLocale("ar_EG")); QVERIFY(selection.selectOnly(id));
        QCOMPARE(field(panel,"centerX")->text(), QLocale().zeroDigit()+QStringLiteral(" mm"));
        QLocale::setDefault(QLocale::c());
    }
    /// @brief 文書や選択モデルが先に破棄されても、古い値や不正参照を残さないことを確認する。
    void referenceLifetime() {
        // document: パネルより先に明示的に破棄する、パネルが所有しない文書。
        auto *document = new DrawingDocument;
        // selection: 文書破棄後も存在する選択モデル。
        ShapeSelectionModel selection(document);
        // panel: 参照先の寿命を弱い参照で監視するパネル。
        ObjectInfoPanel panel(document,&selection);
        QVERIFY(selection.selectOnly(document->addShape(QLineF(0,0,10,0))));
        delete document; QVERIFY(field(panel,"length")->text().isEmpty());
        // remaining: 選択モデルだけが先に破棄されるケースの文書。
        DrawingDocument remaining;
        // temporarySelection: パネルが所有せず、テスト内で先に解放する選択モデル。
        auto *temporarySelection = new ShapeSelectionModel(&remaining);
        // another: 選択モデル破棄後も生存するパネル。
        ObjectInfoPanel another(&remaining,temporarySelection);
        // id: 選択モデル破棄後に移動しても表示が復活しないことを確かめるUUID。
        const auto id = remaining.addShape(CircleGeometry{{0,0},10});
        QVERIFY(temporarySelection->selectOnly(id)); delete temporarySelection;
        QVERIFY(field(another,"radius")->text().isEmpty());
        QVERIFY(remaining.translateShapes({id},{10,10})); QVERIFY(field(another,"radius")->text().isEmpty());
    }
};
QTEST_MAIN(ObjectInfoTests)
#include "test_objectinfo.moc"
