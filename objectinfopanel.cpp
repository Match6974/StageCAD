/**
 * @file objectinfopanel.cpp
 * @brief 実寸形状を選択に応じたプロパティ表示へ変換し、移動・削除に追従する。
 */
#include "objectinfopanel.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "rectangleanchorselector.h"
#include "textalignmenteditor.h"
#include "textlayout.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QCheckBox>
#include <algorithm>
#include <QLabel>
#include <QGroupBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLocale>
#include <QLineEdit>
#include <QDoubleValidator>
#include <QKeyEvent>
#include <QKeySequence>
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

/// @brief 形状表示と直線・四角形・円の編集欄を作り、モデルの選択・形状・寿命の変更に接続する。
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
    m_anchorSelector = new RectangleAnchorSelector(m_shapeGroup);
    m_anchorLabel = new QLabel(m_shapeGroup); m_form->addRow(m_anchorLabel,m_anchorSelector);
    // コールバック: 固定点の変更だけでは図形を変えず、未確定入力と座標表示を更新する。
    connect(m_anchorSelector,&RectangleAnchorSelector::anchorChanged,this,&ObjectInfoPanel::refresh);
    addField("originX", tr("左下 X")); addField("originY", tr("左下 Y"));
    addField("width", tr("幅")); addField("height", tr("高さ"));
    addField("centerX", tr("中心 X")); addField("centerY", tr("中心 Y"));
    addField("radius", tr("半径")); addField("diameter", tr("直径")); addField("vertices", tr("頂点数"));
    addField("rectangleAngle",tr("角度 (°)")); addField("arcStart",tr("開始角度 (°)"));
    addField("arcSweep",tr("円弧角 (°)")); addField("vertexX",tr("頂点 X")); addField("vertexY",tr("頂点 Y"));
    m_vertexControls = new QWidget(m_shapeGroup);
    // vertexLayout: 頂点番号・操作・開閉をまとめるコンテナ所有のレイアウト。
    auto *vertexLayout = new QVBoxLayout(m_vertexControls); vertexLayout->setContentsMargins(0,0,0,0);
    m_vertexLabel = new QLabel(m_vertexControls); vertexLayout->addWidget(m_vertexLabel);
    // actions: CSVの頂点順に従う移動、再選択、挿入・削除・反転ボタンの内部名と表示名。
    const QList<QPair<QString,QString>> actions{{"back",tr("戻る")},{"current",tr("頂点")},{"next",tr("次へ")},
        {"add",tr("頂点を追加")},{"remove",tr("頂点を削除")},{"reverse",tr("方向を反転")}};
    // index: 3列ずつ並べるボタンの番号。
    int index = 0;
    // buttons: ボタンの行列を所有するレイアウト。
    auto *buttons = new QGridLayout; vertexLayout->addLayout(buttons);
    // action: 各ボタンの操作キーと表示名。
    for (const auto &action : actions) {
        // button: 操作コンテナが所有する頂点操作ボタン。
        auto *button = new QPushButton(action.second,m_vertexControls); button->setObjectName("vertex_"+action.first);
        buttons->addWidget(button,index/3,index%3); ++index;
        // コールバック: このボタンに対応する操作を最新の単独選択へ適用する。
        connect(button,&QPushButton::clicked,this,[this,action] { editVertex(action.first); });
    }
    m_closed = new QCheckBox(tr("多角形を閉じる"),m_vertexControls); m_closed->setObjectName("polygonClosed");
    vertexLayout->addWidget(m_closed); m_form->addRow(m_vertexControls);
    // コールバック: 開閉時も頂点列を維持し、不正な閉形状なら元の状態へ戻す。
    connect(m_closed,&QCheckBox::clicked,this,[this](bool closed) {
        // data: 最新の単独選択をコピーした更新候補。
        auto data = m_selection ? m_selection->singleShape() : std::nullopt;
        if (!isEnabled() || !data || data->id != m_editId || data->kind() != ShapeKind::Polygon) { refresh(); return; }
        data->polygonClosed = closed;
        if (!m_document->updateShape(*data)) { refresh(); m_error->setText(tr("3点以上の有効な面が必要なため閉じられません。")); }
    });
    m_textAlignment=new TextAlignmentEditor(m_shapeGroup); m_textAlignment->setObjectName("textObjectAlignment");
    m_form->addRow(tr("基準点（Alignment Point）"),m_textAlignment);
    // コールバック: 揃えを変えても基準点の実寸座標を固定し、編集を1回のUndo対象にする。
    connect(m_textAlignment,&TextAlignmentEditor::alignmentChanged,this,[this] {
        auto data=m_selection ? m_selection->singleShape() : std::nullopt; // 編集時点で選択中の値コピー。
        if (!isEnabled() || !m_document || !data || data->id != m_editId || data->kind() != ShapeKind::Text) { refresh(); return; }
        const auto &text=std::get<TextGeometry>(data->geometryMm); // 現在の設定サイズと固定する実寸点。
        const auto geometry=makeTextGeometry(text.text,text.pointSizePt,text.anchorMm,
            m_textAlignment->horizontalAlignment(),m_textAlignment->verticalAlignment()); // 揃えだけを変更した字形候補。
        if (geometry) data->geometryMm=*geometry;
        if (!geometry || !m_document->updateShape(*data)) { refresh(); m_error->setText(tr("基準点の変更後に座標範囲を超えるため適用できません。")); }
    });
    m_textEditButton = new QPushButton(tr("文字・サイズを編集…"),m_shapeGroup);
    m_textEditButton->setObjectName("editSelectedTextButton"); m_form->addRow(m_textEditButton);
    // コールバック: 表示されたUUIDを再確認し、編集対象だけをメイン画面へ渡す。
    connect(m_textEditButton,&QPushButton::clicked,this,[this] {
        const auto data=m_selection ? m_selection->singleShape() : std::nullopt; // 現在の単独選択。
        if (isEnabled() && data && data->id == m_editId && data->kind() == ShapeKind::Text)
            emit textEditRequested(data->id);
    });
    contentLayout->addWidget(m_shapeGroup);
    m_lineGroup = new QGroupBox(tr("形状"), content);
    m_lineForm = new QFormLayout(m_lineGroup); m_lineForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    addLineEditor("startX", tr("始点 X (mm)")); addLineEditor("startY", tr("始点 Y (mm)"));
    addLineEditor("endX", tr("終点 X (mm)")); addLineEditor("endY", tr("終点 Y (mm)"));
    addLineEditor("length", tr("長さ (mm)")); addLineEditor("angle", tr("角度 (°)"));
    m_edits.value("angle")->setToolTip(tr("+X方向を0°とした反時計回りの角度。始点を固定して変更します。"));
    contentLayout->addWidget(m_lineGroup);
    // hint: 各入力の確定・取消と、固定点を説明する直線グループ所有のラベル。
    auto *hint = new QLabel(tr("各項目はEnterで確定、Escで取消。\n長さ・角度の変更では始点を固定します。"), m_lineGroup);
    hint->setWordWrap(true); m_lineForm->addRow(hint);
    m_error = new QLabel(content); m_error->setObjectName("lineEditError"); m_error->setWordWrap(true);
    contentLayout->addWidget(m_error);
    // units: 表示単位と座標原点を説明する、コンテナ所有のラベル。
    auto *units = new QLabel(tr("数値入力はEnterで確定、Escで取消。\n座標・長さは実寸mmです。\n原点は用紙中心、右が+X、上が+Yです。"), content);
    units->setWordWrap(true); contentLayout->addWidget(units); contentLayout->addStretch();
    scroll->setWidget(content); layout->addWidget(scroll);
    if (selection) {
        connect(selection, &ShapeSelectionModel::selectionChanged, this, &ObjectInfoPanel::refresh);
        // コールバック: 選択モデルの破棄後に古い図形情報を残さない。
        connect(selection, &QObject::destroyed, this, [this] { m_selection = nullptr; refresh(); });
    }
    if (document) {
        connect(document,&DrawingDocument::historyAboutToRestore,this,&ObjectInfoPanel::refresh);
        // id: 変更・削除された図形のUUID。選択外の変更では現在の下書きを消さない。
        // コールバック: 表示中の図形だけを最新値へ同期し、他の図形の更新を無視する。
        const auto refreshSelected = [this](const QUuid &id) { if (id == m_editId) refresh(); };
        connect(document, &DrawingDocument::shapeChanged, this, refreshSelected);
        connect(document, &DrawingDocument::shapeRemoved, this, refreshSelected);
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
    // row: 読み取りラベルと数値入力を切り替える、形状グループ所有のコンテナ。
    auto *row = new QWidget(m_shapeGroup);
    // layout: 行内の余白を設けず、現在のモードに合う表示要素を並べるレイアウト。
    auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
    // value: 値をコピーできる、行コンテナ所有の読み取りラベル。
    auto *value = new QLabel(row); value->setObjectName("property_" + key);
    value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    value->setWordWrap(true); layout->addWidget(value);
    if (key != "vertices") {
        // edit: 選択図形の種類に応じて表示する数値入力欄。
        auto *edit = createEditor(key,row); edit->hide(); layout->addWidget(edit);
    }
    m_form->addRow(title,row); m_values.insert(key,value); m_fieldRows.insert(key,row);
}

/// @brief 指定項目の表示文字列を設定し、その行を見える状態にする。
/// @param key 登録済みの項目名。
/// @param value 単位を含む表示文字列。
void ObjectInfoPanel::showField(const QString &key, const QString &value)
{
    // label: グループが所有する、表示対象の値ラベル。
    auto *label = m_values.value(key); label->setText(value); label->show();
    if (m_edits.contains(key)) m_edits.value(key)->hide();
    m_form->setRowVisible(m_fieldRows.value(key), true);
}

/// @brief 現在の選択図形を読み取り、該当する実寸項目だけを表示する。
void ObjectInfoPanel::refresh()
{
    // value: 以前の図形から値が残らないよう消去する、グループ所有のラベル。
    for (auto *value : m_values) value->clear();
    // row: 前の図形にだけ必要だった項目を隠す、形状グループ所有のコンテナ。
    for (auto *row : m_fieldRows) m_form->setRowVisible(row,false);
    m_error->clear();
    // previousId: 選択図形が変わったときだけ頂点番号を先頭へ戻すための前回UUID。
    const QUuid previousId = m_editId; m_editId = {};
    // edit: 以前の図形の下書きを破棄する、パネル内コンテナ所有の入力欄。
    for (auto *edit : m_edits) { edit->clear(); edit->setModified(false); }
    // single: 単独選択時に取得する値データ。表示のためにモデルを変更しない。
    const auto single = m_document && m_selection ? m_selection->singleShape() : std::nullopt;
    // isLine: 単独選択した直線では、専用の端点・長さ・角度のフォームを使う。
    const bool isLine = single && single->kind() == ShapeKind::Line;
    // isRectangle: 四角形だけで9点の固定位置選択を表示する。
    const bool isRectangle = single && single->kind() == ShapeKind::Rectangle;
    // isPolygon: 頂点編集と外接矩形の9点基準を表示する対象。
    const bool isPolygon = single && single->kind() == ShapeKind::Polygon;
    m_form->setRowVisible(m_textEditButton,single && single->kind() == ShapeKind::Text);
    m_form->setRowVisible(m_textAlignment,single && single->kind() == ShapeKind::Text);
    m_form->setRowVisible(m_anchorSelector,isRectangle || isPolygon);
    m_form->setRowVisible(m_vertexControls,isPolygon);
    if (!isPolygon && (!single || single->kind() != ShapeKind::Text)) emit vertexHighlighted({},false);
    m_anchorLabel->setText(tr("固定点: %1").arg(m_anchorSelector->anchorName()));
    // axis: 四角形・多角形・文字では基準点、その他では左下として座標の意味を明示する行名。
    for (const QString axis : {QStringLiteral("X"),QStringLiteral("Y")}) {
        // key: 基準点・外接範囲の座標を表示する項目名。
        const QString key = "origin" + axis;
        // label: フォームが生成・所有する、座標の項目名ラベル。
        auto *label = qobject_cast<QLabel*>(m_form->labelForField(m_fieldRows.value(key)));
        label->setText((isRectangle || isPolygon) ? tr("基準点 %1").arg(axis) : (single && single->kind() == ShapeKind::Text ? tr("基準点 %1").arg(axis) : tr("左下 %1").arg(axis)));
    }
    m_shapeGroup->setVisible(single.has_value() && !isLine); m_lineGroup->setVisible(isLine);
    if (!single) {
        // count: 有効な選択図形数。文書がない場合は未選択として扱う。
        const int count = m_document && m_selection ? m_selection->count() : 0;
        m_summary->setText(count > 1 ? tr("%1個の図形を選択中\n1個選択すると形状情報を表示します。").arg(count)
                                    : tr("図形を選択すると形状情報を表示します。"));
        return;
    }
    m_editId = single->id;
    if (previousId != m_editId) m_vertexIndex = 0;
    // offset: 図形の形状全体に加える配置移動量（実寸mm）。
    const QPointF offset = single->positionMm;
    switch (single->kind()) {
    case ShapeKind::Line: {
        m_summary->setText(tr("直線"));
        // line: 2端点のローカル実寸形状。配置移動量は別に加える。
        const auto line = std::get<QLineF>(single->geometryMm);
        m_editId = single->id;
        // world: 配置移動量を反映した、用紙中心基準の実寸端点（mm）。
        const QLineF world = line.translated(offset);
        // values: 直線6項目の表示値。角度だけ度、それ以外は実寸mm。
        const QHash<QString,double> values{{"startX",world.x1()},{"startY",world.y1()},
            {"endX",world.x2()},{"endY",world.y2()},{"length",world.length()},{"angle",lineAngle(world)}};
        // it: 最新の実寸値を、対応する数値入力へ反映する項目走査。
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            showEditor(it.key(),it.key() == "angle" && number(it.value()) == number(360) ? 0 : it.value());
        break;
    }
    case ShapeKind::Text: {
        const auto &text=std::get<TextGeometry>(single->geometryMm); // 設定ptと固定フォント。縮尺による見た目と区別する。
        m_summary->setText(tr("文字（横書き）\n%1 / %2 pt\n%3").arg(text.fontFamily).arg(text.pointSizePt,0,'g',15).arg(text.text));
        m_textAlignment->setAlignment(text.horizontalAlignment,text.verticalAlignment);
        emit vertexHighlighted(text.anchorMm+offset,true); // 図面上にも現在固定するAlignment Pointを表示する。
        showEditor("originX",text.anchorMm.x()+offset.x()); showEditor("originY",text.anchorMm.y()+offset.y());
        break;
    }
    case ShapeKind::Circle: {
        // circle: 真円の中心と半径の形状データ（実寸mm）。
        const auto circle = std::get<CircleGeometry>(single->geometryMm);
        m_summary->setText(circle.sweepAngleDeg == 360 ? tr("円") : tr("円弧"));
        showField("centerX", millimeters(circle.centerMm.x() + offset.x())); showEditor("centerX",circle.centerMm.x()+offset.x());
        showField("centerY", millimeters(circle.centerMm.y() + offset.y())); showEditor("centerY",circle.centerMm.y()+offset.y());
        showField("radius", millimeters(circle.radiusMm)); showEditor("radius",circle.radiusMm);
        showField("diameter", millimeters(2 * circle.radiusMm)); showEditor("diameter",2*circle.radiusMm);
        showEditor("arcStart",circle.startAngleDeg); showEditor("arcSweep",circle.sweepAngleDeg);
        break;
    }
    case ShapeKind::Rectangle:
    case ShapeKind::Polygon: {
        // bounds: 配置移動量を反映した外接範囲（実寸mm）。minYがCAD上の下辺に対応する。
        const QRectF bounds = single->boundsMm();
        m_summary->setText(single->kind() == ShapeKind::Rectangle ? tr("四角") : tr("多角形"));
        showField("originX", millimeters(bounds.left())); showField("originY", millimeters(bounds.top()));
        showField("width", millimeters(bounds.width())); showField("height", millimeters(bounds.height()));
        // fraction: 四角形・外接矩形で共用する9点基準（無次元）。
        const QPointF fraction = m_anchorSelector->normalizedAnchor();
        // reference: 回転後の四角形または多角形の外接範囲の基準点（実寸mm）。
        const QPointF reference = single->referencePoint(fraction);
        showField("originX",millimeters(reference.x())); showField("originY",millimeters(reference.y()));
        showEditor("originX",reference.x()); showEditor("originY",reference.y());
        if (single->kind() == ShapeKind::Polygon) {
            // points: 順序を維持して個別編集する頂点列（ローカル実寸mm）。
            const auto &points = std::get<QPolygonF>(single->geometryMm);
            m_vertexIndex = std::clamp(m_vertexIndex,qsizetype(0),points.size()-1);
            showField("vertices",QString::number(points.size()));
            showEditor("width",bounds.width()); showEditor("height",bounds.height());
            showEditor("vertexX",points[m_vertexIndex].x()+offset.x());
            showEditor("vertexY",points[m_vertexIndex].y()+offset.y());
            m_vertexLabel->setText(tr("頂点 %1 / %2").arg(m_vertexIndex+1).arg(points.size()));
            m_closed->setChecked(single->polygonClosed);
            emit vertexHighlighted(points[m_vertexIndex]+offset,true);
        } else {
            // rect: 回転前の辺長を保持する矩形。外接幅と混同しない。
            const auto &rect = std::get<QRectF>(single->geometryMm);
            showEditor("width",rect.width()); showEditor("height",rect.height());
            showEditor("rectangleAngle",single->rectangleAngleDeg);
        }
        break;
    }
    }
}

/// @brief 各直線項目を数値入力として登録し、個別確定のキー操作を監視する。
/// @param key 入力の内部識別名。
/// @param title 単位を含む項目名。
void ObjectInfoPanel::addLineEditor(const QString &key, const QString &title)
{
    m_lineForm->addRow(title,createEditor(key,m_lineGroup));
}

/// @brief 共有する入力形式とキーボード操作を持つ数値欄を作成する。
/// @param key 直線座標・角度・寸法を区別する項目名。
/// @param parent 入力欄を所有するグループまたは行コンテナ。
QLineEdit *ObjectInfoPanel::createEditor(const QString &key, QWidget *parent)
{
    // edit: 呼び出し元のコンテナが所有する数値入力欄。
    auto *edit = new QLineEdit(parent); edit->setObjectName("edit_"+key);
    // bound: 角度・寸法も有限のdoubleとして入力できる最大値。
    const double bound = std::numeric_limits<double>::max();
    // dimension: 正の値だけを確定できる長さ・幅・高さ・半径の項目かどうか。
    const bool dimension = key == "length" || key == "width" || key == "height" || key == "radius" || key == "diameter";
    // validator: 表示桁と揃えた通常表記を受け付ける、入力欄所有の検証器。
    auto *validator = new QDoubleValidator(dimension ? 0 : -bound,bound,DisplayPrecision,edit);
    if (!dimension && key != "angle" && key != "rectangleAngle" && key != "arcStart" && key != "arcSweep") validator->setRange(-ShapeData::CoordinateLimitMm,ShapeData::CoordinateLimitMm,DisplayPrecision);
    validator->setNotation(QDoubleValidator::StandardNotation); edit->setValidator(validator);
    edit->installEventFilter(this); m_edits.insert(key,edit);
    return edit;
}

/// @brief 表示用の丸め値を入力へ反映し、ユーザーが変更するまで未編集として保持する。
/// @param key 表示する編集項目の内部名。
/// @param value 現在の実寸mmまたは角度（度）。
void ObjectInfoPanel::showEditor(const QString &key, double value)
{
    // edit: 最新の数値を表示する、形状グループまたは行コンテナ所有の入力欄。
    auto *edit = m_edits.value(key);
    // locale: 小数点記号はOSに合わせ、桁区切りは拒否する入力ロケール。
    QLocale locale; locale.setNumberOptions(QLocale::RejectGroupSeparator | QLocale::OmitGroupSeparator);
    edit->findChild<QDoubleValidator*>()->setLocale(locale);
    edit->setText(number(value)); edit->setModified(false); edit->show();
    if (m_values.contains(key)) {
        m_values.value(key)->hide(); m_form->setRowVisible(m_fieldRows.value(key),true);
    }
}

/// @brief 入力欄のEnterで1項目を確定し、Escでは未確定入力だけを戻す。
/// @param watched キー入力を監視するQtオブジェクト。
/// @param event 受け取ったイベント。処理したキーはここで受理する。
bool ObjectInfoPanel::eventFilter(QObject *watched, QEvent *event)
{
    // key: このパネルが管理する入力欄かを識別する項目名。
    const QString key = m_edits.key(qobject_cast<QLineEdit*>(watched));
    if (!key.isEmpty() && event->type() == QEvent::ShortcutOverride && isEnabled()) {
        // shortcut: 文字の下書きがなければ、文書のUndo／Redoへ渡す標準キー。
        auto *shortcut = static_cast<QKeyEvent*>(event);
        // edit: 未確定文字のUndoをすべて戻した後も、文字Redoの分岐を保持する入力欄。
        auto *edit = m_edits.value(key);
        // local: 文書より先に処理する文字入力の履歴があるかどうか。
        const bool local = shortcut->matches(QKeySequence::Undo) ? edit->isUndoAvailable() : edit->isRedoAvailable();
        if ((shortcut->matches(QKeySequence::Undo) || shortcut->matches(QKeySequence::Redo)) && !edit->isModified() && !local) {
            event->ignore(); return true;
        }
    }
    if (!key.isEmpty() && event->type() == QEvent::KeyPress && isEnabled()) {
        // press: Enter・Escを判定する、Qt所有のキーイベント。
        auto *press = static_cast<QKeyEvent*>(event);
        if (press->key() == Qt::Key_Escape) { refresh(); event->accept(); return true; }
        if (press->key() == Qt::Key_Return || press->key() == Qt::Key_Enter) {
            commitField(key); event->accept(); return true;
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

/// @brief 1項目を最新の単独選択図形へ反映する。不正値では入力とモデルを保持する。
/// @param key 確定する項目名。直線は始点、四角形は選択点、円は中心を固定する。
void ObjectInfoPanel::commitField(const QString &key)
{
    if (!isEnabled() || !m_document || !m_selection) return;
    // single: Enter時点の単独選択を再取得し、別図形へ古い入力が適用されるのを防ぐ。
    const auto single = m_selection->singleShape();
    if (!single || single->id != m_editId) { refresh(); return; }
    // edit: 今回確定する1項目の入力欄。
    auto *edit = m_edits.value(key);
    if (!edit || !edit->isModified()) return;
    // valid: 入力表記と有限値の変換がどちらも成立したかどうか。
    bool valid = false;
    // value: 項目の入力値。角度は度、それ以外は実寸mm。
    const double value = edit->findChild<QDoubleValidator*>()->locale().toDouble(edit->text(), &valid);
    if (!valid || !std::isfinite(value) || !edit->hasAcceptableInput()) {
        m_error->setText(tr("入力値が範囲外、または数値として不正です。小数点以下6桁までで指定してください。")); return;
    }
    if (single->kind() != ShapeKind::Line) {
        // next: 未編集属性・精度・UUIDを保つ更新候補。失敗時は文書に渡さない。
        ShapeData next = *single;
        // fraction: 寸法・回転の前後で実寸座標を保持する選択基準。
        const QPointF fraction = m_anchorSelector->normalizedAnchor();
        // fixed: 寸法・角度変更前の基準点（実寸mm）。
        const QPointF fixed = next.referencePoint(fraction);
        if (key == "originX" || key == "originY") {
            next.positionMm += key == "originX" ? QPointF(value-fixed.x(),0) : QPointF(0,value-fixed.y());
        } else if (next.kind() == ShapeKind::Rectangle) {
            // rect: 辺長だけを編集する矩形候補。回転と配置は別に維持する。
            auto &rect = std::get<QRectF>(next.geometryMm);
            if (key == "width" && value > 0) rect.setWidth(value);
            else if (key == "height" && value > 0) rect.setHeight(value);
            else if (key == "rectangleAngle") {
                next.rectangleAngleDeg = std::fmod(value,360); if (next.rectangleAngleDeg < 0) next.rectangleAngleDeg += 360;
            } else { m_error->setText(tr("寸法は0より大きい値が必要です。")); return; }
            next.positionMm += fixed-next.referencePoint(fraction);
        } else if (next.kind() == ShapeKind::Circle) {
            // circle: 中心・寸法・開始角・円弧角を個別に編集する候補。
            auto &circle = std::get<CircleGeometry>(next.geometryMm);
            if (key == "centerX") circle.centerMm.setX(value-next.positionMm.x());
            else if (key == "centerY") circle.centerMm.setY(value-next.positionMm.y());
            else if ((key == "radius" || key == "diameter") && value > 0) circle.radiusMm = key == "diameter" ? value/2 : value;
            else if (key == "arcStart") { circle.startAngleDeg = std::fmod(value,360); if (circle.startAngleDeg < 0) circle.startAngleDeg += 360; }
            else if (key == "arcSweep" && value > 0 && value <= 360) circle.sweepAngleDeg = value;
            else { m_error->setText(tr("寸法は正の値、円弧角は0より大きく360°以下で指定してください。")); return; }
        } else if (next.kind() == ShapeKind::Polygon) {
            // points: 個別頂点または外接範囲の拡縮に使うローカル実寸頂点列。
            auto &points = std::get<QPolygonF>(next.geometryMm);
            if (key == "vertexX" && m_vertexIndex < points.size()) points[m_vertexIndex].setX(value-next.positionMm.x());
            else if (key == "vertexY" && m_vertexIndex < points.size()) points[m_vertexIndex].setY(value-next.positionMm.y());
            else if ((key == "width" || key == "height") && value > 0) {
                // bounds: 拡縮前の実寸外接範囲。
                const QRectF bounds = next.boundsMm();
                // extent: 縦または横だけの倍率を計算する元の辺長（実寸mm）。
                const double extent = key == "width" ? bounds.width() : bounds.height();
                if (extent <= 0) { m_error->setText(tr("元の寸法が0の方向には拡縮できません。頂点座標を編集してください。")); return; }
                // point: 選択基準点の周りで拡縮する各頂点（ローカル実寸mm）。
                for (auto &point : points) {
                    if (key == "width") point.setX((point.x()+next.positionMm.x()-fixed.x())*(value/extent)+fixed.x()-next.positionMm.x());
                    else point.setY((point.y()+next.positionMm.y()-fixed.y())*(value/extent)+fixed.y()-next.positionMm.y());
                }
            } else { m_error->setText(tr("寸法は0より大きい値が必要です。")); return; }
        }
        else { m_error->setText(tr("この文字項目は編集できません。")); return; }
        // 配置をローカル座標へ統合し、移動済み図形でも有効な実寸範囲全体を編集できるようにする。
        if (auto *rect = std::get_if<QRectF>(&next.geometryMm)) *rect = rect->translated(next.positionMm);
        else if (auto *circle = std::get_if<CircleGeometry>(&next.geometryMm)) circle->centerMm += next.positionMm;
        else if (auto *text=std::get_if<TextGeometry>(&next.geometryMm)) text->anchorMm += next.positionMm;
        else std::get<QPolygonF>(next.geometryMm).translate(next.positionMm);
        next.positionMm = {};
        if (!m_document->updateShape(next)) {
            m_error->setText(tr("変更後の形状が無効、または座標範囲（±10000000mm）を超えるため適用できません。")); return;
        }
        refresh(); return;
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

/// @brief 選択頂点を巡回し、頂点数・実寸範囲を検証してから編集を確定する。
/// @param action back/next/current/add/remove/reverseの操作名。
void ObjectInfoPanel::editVertex(const QString &action)
{
    if (!isEnabled() || !m_document || !m_selection) return;
    // data: 最新の単独選択のコピー。別図形への古いボタン操作を拒否する。
    auto data = m_selection->singleShape();
    if (!data || data->id != m_editId || data->kind() != ShapeKind::Polygon) return;
    // points: 順序を保って変更する候補のローカル実寸頂点列。
    auto &points = std::get<QPolygonF>(data->geometryMm);
    // previous: 不正な更新を拒否した場合に戻す元の頂点番号。
    const qsizetype previous = m_vertexIndex;
    m_vertexIndex = std::clamp(m_vertexIndex,qsizetype(0),points.size()-1);
    if (action == "back") m_vertexIndex = (m_vertexIndex+1)%points.size();
    else if (action == "next") m_vertexIndex = (m_vertexIndex+points.size()-1)%points.size();
    else if (action == "add") {
        // insert: 開いた終点では前の辺、それ以外では選択頂点の次の辺へ挿入する番号。
        const qsizetype insert = !data->polygonClosed && m_vertexIndex == points.size()-1 ? m_vertexIndex : m_vertexIndex+1;
        // first: 挿入する辺の始点の番号。
        const qsizetype first = insert-1;
        // middle: 既存の辺の中点（ローカル実寸mm）。
        const QPointF middle = (points[first]+points[insert%points.size()])/2;
        points.insert(insert,middle); m_vertexIndex = insert;
    } else if (action == "remove") {
        if (points.size() <= (data->polygonClosed ? 3 : 2)) { m_error->setText(tr("閉じた多角形は3点、開いた形状は2点以上必要です。")); return; }
        points.removeAt(m_vertexIndex); m_vertexIndex = std::min(m_vertexIndex,points.size()-1);
    } else if (action == "reverse") {
        std::reverse(points.begin(),points.end()); m_vertexIndex = points.size()-1-m_vertexIndex;
    }
    if (action == "back" || action == "next" || action == "current") { refresh(); return; }
    if (!m_document->updateShape(*data)) {
        m_vertexIndex = previous; refresh(); m_error->setText(tr("有効な頂点・形状を保てないため適用できません。"));
    }
}
