/**
 * @file test_objectinfo.cpp
 * @brief 選択図形の情報表示・更新・ロケール・参照先の寿命を検証する。
 */
#include "objectinfopanel.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "rectangleanchorselector.h"
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QSignalSpy>
#include <QTest>
#include <cmath>

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
/// @brief 図形プロパティの数値入力欄を取得する。
/// @param panel 入力欄を所有する対象パネル。
/// @param key startX・length等の項目名。
QLineEdit *editor(ObjectInfoPanel &panel, const char *key)
{
    // edit: 形状グループまたは行コンテナが所有する対象の入力欄。
    auto *edit = panel.findChild<QLineEdit*>(QStringLiteral("edit_") + QString::fromLatin1(key));
    if (!edit) QTest::qFail("Shape editor was not found", __FILE__, __LINE__);
    return edit;
}
/// @brief ユーザーの数値入力とEnterによる項目確定を再現する。
/// @param panel 入力先のプロパティパネル。
/// @param key 確定する形状項目。
/// @param text ロケールに合う数値文字列。
void enter(ObjectInfoPanel &panel, const char *key, const QString &text)
{
    // edit: フォーカスを当て、現在値を選択して置き換える入力欄。
    auto *edit = editor(panel,key); edit->setFocus(); edit->selectAll();
    QTest::keyClicks(edit,text); QTest::keyClick(edit,Qt::Key_Return);
}

}

/// @brief 形状情報の表示、数値編集と、選択・形状変更への追従を検証する。
class ObjectInfoTests : public QObject {
    Q_OBJECT
private slots:
    /// @brief 表示文字列の比較に使う小数点形式を統一する。
    void init() { QLocale::setDefault(QLocale::c()); }
    /// @brief 9点マトリクスの見た目とCAD上の固定点の対応をテスト行として用意する。
    void rectangleAnchors_data() {
        QTest::addColumn<int>("anchorIndex"); QTest::addColumn<QPointF>("fraction");
        QTest::newRow("top-left") << 0 << QPointF(0,1);
        QTest::newRow("top-center") << 1 << QPointF(0.5,1);
        QTest::newRow("top-right") << 2 << QPointF(1,1);
        QTest::newRow("middle-left") << 3 << QPointF(0,0.5);
        QTest::newRow("center") << 4 << QPointF(0.5,0.5);
        QTest::newRow("middle-right") << 5 << QPointF(1,0.5);
        QTest::newRow("bottom-left") << 6 << QPointF(0,0);
        QTest::newRow("bottom-center") << 7 << QPointF(0.5,0);
        QTest::newRow("bottom-right") << 8 << QPointF(1,0);
    }
    /// @brief 9点の各点をクリックし、幅・高さ変更後も基準点の実寸座標を保つことを検証する。
    void rectangleAnchors() {
        // anchorIndex: 画面の左上から行優先で並べた固定点番号。
        QFETCH(int,anchorIndex);
        // fraction: 左下(0,0)〜右上(1,1)として期待する固定点の相対位置。
        QFETCH(QPointF,fraction);
        // document: 移動済み四角形の形状を更新する文書。
        DrawingDocument document;
        // selection: 単独選択のUUIDを保つ選択モデル。
        ShapeSelectionModel selection(&document);
        // panel: マトリクスと数値欄を操作するプロパティパネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // id: 拡大・縮小の対象になる四角形UUID。
        const auto id = document.addShape(QRectF(10,20,30,40));
        QVERIFY(document.translateShapes({id},{100,200})); QVERIFY(selection.selectOnly(id));
        // anchor: 図形を変更せず固定点だけを選ぶUI。
        auto *anchor = panel.findChild<RectangleAnchorSelector*>(); QVERIFY(anchor);
        // changed: 基準点をクリックするだけでは文書が変わらないことを確認する監視。
        QSignalSpy changed(&document,&DrawingDocument::shapeChanged);
        // button: マトリクスの位置と実際の固定点が一致することを確かめる対象ボタン。
        auto *button = anchor->findChild<QToolButton*>(QStringLiteral("rectangleAnchor_%1").arg(anchorIndex)); QVERIFY(button);
        QTest::mouseClick(button,Qt::LeftButton); QCOMPARE(anchor->anchorIndex(),anchorIndex);
        QCOMPARE(anchor->normalizedAnchor(),fraction); QCOMPARE(changed.count(),0);
        // fixed: 変更前に選択点があった、用紙中心基準の実寸座標（mm）。
        const QPointF fixed(110+30*fraction.x(),220+40*fraction.y());
        enter(panel,"width","60"); enter(panel,"height","80");
        // resized: 寸法更新を反映した実寸の矩形。
        const QRectF resized = document.shape(id)->boundsMm();
        QCOMPARE(resized.size(),QSizeF(60,80));
        QCOMPARE(QPointF(resized.x()+60*fraction.x(),resized.y()+80*fraction.y()),fixed);
        QCOMPARE(field(panel,"originX")->text(),QString::number(fixed.x())+" mm");
        QCOMPARE(field(panel,"originY")->text(),QString::number(fixed.y())+" mm");
        QCOMPARE(editor(panel,"width")->text(),QString("60")); QCOMPARE(editor(panel,"height")->text(),QString("80"));
        enter(panel,"width","15"); enter(panel,"height","20");
        // shrunk: 縮小時にも同じ固定点を保つ実寸矩形。
        const QRectF shrunk = document.shape(id)->boundsMm();
        QCOMPARE(QPointF(shrunk.x()+15*fraction.x(),shrunk.y()+20*fraction.y()),fixed);
        QCOMPARE(selection.selectedIds(),QList<QUuid>({id})); QCOMPARE(changed.count(),4);
    }
    /// @brief 固定点の初期値・排他性・保持と、円中心・真円の維持を検証する。
    void anchorPersistenceAndCircleRadius() {
        // document: 形状と通知を検証する文書。
        DrawingDocument document;
        // selection: 四角形と円を切り替えて選択するモデル。
        ShapeSelectionModel selection(&document);
        // panel: 固定点と半径を変更する対象パネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // anchor: 最後の選択を図形切り替え後にも保持するコントロール。
        auto *anchor = panel.findChild<RectangleAnchorSelector*>(); QVERIFY(anchor);
        QCOMPARE(anchor->anchorIndex(),4);
        // notifications: 同じ基準点や不正番号で通知しないことを確認する監視。
        QSignalSpy notifications(anchor,&RectangleAnchorSelector::anchorChanged);
        QVERIFY(anchor->setAnchorIndex(4)); QVERIFY(!anchor->setAnchorIndex(-1)); QVERIFY(!anchor->setAnchorIndex(9));
        QCOMPARE(notifications.count(),0); QVERIFY(anchor->setAnchorIndex(2)); QCOMPARE(notifications.count(),1);
        // checked: 9点のうち押下表示が常に1点だけであることを検証するカウンタ。
        int checked = 0;
        // button: セレクタが所有する各固定点ボタン。
        for (auto *button : anchor->findChildren<QToolButton*>()) checked += button->isChecked();
        QCOMPARE(checked,1);
        // rect: 基準点変更や未確定入力では形状が変わらない四角形UUID。
        const auto rect = document.addShape(QRectF(0,0,30,40)); QVERIFY(selection.selectOnly(rect));
        editor(panel,"width")->setText("99"); editor(panel,"width")->setModified(true);
        QVERIFY(anchor->setAnchorIndex(6)); QCOMPARE(editor(panel,"width")->text(),QString("30"));
        QCOMPARE(document.shape(rect)->boundsMm(),QRectF(0,0,30,40));
        // circle: 中心を保ったまま半径だけを変更する円のUUID。
        const auto circle = document.addShape(CircleGeometry{{10,20},5});
        QVERIFY(document.translateShapes({circle},{100,200})); QVERIFY(selection.selectOnly(circle));
        QVERIFY(!anchor->isVisible()); enter(panel,"radius","12.5");
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).centerMm,QPointF(110,220));
        QCOMPARE(document.shape(circle)->boundsMm(),QRectF(97.5,207.5,25,25));
        QCOMPARE(field(panel,"diameter")->text(),QString("25 mm"));
        QVERIFY(selection.selectOnly(rect)); QCOMPARE(anchor->anchorIndex(),6);
        panel.hide(); panel.show(); QCOMPARE(anchor->anchorIndex(),6);
    }
    /// @brief 不正寸法・未確定値の拒否と、未編集の精密値保持を検証する。
    void invalidSizesAndPrecision() {
        // document: 不正入力時に維持される確定形状。
        DrawingDocument document;
        // selection: 編集対象を切り替えるUUID選択モデル。
        ShapeSelectionModel selection(&document);
        // panel: Enter確定とEsc取消を受け付けるパネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // rect: 表示桁より精密な幅を持つ四角形。
        const auto rect = document.addShape(QRectF(0,0,30.123456789,40)); QVERIFY(selection.selectOnly(rect));
        QTest::keyClick(editor(panel,"width"),Qt::Key_Return);
        QCOMPARE(std::get<QRectF>(document.shape(rect)->geometryMm).width(),30.123456789);
        enter(panel,"height","80");
        QCOMPARE(std::get<QRectF>(document.shape(rect)->geometryMm).width(),30.123456789);
        // valid: 不正入力の前に確定した実寸範囲。
        const QRectF valid = document.shape(rect)->boundsMm();
        // changed: 拒否した入力で通知が発生しないことを確かめる監視。
        QSignalSpy changed(&document,&DrawingDocument::shapeChanged);
        enter(panel,"width","0"); QCOMPARE(changed.count(),0);
        QTest::keyClick(editor(panel,"width"),Qt::Key_Escape);
        // bad: 表記・符号・範囲の検証に使う寸法入力（mm）。
        for (const QString bad : {QString("-1"),QString("NaN"),QString("100000000")}) {
            editor(panel,"height")->setText(bad); editor(panel,"height")->setModified(true);
            QTest::keyClick(editor(panel,"height"),Qt::Key_Return); QCOMPARE(changed.count(),0);
        }
        QCOMPARE(document.shape(rect)->boundsMm(),valid);
        // circle: 不正半径が元の真円を変更しないことを確認する対象。
        const auto circle = document.addShape(CircleGeometry{{10,20},5.123456789}); QVERIFY(selection.selectOnly(circle));
        QTest::keyClick(editor(panel,"radius"),Qt::Key_Return);
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).radiusMm,5.123456789);
        enter(panel,"radius","0"); QCOMPARE(changed.count(),0);
        editor(panel,"radius")->setText("100000000"); editor(panel,"radius")->setModified(true);
        QTest::keyClick(editor(panel,"radius"),Qt::Key_Return); QCOMPARE(changed.count(),0);
        QCOMPARE(std::get<CircleGeometry>(document.shape(circle)->geometryMm).radiusMm,5.123456789);
        QVERIFY(!panel.findChild<QLabel*>("lineEditError")->text().isEmpty());
        QTest::keyClick(editor(panel,"radius"),Qt::Key_Escape);
        editor(panel,"radius")->setText("99"); editor(panel,"radius")->setModified(true);
        panel.setEnabled(false); panel.setEnabled(true); QCOMPARE(changed.count(),0);
        QCOMPARE(editor(panel,"radius")->text(),QString("5.123457"));
    }
    /// @brief 6項目の入力が実寸に反映され、長さ・角度では始点を固定することを検証する。
    void lineEditingKeepsFirstPoint() {
        // document: 移動済みの直線も編集できることを確認する文書。
        DrawingDocument document;
        // selection: 編集対象のUUIDを保持するモデル。
        ShapeSelectionModel selection(&document);
        // panel: 実際の入力操作を送るプロパティパネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // id: 編集後も同じUUIDを維持する直線。
        const auto id = document.addShape(QLineF(0,0,10,0));
        QVERIFY(document.translateShapes({id},{100,200})); QVERIFY(selection.selectOnly(id));
        enter(panel,"startX","101.25"); enter(panel,"startY","201");
        enter(panel,"endX","120.5"); enter(panel,"endY","210.25");
        // line: 編集結果をモデルから読む実寸線分。更新後は配置0に統合される。
        QLineF line = std::get<QLineF>(document.shape(id)->geometryMm);
        QCOMPARE(line,QLineF(101.25,201,120.5,210.25));
        enter(panel,"length","50"); line = std::get<QLineF>(document.shape(id)->geometryMm);
        QCOMPARE(line.p1(),QPointF(101.25,201)); QVERIFY(std::abs(line.length()-50)<1e-9);
        enter(panel,"angle","90"); line = std::get<QLineF>(document.shape(id)->geometryMm);
        QCOMPARE(line.p1(),QPointF(101.25,201)); QCOMPARE(line.p2(),QPointF(101.25,251));
        enter(panel,"angle","-90"); line = std::get<QLineF>(document.shape(id)->geometryMm);
        QCOMPARE(line.p2(),QPointF(101.25,151)); QCOMPARE(editor(panel,"angle")->text(),QString("270"));
        enter(panel,"angle","450"); QCOMPARE(editor(panel,"angle")->text(),QString("90"));
        QCOMPARE(selection.selectedIds(),QList<QUuid>({id})); QCOMPARE(document.shapes().size(),1);
    }
    /// @brief 不正値・ゼロ長さの拒否、Esc取消、選択変更・無効化時の下書き破棄を検証する。
    void invalidEditsAndDraftLifetime() {
        // document: エラー時に変更されないことを確認する文書。
        DrawingDocument document;
        // selection: 編集中に切り替える選択状態。
        ShapeSelectionModel selection(&document);
        // panel: 下書きと確定を区別する対象パネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // first: 不正値で元の形状を維持する最初の線のUUID。
        const auto first = document.addShape(QLineF(0,0,10,0));
        // second: 古い入力が適用されないことを確認する、別の直線UUID。
        const auto second = document.addShape(QLineF(20,20,30,20));
        QVERIFY(selection.selectOnly(first));
        // changed: 拒否・取消がモデル更新通知を出さないことを確認する監視。
        QSignalSpy changed(&document,&DrawingDocument::shapeChanged);
        enter(panel,"length","0"); QCOMPARE(changed.count(),0);
        QVERIFY(!panel.findChild<QLabel*>("lineEditError")->text().isEmpty());
        QTest::keyClick(editor(panel,"length"),Qt::Key_Escape);
        QCOMPARE(editor(panel,"length")->text(),QString("10"));
        enter(panel,"endX","0"); QCOMPARE(changed.count(),0);
        QTest::keyClick(editor(panel,"endX"),Qt::Key_Escape);
        // bad: 貼り付け等で範囲外・非有限値が渡されてもモデルへ適用しない試験入力。
        for (const QString bad : {QString("10000001"),QString("NaN"),QString("inf")}) {
            editor(panel,"endX")->setText(bad); editor(panel,"endX")->setModified(true);
            QTest::keyClick(editor(panel,"endX"),Qt::Key_Return); QCOMPARE(changed.count(),0);
        }
        QTest::keyClick(editor(panel,"endX"),Qt::Key_Escape);
        editor(panel,"startX")->setText("99"); editor(panel,"startX")->setModified(true);
        QVERIFY(selection.selectOnly(second)); QTest::keyClick(editor(panel,"startX"),Qt::Key_Return);
        QCOMPARE(changed.count(),0); QCOMPARE(editor(panel,"startX")->text(),QString("20"));
        editor(panel,"startX")->setText("99"); editor(panel,"startX")->setModified(true);
        panel.setEnabled(false); panel.setEnabled(true);
        QCOMPARE(editor(panel,"startX")->text(),QString("20")); QCOMPARE(changed.count(),0);
        QVERIFY(document.removeShapes({second})); QTest::keyClick(editor(panel,"startX"),Qt::Key_Return);
        QCOMPARE(document.shapes().size(),1); QCOMPARE(std::get<QLineF>(document.shape(first)->geometryMm),QLineF(0,0,10,0));
    }
    /// @brief 表示桁より精密な値を、未編集のEnterや他成分の編集で失わないことを確認する。
    void unmodifiedValuesKeepPrecision() {
        // document: 小数点以下6桁を超える精度の直線を保持する文書。
        DrawingDocument document;
        // selection: 精密な直線のUUIDを選択するモデル。
        ShapeSelectionModel selection(&document);
        // panel: 表示は丸めるが未編集値をモデルへ書き戻さないパネル。
        ObjectInfoPanel panel(&document,&selection); panel.show();
        // id: 高精度の始点を維持する対象UUID。
        const auto id = document.addShape(QLineF(0.123456789,0,10,0)); QVERIFY(selection.selectOnly(id));
        QTest::keyClick(editor(panel,"startX"),Qt::Key_Return);
        QCOMPARE(std::get<QLineF>(document.shape(id)->geometryMm).x1(),0.123456789);
        enter(panel,"endY","1"); QCOMPARE(std::get<QLineF>(document.shape(id)->geometryMm).x1(),0.123456789);
        QLocale::setDefault(QLocale("de_DE")); selection.clear(); QVERIFY(selection.selectOnly(id));
        enter(panel,"endY","2,5"); QCOMPARE(std::get<QLineF>(document.shape(id)->geometryMm).y2(),2.5);
        QLocale::setDefault(QLocale::c());
    }
    /// @brief 4種類の実寸値、配置移動量、不要な項目の非表示を確認する。
    void shapeValuesAndSelection() {
        // document: 実寸形状を保持する検証用文書。
        DrawingDocument document;
        // selection: 表示する図形をUUIDで指定する選択モデル。
        ShapeSelectionModel selection(&document);
        // panel: 選択と情報表示だけではモデルへ書き込まないことを検証するパネル。
        ObjectInfoPanel panel(&document, &selection); panel.show();
        // summary: 未選択・単独・複数の説明を表示するラベル。
        auto *summary = panel.findChild<QLabel*>("objectInfoSummary"); QVERIFY(summary);
        QVERIFY(summary->text().contains(QString::fromUtf8("選択")));
        QVERIFY(!editor(panel,"length")->isVisible());
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
        QCOMPARE(editor(panel,"startX")->text(), QString("90"));
        QCOMPARE(editor(panel,"startY")->text(), QString("-180"));
        QCOMPARE(editor(panel,"endX")->text(), QString("120"));
        QCOMPARE(editor(panel,"endY")->text(), QString("-140"));
        QCOMPARE(editor(panel,"length")->text(), QString("50"));
        QCOMPARE(editor(panel,"angle")->text(), QString("53.130102"));
        QVERIFY(panel.findChild<RectangleAnchorSelector*>()->setAnchorIndex(6));
        QVERIFY(selection.selectOnly(rect));
        QCOMPARE(field(panel,"originX")->text(), QString("-90 mm"));
        QCOMPARE(field(panel,"originY")->text(), QString("30 mm"));
        QCOMPARE(field(panel,"width")->text(), QString("30 mm"));
        QCOMPARE(field(panel,"height")->text(), QString("40 mm"));
        QVERIFY(editor(panel,"length")->text().isEmpty()); QVERIFY(!editor(panel,"length")->isVisible());
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
        // edit: 未選択時は全ての直線編集欄が隠れ、他の形状を編集できないことを確認する。
        for (auto *edit : panel.findChildren<QLineEdit*>()) QVERIFY(!edit->isVisible());
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
        QVERIFY(selection.selectOnly(id)); QCOMPARE(editor(panel,"startX")->text(), QString("0"));
        QVERIFY(document.translateShapes({id},{2.5,-3.75}));
        QCOMPARE(editor(panel,"startX")->text(), QString("2.5"));
        QCOMPARE(editor(panel,"startY")->text(), QString("-3.75"));
        QCOMPARE(editor(panel,"length")->text(), QString("10"));
        QCOMPARE(editor(panel,"angle")->text(), QString("0"));
        QVERIFY(document.removeShapes({id})); QVERIFY(editor(panel,"startX")->text().isEmpty());
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
        delete document; QVERIFY(editor(panel,"length")->text().isEmpty());
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
