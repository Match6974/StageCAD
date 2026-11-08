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
#include <QLineEdit>
#include <QDoubleValidator>
#include <QKeyEvent>
#include <limits>
#include <cmath>

namespace {
// DisplayPrecision: 既存の座標入力と揃える表示桁数。末尾の0は表示時に省く。
constexpr int DisplayPrecision = 6;
// Pi: 直線の角度表示・編集でラジアンと度を換算する円周率。
constexpr double Pi = 3.14159265358979323846;
/// @brief +X基準・反時計回りの直線角度を0〜360度で返す。
/// @param line 配置に依存しない方向を持つ実寸線分。
double lineAngle(const QLineF &line)
{
    // angle: atan2で求めた、正規化前の角度（度）。
    const double angle = std::atan2(line.dy(), line.dx()) * 180 / Pi;
    return angle < 0 ? angle + 360 : angle;
}
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

/// @brief 形状表示と直線の編集欄を作り、モデルの選択・形状・寿命の変更に接続する。
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
    addField("originX", tr("左下 X")); addField("originY", tr("左下 Y"));
    addField("width", tr("幅")); addField("height", tr("高さ"));
    addField("centerX", tr("中心 X")); addField("centerY", tr("中心 Y"));
    addField("radius", tr("半径")); addField("diameter", tr("直径")); addField("vertices", tr("頂点数"));
    contentLayout->addWidget(m_shapeGroup);
    m_lineGroup = new QGroupBox(tr("形状"), content);
    m_lineForm = new QFormLayout(m_lineGroup); m_lineForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    addLineEditor("startX", tr("始点 X (mm)")); addLineEditor("startY", tr("始点 Y (mm)"));
    addLineEditor("endX", tr("終点 X (mm)")); addLineEditor("endY", tr("終点 Y (mm)"));
    addLineEditor("length", tr("長さ (mm)")); addLineEditor("angle", tr("角度 (°)"));
    m_lineEdits.value("angle")->setToolTip(tr("+X方向を0°とした反時計回りの角度。始点を固定して変更します。"));
    contentLayout->addWidget(m_lineGroup);
    // hint: 各入力の確定・取消と、固定点を説明する直線グループ所有のラベル。
    auto *hint = new QLabel(tr("各項目はEnterで確定、Escで取消。\n長さ・角度の変更では始点を固定します。"), m_lineGroup);
    hint->setWordWrap(true); m_lineForm->addRow(hint);
    m_error = new QLabel(content); m_error->setObjectName("lineEditError"); m_error->setWordWrap(true);
    contentLayout->addWidget(m_error);
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
    m_error->clear(); m_lineId = {};
    // edit: 以前の線の下書きを破棄する、直線グループ所有の入力欄。
    for (auto *edit : m_lineEdits) { edit->clear(); edit->setModified(false); }
    // single: 単独選択時に取得する値データ。表示のためにモデルを変更しない。
    const auto single = m_document && m_selection ? m_selection->singleShape() : std::nullopt;
    // isLine: 単独選択した直線には数値編集を、それ以外の形状には情報表示を使う。
    const bool isLine = single && single->kind() == ShapeKind::Line;
    m_shapeGroup->setVisible(single.has_value() && !isLine); m_lineGroup->setVisible(isLine);
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
        m_lineId = single->id;
        // world: 配置移動量を反映した、用紙中心基準の実寸端点（mm）。
        const QLineF world = line.translated(offset);
        // values: 直線6項目の表示値。角度だけ度、それ以外は実寸mm。
        const QHash<QString,double> values{{"startX",world.x1()},{"startY",world.y1()},
            {"endX",world.x2()},{"endY",world.y2()},{"length",world.length()},{"angle",lineAngle(world)}};
        // it: 数値を対応する入力欄へ反映する項目走査。
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            // edit: 対象項目の入力欄。プログラム反映で変更済み状態にはしない。
            auto *edit = m_lineEdits.value(it.key());
            // locale: 表示値と一致させる、桁区切りを拒否する入力ロケール。
            QLocale locale; locale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
            edit->findChild<QDoubleValidator*>()->setLocale(locale);
            edit->setText(number(it.key() == "angle" && number(it.value()) == number(360) ? 0 : it.value()));
            edit->setModified(false);
        }
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

/// @brief 各直線項目を数値入力として登録し、個別確定のキー操作を監視する。
/// @param key 入力の内部識別名。
/// @param title 単位を含む項目名。
void ObjectInfoPanel::addLineEditor(const QString &key, const QString &title)
{
    // edit: 直線グループが所有する、1項目の下書き入力欄。
    auto *edit = new QLineEdit(m_lineGroup); edit->setObjectName("edit_" + key);
    // bound: 座標以外も有限の数値として検証するための上限。
    const double bound = std::numeric_limits<double>::max();
    // validator: 小数6桁・通常表記で入力を検証する、入力欄所有のバリデータ。
    auto *validator = new QDoubleValidator(key == "length" ? 0 : -bound, bound, DisplayPrecision, edit);
    if (key != "length" && key != "angle") validator->setRange(-ShapeData::CoordinateLimitMm, ShapeData::CoordinateLimitMm, DisplayPrecision);
    validator->setNotation(QDoubleValidator::StandardNotation); edit->setValidator(validator);
    edit->installEventFilter(this); m_lineForm->addRow(title, edit); m_lineEdits.insert(key,edit);
}

/// @brief 入力欄のEnterで1項目を確定し、Escでは未確定入力だけを戻す。
/// @param watched キー入力を監視するQtオブジェクト。
/// @param event 受け取ったイベント。処理したキーはここで受理する。
bool ObjectInfoPanel::eventFilter(QObject *watched, QEvent *event)
{
    // key: このパネルが管理する入力欄かを識別する項目名。
    const QString key = m_lineEdits.key(qobject_cast<QLineEdit*>(watched));
    if (!key.isEmpty() && event->type() == QEvent::KeyPress && isEnabled()) {
        // press: Enter・Escを判定する、Qt所有のキーイベント。
        auto *press = static_cast<QKeyEvent*>(event);
        if (press->key() == Qt::Key_Escape) { refresh(); event->accept(); return true; }
        if (press->key() == Qt::Key_Return || press->key() == Qt::Key_Enter) {
            commitLineField(key); event->accept(); return true;
        }
    }
    return QWidget::eventFilter(watched,event);
}

/// @brief PDF参照・補正等による無効化で、未確定の数値を破棄する。
/// @param event Qtから渡される有効状態等の変更イベント。
void ObjectInfoPanel::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::EnabledChange && !isEnabled()) refresh();
    QWidget::changeEvent(event);
}

/// @brief 1つの入力だけを最新の直線へ反映し、不正値では入力とモデルを保持する。
/// @param key 確定する項目名。長さ・角度は始点を固定して終点を動かす。
void ObjectInfoPanel::commitLineField(const QString &key)
{
    if (!isEnabled() || !m_document || !m_selection) return;
    // single: Enter時点の単独選択を再取得し、別図形へ古い入力が適用されるのを防ぐ。
    const auto single = m_selection->singleShape();
    if (!single || single->id != m_lineId || single->kind() != ShapeKind::Line) { refresh(); return; }
    // edit: 今回確定する1項目の入力欄。
    auto *edit = m_lineEdits.value(key);
    if (!edit || !edit->isModified()) return;
    // valid: 入力表記と有限値の変換がどちらも成立したかどうか。
    bool valid = false;
    // value: 項目の入力値。角度は度、それ以外は実寸mm。
    const double value = edit->findChild<QDoubleValidator*>()->locale().toDouble(edit->text(), &valid);
    if (!valid || !std::isfinite(value) || !edit->hasAcceptableInput()) {
        m_error->setText(tr("入力値が範囲外、または数値として不正です。小数点以下6桁までで指定してください。")); return;
    }
    // line: 現在の確定済み形状を実寸の端点へ変換した更新候補。
    QLineF line = std::get<QLineF>(single->geometryMm).translated(single->positionMm);
    // first: 座標編集では対象成分だけを更新し、長さ・角度編集では固定する始点（実寸mm）。
    QPointF first = line.p1();
    // second: 更新候補の終点（実寸mm）。未編集成分には丸めた表示値を使わない。
    QPointF second = line.p2();
    if (key == "startX") first.setX(value);
    else if (key == "startY") first.setY(value);
    else if (key == "endX") second.setX(value);
    else if (key == "endY") second.setY(value);
    else if (key == "length") {
        if (value <= 0) { m_error->setText(tr("長さは0より大きい値を指定してください。")); return; }
        // 先に単位方向へ変換し、極端に短い元の線でも倍率の計算をオーバーフローさせない。
        second = first + ((second - first) / line.length()) * value;
    } else if (key == "angle") {
        // degrees: 大きい角度入力でも三角関数の精度を保つ、正規化した角度（度）。
        double degrees = std::fmod(value,360); if (degrees < 0) degrees += 360;
        // direction: 端点更新に使う単位方向。軸方向では微小な計算ずれを残さない。
        QPointF direction;
        if (degrees == 0) direction = {1,0};
        else if (degrees == 90) direction = {0,1};
        else if (degrees == 180) direction = {-1,0};
        else if (degrees == 270) direction = {0,-1};
        else direction = {std::cos(degrees*Pi/180),std::sin(degrees*Pi/180)};
        second = first + direction * line.length();
    } else return;
    line.setP1(first); line.setP2(second);
    if (!m_document->setLineEndpoints(single->id,line)) {
        m_error->setText(tr("長さが0になる変更、または座標が±10000000mmを超える変更は適用できません。")); return;
    }
    refresh();
}
