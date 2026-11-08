/**
 * @file objectinfopanel.cpp
 * @brief 実寸形状を選択に応じたプロパティ表示へ変換し、移動・削除に追従する。
 */
#include "objectinfopanel.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLocale>
#include <cmath>

namespace {
// DisplayPrecision: 既存の座標入力と揃える表示桁数。末尾の0は表示時に省く。
constexpr int DisplayPrecision = 6;
/// @brief ロケールに合わせて数値を表示し、不要な小数部と負のゼロを除く。
/// @param value 表示する実寸値または角度。モデルの値自体は丸めない。
QString number(double value)
{
    // locale: 桁区切りを省き、OSに合う小数点記号を使う表示用ロケール。
    QLocale locale; locale.setNumberOptions(QLocale::OmitGroupSeparator);
    // zero: 現在のロケールの数字0。非ラテン数字の表示でも末尾を正しく省く。
    const QString zero = locale.zeroDigit();
    // text: 小数点以下6桁から不要な末尾0を削る作業用文字列。
    QString text = locale.toString(value, 'f', DisplayPrecision);
    while (text.endsWith(zero)) text.chop(zero.size());
    if (text.endsWith(locale.decimalPoint())) text.chop(locale.decimalPoint().size());
    if (text == locale.negativeSign() + zero) text = zero;
    return text;
}
/// @brief 実寸mmの値を単位付きで表示する。
/// @param value 表示する実寸の座標または長さ（mm）。
QString millimeters(double value) { return number(value) + QStringLiteral(" mm"); }
}

/// @brief 読み取り専用の形状行を作り、モデルの選択・形状・寿命の変更に接続する。
/// @param document 参照先の図形文書。パネルは所有しない。
/// @param selection 参照先の選択モデル。パネルは所有しない。
/// @param parent このパネルを所有するQtの親。
ObjectInfoPanel::ObjectInfoPanel(DrawingDocument *document, ShapeSelectionModel *selection, QWidget *parent)
    : QWidget(parent), m_document(document), m_selection(selection)
{
    setObjectName("objectInfoPanel"); setMinimumWidth(220);
    // layout: 状態説明とスクロール領域を並べる、パネル所有のレイアウト。
    auto *layout = new QVBoxLayout(this);
    m_summary = new QLabel(this); m_summary->setObjectName("objectInfoSummary"); m_summary->setWordWrap(true);
    layout->addWidget(m_summary);
    // scroll: 項目が縦に収まらない場合にスクロールする、パネル所有の領域。
    auto *scroll = new QScrollArea(this); scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // content: スクロール領域が所有する形状表示のコンテナ。
    auto *content = new QWidget(scroll);
    // contentLayout: 形状グループと単位の説明を並べる、コンテナ所有のレイアウト。
    auto *contentLayout = new QVBoxLayout(content);
    m_shapeGroup = new QGroupBox(tr("形状"), content);
    m_form = new QFormLayout(m_shapeGroup); m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    addField("startX", tr("始点 X")); addField("startY", tr("始点 Y"));
    addField("endX", tr("終点 X")); addField("endY", tr("終点 Y"));
    addField("length", tr("長さ")); addField("angle", tr("角度"));
    addField("originX", tr("左下 X")); addField("originY", tr("左下 Y"));
    addField("width", tr("幅")); addField("height", tr("高さ"));
    addField("centerX", tr("中心 X")); addField("centerY", tr("中心 Y"));
    addField("radius", tr("半径")); addField("diameter", tr("直径")); addField("vertices", tr("頂点数"));
    m_values.value("angle")->setToolTip(tr("X軸の正方向を0°とした反時計回りの角度です。"));
    contentLayout->addWidget(m_shapeGroup);
    // units: 表示単位と座標原点を説明する、コンテナ所有のラベル。
    auto *units = new QLabel(tr("座標・長さは実寸mmです。\n原点は用紙中心、右が+X、上が+Yです。"), content);
    units->setWordWrap(true); contentLayout->addWidget(units); contentLayout->addStretch();
    scroll->setWidget(content); layout->addWidget(scroll);
    if (selection) {
        connect(selection, &ShapeSelectionModel::selectionChanged, this, &ObjectInfoPanel::refresh);
        // コールバック: 選択モデルの破棄後に古い図形情報を残さない。
        connect(selection, &QObject::destroyed, this, [this] { m_selection = nullptr; refresh(); });
    }
    if (document) {
        connect(document, &DrawingDocument::shapeChanged, this, &ObjectInfoPanel::refresh);
        connect(document, &DrawingDocument::shapeRemoved, this, &ObjectInfoPanel::refresh);
        // コールバック: 文書の破棄後は形状データを読まず、未選択表示へ戻す。
        connect(document, &QObject::destroyed, this, [this] { m_document = nullptr; refresh(); });
    }
    refresh();
}

/// @brief QWidgetの子を破棄する前に、参照モデルからの通知を切り離す。
ObjectInfoPanel::~ObjectInfoPanel()
{
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    if (m_selection) disconnect(m_selection, nullptr, this, nullptr);
}

/// @brief 値の文字列をコピーできるラベルとして形状行を登録する。
/// @param key UI項目を識別する内部名。
/// @param title 項目の表示名。
void ObjectInfoPanel::addField(const QString &key, const QString &title)
{
    // value: グループが所有する、読み取り専用で文字列を選択できる値ラベル。
    auto *value = new QLabel(m_shapeGroup); value->setObjectName("property_" + key);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setWordWrap(true); m_form->addRow(title, value); m_values.insert(key, value);
}

/// @brief 指定項目の表示文字列を設定し、その行を見える状態にする。
/// @param key 登録済みの項目名。
/// @param value 単位を含む表示文字列。
void ObjectInfoPanel::showField(const QString &key, const QString &value)
{
    // label: グループが所有する、表示対象の値ラベル。
    auto *label = m_values.value(key); label->setText(value); m_form->setRowVisible(label, true);
}

/// @brief 現在の選択図形を読み取り、該当する実寸項目だけを表示する。
void ObjectInfoPanel::refresh()
{
    // value: 以前の図形から値が残らないよう消去する、グループ所有のラベル。
    for (auto *value : m_values) { value->clear(); m_form->setRowVisible(value, false); }
    // single: 単独選択時に取得する値データ。表示のためにモデルを変更しない。
    const auto single = m_document && m_selection ? m_selection->singleShape() : std::nullopt;
    m_shapeGroup->setVisible(single.has_value());
    if (!single) {
        // count: 有効な選択図形数。文書がない場合は未選択として扱う。
        const int count = m_document && m_selection ? m_selection->count() : 0;
        m_summary->setText(count > 1 ? tr("%1個の図形を選択中\n1個選択すると形状情報を表示します。").arg(count)
                                    : tr("図形を選択すると形状情報を表示します。"));
        return;
    }
    // offset: 図形の形状全体に加える配置移動量（実寸mm）。
    const QPointF offset = single->positionMm;
    switch (single->kind()) {
    case ShapeKind::Line: {
        m_summary->setText(tr("直線"));
        // line: 2端点のローカル実寸形状。配置移動量は別に加える。
        const auto line = std::get<QLineF>(single->geometryMm);
        showField("startX", millimeters(line.p1().x() + offset.x()));
        showField("startY", millimeters(line.p1().y() + offset.y()));
        showField("endX", millimeters(line.p2().x() + offset.x()));
        showField("endY", millimeters(line.p2().y() + offset.y()));
        showField("length", millimeters(line.length()));
        // Pi: ラジアンを度へ換算する円周率。
        constexpr double Pi = 3.14159265358979323846;
        // angle: +X基準・反時計回りの角度を0〜360度へ正規化する表示用の値。
        double angle = std::atan2(line.dy(), line.dx()) * 180 / Pi;
        if (angle < 0) angle += 360;
        // shownAngle: 表示桁への丸めで360度となる場合だけ、同方向の0度に戻す文字列。
        const QString shownAngle = number(angle) == number(360) ? number(0) : number(angle);
        showField("angle", shownAngle + QStringLiteral("°"));
        break;
    }
    case ShapeKind::Circle: {
        m_summary->setText(tr("円"));
        // circle: 真円の中心と半径の形状データ（実寸mm）。
        const auto circle = std::get<CircleGeometry>(single->geometryMm);
        showField("centerX", millimeters(circle.centerMm.x() + offset.x()));
        showField("centerY", millimeters(circle.centerMm.y() + offset.y()));
        showField("radius", millimeters(circle.radiusMm)); showField("diameter", millimeters(2 * circle.radiusMm));
        break;
    }
    case ShapeKind::Rectangle:
    case ShapeKind::Polygon: {
        // bounds: 配置移動量を反映した外接範囲（実寸mm）。minYがCAD上の下辺に対応する。
        const QRectF bounds = single->boundsMm();
        m_summary->setText(single->kind() == ShapeKind::Rectangle ? tr("四角") : tr("多角形"));
        showField("originX", millimeters(bounds.left())); showField("originY", millimeters(bounds.top()));
        showField("width", millimeters(bounds.width())); showField("height", millimeters(bounds.height()));
        if (single->kind() == ShapeKind::Polygon)
            showField("vertices", QString::number(std::get<QPolygonF>(single->geometryMm).size()));
        break;
    }
    }
}
