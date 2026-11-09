/**
 * @file drawingview.cpp
 * @brief CAD座標を表示し、描画・選択・移動・削除・PDF距離測定の入力を処理する。
 */

#include "drawingview.h"
#include "drawingsettings.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"

#include <QGraphicsRectItem>
#include <QGraphicsLineItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsPolygonItem>
#include <QMouseEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QContextMenuEvent>
#include <QMenu>
#include <QResizeEvent>
#include <QApplication>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <algorithm>
#include <cmath>
#include <array>
#include <limits>

namespace {
/// @brief 標準アイテムの型と面描画を維持し、開いた形状だけを閉じずに描画する。シーンが所有する。
/// @tparam Base 円または多角形の標準描画アイテム。
template<class Base> class OpenShapeItem : public Base {
public:
    /// @brief 開閉状態とローカル実寸パスを同期する。
    /// @param path 描画する開いたパス（実寸mm）。
    /// @param open 面を塗らず、終点と始点を結ばないかどうか。
    void setOpenPath(const QPainterPath &path, bool open) { m_path = path; m_open = open; this->update(); }
    /// @brief 開いた形状のヒット領域には線幅だけを与える。
    QPainterPath shape() const override {
        if (!m_open) return Base::shape();
        // stroker: 開いた線だけをヒット対象にする生成器。
        QPainterPathStroker stroker; stroker.setWidth(this->pen().widthF()); return stroker.createStroke(m_path);
    }
    /// @brief 真円・閉多角形では標準描画、円弧・開多角形では線だけを描画する。
    /// @param painter Qtが所有する描画コンテキスト。
    /// @param option 選択状態などの描画情報。
    /// @param widget 描画先。所有権は取得しない。
    void paint(QPainter *painter,const QStyleOptionGraphicsItem *option,QWidget *widget) override {
        if (!m_open) { Base::paint(painter,option,widget); return; }
        painter->save(); painter->setPen(this->pen()); painter->setBrush(Qt::NoBrush); painter->drawPath(m_path);
        if (option->state & QStyle::State_Selected) {
            // selectionPen: ズームに依存しない選択枠用の画面線幅。
            QPen selectionPen(Qt::black,0,Qt::DashLine); painter->setPen(selectionPen); painter->drawRect(this->boundingRect());
        }
        painter->restore();
    }
private:
    // m_path: 終点と始点を結ばずに描画するローカル実寸形状。
    QPainterPath m_path;
    // m_open: 標準の塗りつぶし描画を使わない場合だけtrue。
    bool m_open = false;
};

/// @brief 座標の各成分が有限で、実寸mmの許容範囲内にあるかを返す。
/// @param point 用紙中心基準の実寸座標（mm）。
bool validPoint(QPointF point) { return ShapeData::validPoint(point); }
/// @brief 全頂点が有効で、同一直線上にない3点以上を含むかを確認する。
/// @param points 指定順に並ぶ多角形の頂点列（実寸mm）。
bool validPolygon(const QPolygonF &points) { return ShapeData::validPolygon(points); }

/// @brief 始点を基準に、最も近い許可角度へ終点を射影する。入出力は実寸mm。
/// @param first 用紙中心基準の実寸座標（mm）。
/// @param point 用紙中心基準の実寸座標（mm）。
QPointF constrainedLinePoint(QPointF first, QPointF point)
{
    // pi: 度とラジアンの変換に使う円周率。
    constexpr double pi = 3.14159265358979323846;
    // delta: 開始位置から加える移動量（実寸mm）。
    const QPointF delta = point - first;
    if (delta.isNull()) return point;
    // angle: 線分または候補方向の角度（ラジアン、テスト入力では度）。
    const double angle = std::atan2(delta.y(), delta.x());
    // angles: Shiftで許可する全象限の角度一覧（度）。
    const std::array<int, 16> angles = {0, 30, 45, 60, 90, 120, 135, 150,
                                      180, 210, 225, 240, 270, 300, 315, 330};
    // nearest: 現在までに見つかった最も近い許可角度（ラジアン）。
    // difference: 候補角度との最小の差（ラジアン）。
    double nearest = 0, difference = std::numeric_limits<double>::max();
    // degrees: 許可方向を走査するときの角度（度）。
    for (const int degrees : angles) {
        // candidate: 比較対象の許可角度またはツールボタン。
        const double candidate = degrees * pi / 180;
        // distance: 候補方向との角度差の絶対値（ラジアン）。
        const double distance = std::abs(std::remainder(angle - candidate, 2 * pi));
        if (distance < difference) { nearest = candidate; difference = distance; }
    }
    // direction: 選んだ許可角度の単位方向ベクトル。
    const QPointF direction(std::cos(nearest), std::sin(nearest));
    // projected: 線分を許可方向へ射影した長さ（実寸mm）。
    const double projected = delta.x() * direction.x() + delta.y() * direction.y();
    return first + direction * projected;
}

/// @brief 画面上の線幅がズームで変化しない、確定図形用のペンを返す。
QPen shapePen()
{
    // pen: 画面上の線や選択マーカーに使う描画ペン。
    QPen pen(QColor("#182b3d"), 1.5); pen.setCosmetic(true); return pen;
}

/// @brief 確定した面図形に使う半透明の塗りを返す。
QBrush shapeBrush() { return QColor(95, 155, 215, 65); }
}

/// @brief 図面の拡大率に関係なく、選択頂点を小さな丸で示す。
/// @param point マーカーの実寸座標（mm）。
/// @param visible マーカーの表示状態。
void DrawingView::highlightVertex(QPointF point,bool visible)
{
    if (!m_vertexMarker) {
        m_vertexMarker = scene()->addEllipse(-4,-4,8,8,QPen(QColor("#c35100"),1),QBrush(QColor("#ffdf60")));
        m_vertexMarker->setFlag(QGraphicsItem::ItemIgnoresTransformations);
        m_vertexMarker->setAcceptedMouseButtons(Qt::NoButton); m_vertexMarker->setZValue(m_shapes.size()+1);
    }
    m_vertexMarker->setPos(point); m_vertexMarker->setVisible(visible); viewport()->update();
}

/// @brief 図形モデル・シーン・用紙を作り、座標表示と入力処理を接続する。
/// @param settings 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
DrawingView::DrawingView(DrawingSettings *settings, QWidget *parent)
    : QGraphicsView(parent), m_settings(settings)
{
    setObjectName("drawingView");
    setScene(new QGraphicsScene(this));
    m_document = new DrawingDocument(this); m_document->setObjectName("drawingDocument");
    // 単独ビューを既存縮尺で作る場合も、最初のレイヤーと表示変換を一致させる。
    m_document->setLayerScale(m_document->activeLayerId(),m_settings->scaleDenominator());
    m_selectionModel = new ShapeSelectionModel(m_document, this);
    m_selectionModel->setObjectName("shapeSelectionModel");
    // コールバック: 外部からの選択変更でも未確定移動を取り消し、表示と通知を更新する。
    connect(m_selectionModel, &ShapeSelectionModel::selectionChanged, this, [this] {
        endShapeMove(false); syncSelectionDisplay();
        emit shapeSelectionChanged(); emit drawingHintChanged(drawingHint());
    });
    connect(scene(), &QGraphicsScene::selectionChanged, this, &DrawingView::syncSelectionDisplay);
    // コールバック: モデル変更の前に移動プレビューを取り消し、変更前の表示へ戻す。
    connect(m_document, &DrawingDocument::aboutToChange, this, [this] { endShapeMove(false); });
    // コールバック: 履歴復元では未確定の作成・移動・パンを残さない。
    connect(m_document,&DrawingDocument::historyAboutToRestore,this,[this] {
        endShapeMove(false); cancelDrawing(); m_panning = false; m_selectionMenu->hide(); updateInteractionCursor();
    });
    // コールバック: レイヤー変更で未確定描画を破棄し、表示・色・選択可否を同期する。
    connect(m_document,&DrawingDocument::layersChanged,this,[this] {
        endShapeMove(false); cancelDrawing();
        // active: 今回の表示換算へ反映するアクティブレイヤーの値。
        const auto active = m_document->layer(m_document->activeLayerId());
        if (active) m_settings->setScaleDenominator(active->scaleDenominator);
        // data: 表示属性を再適用する確定図形。
        for (const auto &data : m_document->shapes()) syncShape(data.id);
    });
    connect(m_document, &DrawingDocument::shapeAdded, this, &DrawingView::syncShape);
    connect(m_document, &DrawingDocument::shapeChanged, this, &DrawingView::syncShape);
    connect(m_document, &DrawingDocument::shapeRemoved, this, &DrawingView::removeShapeItem);
    m_selectionMenu = new QMenu(this); m_selectionMenu->setObjectName("shapeSelectionMenu");
    // remove: 右クリックメニューの削除アクション。親メニューが所有する。
    auto *remove = m_selectionMenu->addAction(tr("削除")); remove->setObjectName("deleteSelectedShapesAction");
    // コールバック: 削除メニューの選択を、編集可否を検証する共通削除処理へ渡す。
    connect(remove, &QAction::triggered, this, [this] { deleteSelectedShapes(); });
    setBackgroundBrush(QColor("#51565e"));
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    // rememberScroll: スクロール変更からカメラ中心を記録するコールバック。
    // コールバック: ユーザーがスクロールしたときだけ、現在の論理カメラ中心を記録する。
    auto rememberScroll = [this] {
        if (!m_updatingView) m_viewCenter = mapToScene(viewport()->rect().center());
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, rememberScroll);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, rememberScroll);
    setToolTip(tr("CAD座標は実寸mmです。原点は用紙中心、Xは右向き、Yは上向きです。"));

    // border: 用紙の輪郭を描く、画面上の幅を保つペン。
    QPen border(QColor("#252a30"));
    border.setCosmetic(true);
    m_paper = scene()->addRect({}, border, Qt::white);
    m_paper->setZValue(-1000);
    // printable: 印刷可能範囲を描く破線のペン。
    QPen printable(QColor("#888888"), 1, Qt::DashLine);
    printable.setCosmetic(true);
    m_printableArea = scene()->addRect({}, printable, Qt::NoBrush);
    m_printableArea->setZValue(-999);

    connect(settings, &DrawingSettings::scaleChanged, this, &DrawingView::updatePaper);
    // denominator: 既存の表示設定APIから変更された新しい縮尺の分母。
    // コールバック: 表示設定への既存呼び出しもアクティブレイヤーだけへ反映する。
    m_scaleConnection = connect(settings,&DrawingSettings::scaleChanged,this,[this](double denominator) {
        // active: 表示値と二重適用・循環しないよう比較する正本の縮尺。
        const auto active = m_document->layer(m_document->activeLayerId());
        if (active && active->scaleDenominator != denominator && !m_document->setLayerScale(active->id,denominator))
            m_settings->setScaleDenominator(active->scaleDenominator);
    });
    updatePaper();
}

/// @brief Qtの基底ビューがシーンを切り離す際、破棄済みの派生メンバーへ通知させない。
DrawingView::~DrawingView()
{
    disconnect(scene(), nullptr, this, nullptr);
    disconnect(m_document, nullptr, this, nullptr);
    disconnect(m_scaleConnection);
    disconnect(horizontalScrollBar(), nullptr, this, nullptr);
    disconnect(verticalScrollBar(), nullptr, this, nullptr);
    // 文書破棄による選択解除通知より先にモデルを解放する。Qtの子リストからも自動で外れる。
    delete m_selectionModel;
}

/// @brief 有効なズーム倍率を設定する。実寸座標を保ち、不正倍率ではfalseを返す。
/// @param zoom 用紙基準の表示に対する画面ズーム倍率。
bool DrawingView::setDisplayZoom(double zoom)
{
    if (!std::isfinite(zoom) || zoom < 0.1 || zoom > 10.0) return false;
    // center: 用紙中心基準の実寸座標（mm）。
    const QPointF center = m_viewCenter;
    m_displayZoom = zoom;
    updateViewTransform(center);
    return true;
}

/// @brief 用紙中心と基準ズームへ戻す。図形やPDFの実寸は変更しない。
void DrawingView::resetView()
{
    m_displayZoom = 1;
    updateViewTransform({});
}

/// @brief 対応済みのツールへ切り替える。測定中は拒否し、未確定操作は取り消す。
/// @param tool 現在処理するツールの種類。
bool DrawingView::setActiveTool(DrawingTool tool)
{
    if (m_picking || (tool != DrawingTool::Selection && tool != DrawingTool::Pan
        && tool != DrawingTool::Line && tool != DrawingTool::Rectangle
        && tool != DrawingTool::Circle && tool != DrawingTool::Polygon && tool != DrawingTool::Text)) return false;
    if (m_activeTool != tool) {
        endShapeMove(false); m_selectionMenu->hide();
        cancelDrawing();
        m_panning = false;
        m_activeTool = tool;
        updateInteractionCursor();
        emit activeToolChanged(tool);
        notifyDrawingState();
    }
    return true;
}

/// @brief 現在のツールが図形または文字の作成ツールかを返す。
bool DrawingView::isDrawingTool() const
{
    return m_activeTool == DrawingTool::Line || m_activeTool == DrawingTool::Rectangle
        || m_activeTool == DrawingTool::Circle || m_activeTool == DrawingTool::Polygon || m_activeTool == DrawingTool::Text;
}

/// @brief 現在のツールと作成途中の状態に対応する操作案内を返す。
QString DrawingView::drawingHint() const
{
    if (!m_drawingEnabled) return tr("作業領域で描画ツールを選択してください。");
    if (isDrawingTool() && !m_document->canCreateShape()) return tr("アクティブレイヤーを通常表示にするか、別のレイヤーを選んでください。");
    if (m_activeTool == DrawingTool::Selection)
        return tr("クリックで選択、Shiftで追加・解除。選択図形をドラッグで移動、Delか右クリックで削除、Escで解除します。");
    if (m_activeTool == DrawingTool::Pan) return tr("左ドラッグで画面を移動します。");
    if (m_activeTool == DrawingTool::Text) return tr("文字の左上位置を指定してください。横書きで入力します。");
    if (m_activeTool == DrawingTool::Polygon)
        return m_draftPoints.isEmpty() ? tr("最初の頂点を指定してください。")
            : tr("頂点を追加します。始点か終点のダブルクリックで完了。Delで1点戻り、Escで中止します。");
    if (m_draftPoints.isEmpty())
        return m_activeTool == DrawingTool::Circle ? tr("円の中心を指定してください。") : tr("始点を指定してください。");
    if (m_activeTool == DrawingTool::Circle) return tr("円周上の点を指定してください。Escで中止します。");
    if (m_activeTool == DrawingTool::Rectangle) return tr("対角の頂点を指定してください。Escで中止します。");
    return tr("終点を指定してください。Shiftで角度を固定、Escで中止します。");
}

/// @brief プレビューを再描画し、描画状態と操作案内の変更を通知する。
void DrawingView::notifyDrawingState()
{
    viewport()->update();
    emit drawingStateChanged();
    emit drawingHintChanged(drawingHint());
}

/// @brief 作成途中の頂点とプレビューを破棄する。確定済み図形は残す。
void DrawingView::cancelDrawing()
{
    m_draftPoints.clear(); m_draftHover.reset(); m_drawPressed = false; m_drawShift = false;
    notifyDrawingState();
}

/// @brief 作業領域の編集可否を切り替え、無効化時は未確定の操作を取り消す。
/// @param enabled 描画・選択編集を許可するかどうか。
void DrawingView::setDrawingEnabled(bool enabled)
{
    if (m_drawingEnabled == enabled) return;
    m_drawingEnabled = enabled;
    if (!enabled) { endShapeMove(false); m_selectionMenu->hide(); cancelDrawing(); }
    else notifyDrawingState();
}

/// @brief 2点が画面上の6px以内にあるかを判定する。ズームで判定幅を変えない。
/// @param point 用紙中心基準の実寸座標（mm）。
/// @param vertex 用紙中心基準の実寸座標（mm）。
bool DrawingView::nearDraftVertex(QPointF point, QPointF vertex) const
{
    return QLineF(viewportTransform().map(point), viewportTransform().map(vertex)).length() <= 6;
}

/// @brief 実寸mmの点で図形作成を進め、文字では1点で入力を要求する。不正点は拒否する。
/// @param point 用紙中心基準の実寸座標（mm）。
/// @param constrainLine 直線の終点にShift相当の角度制限を適用するかどうか。
bool DrawingView::submitDrawingPoint(QPointF point, bool constrainLine)
{
    if (!m_drawingEnabled || !m_document->canCreateShape() || m_picking || m_panning || !isDrawingTool()) return false;
    if (!validPoint(point)) {
        emit drawingHintChanged(tr("座標は±10000000mm以内の有限の数値で指定してください。"));
        return false;
    }
    if (m_activeTool == DrawingTool::Text) { emit textCreationRequested(point); return true; }
    if (m_activeTool == DrawingTool::Line && constrainLine && !m_draftPoints.isEmpty())
        point = constrainedLinePoint(m_draftPoints.first(), point);
    if (!validPoint(point)) return false;
    if (m_draftPoints.isEmpty()) {
        m_draftPoints.append(point); m_draftHover = point;
        notifyDrawingState(); return true;
    }
    if (m_activeTool == DrawingTool::Polygon) {
        if (point == m_draftPoints.last() || point == m_draftPoints.first()) {
            emit drawingHintChanged(tr("多角形は始点か終点のダブルクリック、または「多角形を完了」で閉じます。"));
            return false;
        }
        m_draftPoints.append(point); m_draftHover = point;
        notifyDrawingState(); return true;
    }
    return commitShape(point);
}

/// @brief 2点から直線・四角・円を作り、共通モデルへの追加が成功した場合だけ確定する。
/// @param second 用紙中心基準の実寸座標（mm）。
bool DrawingView::commitShape(QPointF second)
{
    // first: 用紙中心基準の実寸座標（mm）。
    const QPointF first = m_draftPoints.first();
    // geometry: 2点から生成した確定候補の形状データ（実寸mm）。
    ShapeGeometry geometry;
    if (m_activeTool == DrawingTool::Line) geometry = QLineF(first, second);
    else if (m_activeTool == DrawingTool::Rectangle) geometry = QRectF(first, second).normalized();
    else if (m_activeTool == DrawingTool::Circle) geometry = CircleGeometry{first, QLineF(first, second).length()};
    else return false;
    if (m_document->addShape(geometry).isNull()) {
        emit drawingHintChanged(tr("大きさが0の図形や、範囲外の図形は作成できません。終点を指定し直してください。"));
        return false;
    }
    // created: 今回確定した図形を作成したツールの種類。
    const DrawingTool created = m_activeTool;
    cancelDrawing(); emit shapeCreated(created);
    return true;
}

/// @brief 現在の多角形を、測定やパンと競合せず有効な面として閉じられるかを返す。
bool DrawingView::canFinishPolygon() const
{
    return m_drawingEnabled && m_document->canCreateShape() && !m_picking && !m_panning && m_activeTool == DrawingTool::Polygon && validPolygon(m_draftPoints);
}

/// @brief 頂点順を保った多角形をモデルへ追加する。不成立なら作成途中の頂点を残す。
bool DrawingView::finishPolygon()
{
    if (!canFinishPolygon()) {
        emit drawingHintChanged(tr("多角形には同一直線上にない3点以上が必要です。"));
        return false;
    }
    if (m_document->addShape(m_draftPoints).isNull()) return false;
    cancelDrawing(); emit shapeCreated(DrawingTool::Polygon);
    return true;
}

/// @brief このビューが管理する表示アイテムの図形UUIDを返す。管理外なら空のUUID。
/// @param item シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
QUuid DrawingView::shapeId(const QGraphicsItem *item) const
{
    if (!m_shapes.contains(const_cast<QGraphicsItem*>(item))) return {};
    return item->data(ShapeIdRole).toUuid();
}

/// @brief モデルの形状・配置を表示へ反映する。新規図形の場合は描画アイテムも作る。
/// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
void DrawingView::syncShape(const QUuid &id)
{
    // data: 検証・追加・表示同期に使う図形データ。
    const auto data = m_document->shape(id);
    if (!data) return;
    // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
    auto *item = m_shapeGraphics.value(id, nullptr);
    // added: モデルから初めて表示アイテムを作る必要があるかどうか。
    const bool added = !item;
    // tool: 現在処理するツールの種類。
    DrawingTool tool = DrawingTool::Line;
    switch (data->kind()) {
    case ShapeKind::Line:
        if (!item) item = scene()->addLine({}, shapePen());
        qgraphicsitem_cast<QGraphicsLineItem*>(item)->setLine(std::get<QLineF>(data->geometryMm));
        break;
    case ShapeKind::Rectangle:
        tool = DrawingTool::Rectangle;
        if (!item) item = scene()->addRect({}, shapePen(), shapeBrush());
        qgraphicsitem_cast<QGraphicsRectItem*>(item)->setRect(std::get<QRectF>(data->geometryMm));
        break;
    case ShapeKind::Circle: {
        tool = DrawingTool::Circle;
        // circle: 対象の円形状またはその描画アイテム。
        const auto circle = std::get<CircleGeometry>(data->geometryMm);
        // radius: 円の外接矩形の対角へ向かう半径ベクトル（実寸mm）。
        const QPointF radius(circle.radiusMm, circle.radiusMm);
        if (!item) {
            item = new OpenShapeItem<QGraphicsEllipseItem>(); scene()->addItem(item);
            qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->setPen(shapePen());
            qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->setBrush(shapeBrush());
        }
        qgraphicsitem_cast<QGraphicsEllipseItem*>(item)->setRect(QRectF(circle.centerMm - radius, circle.centerMm + radius));
        static_cast<OpenShapeItem<QGraphicsEllipseItem>*>(item)->setOpenPath(data->localPath(),!data->isFilled());
        break;
    }
    case ShapeKind::Text:
        tool = DrawingTool::Text;
        if (!item) item = scene()->addPath({});
        qgraphicsitem_cast<QGraphicsPathItem*>(item)->setPath(m_document->shapePath(*data));
        break;
    case ShapeKind::Polygon:
        tool = DrawingTool::Polygon;
        if (!item) {
            item = new OpenShapeItem<QGraphicsPolygonItem>(); scene()->addItem(item);
            qgraphicsitem_cast<QGraphicsPolygonItem*>(item)->setPen(shapePen());
            qgraphicsitem_cast<QGraphicsPolygonItem*>(item)->setBrush(shapeBrush());
        }
        qgraphicsitem_cast<QGraphicsPolygonItem*>(item)->setPolygon(std::get<QPolygonF>(data->geometryMm));
        static_cast<OpenShapeItem<QGraphicsPolygonItem>*>(item)->setOpenPath(data->localPath(),!data->isFilled());
        break;
    }
    // owner: 表示と編集可否を決める所属レイヤー。
    const auto owner = m_document->layer(data->layerId);
    // visibility: 縮尺による一時的な非表示を含む実効状態。
    const auto visibility = m_document->effectiveLayerVisibility(data->layerId);
    // pen: グレイ参照では色だけを変え、画面線幅を維持するペン。
    QPen pen = shapePen();
    // brush: 通常の面表示または参照用の薄いグレイ塗り。
    QBrush brush = shapeBrush();
    if (visibility == LayerVisibility::Gray) { pen.setColor(QColor("#999999")); brush = QColor(160,160,160,35); }
    if (data->kind() == ShapeKind::Text) {
        auto *text = qgraphicsitem_cast<QGraphicsPathItem*>(item); // 字形のみ塗る。行ボックスの背景は描かない。
        text->setPen(Qt::NoPen); text->setBrush(pen.color());
    }
    else if (auto *line = qgraphicsitem_cast<QGraphicsLineItem*>(item)) line->setPen(pen);
    else if (auto *rect = qgraphicsitem_cast<QGraphicsRectItem*>(item)) { rect->setPen(pen); rect->setBrush(brush); }
    else if (auto *ellipse = qgraphicsitem_cast<QGraphicsEllipseItem*>(item)) { ellipse->setPen(pen); ellipse->setBrush(brush); }
    else if (auto *polygon = qgraphicsitem_cast<QGraphicsPolygonItem*>(item)) { polygon->setPen(pen); polygon->setBrush(brush); }
    item->setVisible(owner && visibility != LayerVisibility::Hidden);
    item->setFlag(QGraphicsItem::ItemIsSelectable,m_document->canEditShape(data->id));
    item->setTransform(data->rectangleTransform());
    item->setPos(data->positionMm);
    if (added) {
        item->setData(ShapeIdRole, id); item->setData(ShapeToolRole, QVariant::fromValue(tool));
        m_shapeGraphics.insert(id, item); m_shapes.append(item);
    }
    // Undoで途中の図形を復元した場合も、モデルの描画順へ表示順・ヒット判定順を揃える。
    m_shapes.clear();
    // shape: 文書が保持する元の描画順の図形。
    for (const auto &shape : m_document->shapes()) {
        // graphics: すでに復元済みの表示アイテム。シーンが所有する。
        auto *graphics = m_shapeGraphics.value(shape.id,nullptr);
        if (graphics) { graphics->setZValue(m_shapes.size()); m_shapes.append(graphics); }
    }
    if (m_vertexMarker) m_vertexMarker->setZValue(m_shapes.size()+1);
    updateSceneBounds(); setViewCenter(m_viewCenter); viewport()->update();
}

/// @brief 削除通知の図形IDに対応する表示アイテムを破棄し、選択表示と範囲を更新する。
/// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
void DrawingView::removeShapeItem(const QUuid &id)
{
    // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
    auto *item = m_shapeGraphics.take(id);
    if (!item) return;
    m_shapes.removeOne(item); delete item;
    updateSceneBounds(); setViewCenter(m_viewCenter); viewport()->update();
}

/// @brief 現在選択されている、管理対象の表示アイテムを描画順で返す。
QList<QGraphicsItem*> DrawingView::selectedShapeItems() const
{
    // result: 描画順に集める選択済みアイテムの参照リスト。所有権はシーンに残る。
    QList<QGraphicsItem*> result;
    // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
    for (auto *item : m_shapes) if (m_selectionModel->contains(shapeId(item))) result.append(item);
    return result;
}

/// @brief 選択IDの正本をQt描画アイテムへ反映し、逆方向の再入を抑える。
void DrawingView::syncSelectionDisplay()
{
    if (m_syncingSelection) return;
    // syncing: アイテムの選択フラグ変更によるシーン通知をこのスコープだけ抑制する。
    QScopedValueRollback<bool> syncing(m_syncingSelection, true);
    // item: シーンが所有する表示アイテム。選択モデルに一致する見た目だけを設定する。
    for (auto *item : m_shapes) item->setSelected(m_selectionModel->contains(shapeId(item)));
    viewport()->update();
}

/// @brief 未確定の移動を取り消し、全図形の選択を解除する。
void DrawingView::clearShapeSelection()
{
    endShapeMove(false);
    m_selectionModel->clear();
}

/// @brief 画面座標から手前の図形を探す。細線も6pxの許容幅で判定する。
/// @param pixel 入力・表示位置を比較する画面座標（px）。
QGraphicsItem *DrawingView::shapeAt(QPoint pixel) const
{
    // Hit tolerance is in screen pixels so thin lines remain usable at any scale.
    // stroker: 細い輪郭の周囲へ画面px単位の選択許容幅を与える生成器。
    QPainterPathStroker stroker; stroker.setWidth(12);
    // it: 手前から図形を調べる逆順イテレータ。
    for (auto it = m_shapes.crbegin(); it != m_shapes.crend(); ++it) {
        // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
        auto *item = *it;
        if (!item->isVisible()) continue;
        // data: 検証・追加・表示同期に使う図形データ。
        const auto data = m_document->shape(shapeId(item));
        if (!data || !m_document->canEditShape(data->id)) continue;
        // path: 実寸形状を画面座標へ変換した選択判定用パス（px）。
        if (data->kind() == ShapeKind::Text) {
            // bounds: 空白・改行の隙間も含め、文字全体を選択できる表示上の行ボックス。
            const QRectF bounds=m_document->shapeBounds(*data).translated(item->pos()-data->positionMm);
            if (viewportTransform().mapRect(bounds).adjusted(-3,-3,3,3).contains(pixel)) return item;
            continue;
        }
        const QPainterPath path = viewportTransform().map(m_document->shapePath(*data).translated(item->pos()));
        // filled: 選択判定で線の近傍だけでなく面の内部も対象にするかどうか。
        const bool filled = data->isFilled();
        if ((filled && path.contains(pixel)) || stroker.createStroke(path).contains(pixel)) return item;
    }
    return nullptr;
}

/// @brief ドラッグの一時状態を終了する。確定時だけモデルを更新し、取消時は元の表示へ戻す。
/// @param commit 一時的な移動をモデルへ確定するか、元の位置へ戻すか。
void DrawingView::endShapeMove(bool commit)
{
    // origins: 確定・取消に使う移動前の表示状態のコピー。
    const auto origins = m_moveOrigins;
    // delta: 開始位置から加える移動量（実寸mm）。
    const QPointF delta = m_moveDelta;
    // wasMoving: 処理開始時に移動プレビューを表示していたかどうか。
    const bool wasMoving = m_movingShapes;
    // Clear interaction state before document signals can call back into the view.
    m_moveOrigins.clear(); m_moveDelta = {}; m_selectionPressed = false; m_movingShapes = false;
    // committed: ドラッグの移動量をモデルへ正常に確定できたかどうか。
    bool committed = false;
    if (commit && wasMoving) {
        // ids: 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
        QList<QUuid> ids;
        // origin: 移動前の表示アイテムと配置移動量の組。
        for (const auto &origin : origins) ids.append(shapeId(origin.first));
        committed = m_document->translateShapes(ids, delta);
    }
    // origin: 移動前の表示アイテムと配置移動量の組。
    if (!committed) for (const auto &origin : origins) origin.first->setPos(origin.second);
    if (wasMoving) { updateSceneBounds(); setViewCenter(m_viewCenter); viewport()->update(); }
}

/// @brief 選択ツールが有効な場合に選択図形をモデルから削除する。対象なしや操作禁止時はfalse。
bool DrawingView::deleteSelectedShapes()
{
    if (!m_drawingEnabled || m_picking || m_panning || m_activeTool != DrawingTool::Selection) return false;
    endShapeMove(false);
    // ids: 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    QList<QUuid> ids;
    // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
    for (auto *item : selectedShapeItems()) ids.append(shapeId(item));
    return m_document->removeShapes(ids);
}

/// @brief 選択ツールで右クリックした図形を選び、削除メニューを表示する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_drawingEnabled || m_picking || m_panning || m_activeTool != DrawingTool::Selection) {
        event->accept(); return;
    }
    endShapeMove(false);
    // hit: クリック位置で見つかった最前面の図形アイテム。
    auto *hit = shapeAt(event->pos());
    if (hit && !m_selectionModel->contains(shapeId(hit))) m_selectionModel->selectOnly(shapeId(hit));
    m_selectionMenu->actions().first()->setEnabled(!selectedShapeItems().isEmpty());
    m_selectionMenu->popup(event->globalPos()); event->accept();
}

/// @brief パン・測定・描画などの現在状態に合うカーソルを設定する。
void DrawingView::updateInteractionCursor()
{
    if (m_panning) viewport()->setCursor(Qt::ClosedHandCursor);
    else if (m_picking) viewport()->setCursor(Qt::CrossCursor);
    else if (m_activeTool == DrawingTool::Pan) viewport()->setCursor(Qt::OpenHandCursor);
    else if (m_activeTool == DrawingTool::Selection) viewport()->setCursor(Qt::ArrowCursor);
    else viewport()->setCursor(Qt::CrossCursor);
}

/// @brief 縮尺に応じて用紙範囲と表示変換を更新する。確定図形の実寸は維持する。
void DrawingView::updatePaper()
{
    endShapeMove(false);
    // Only the paper's real-world coverage changes; model items are untouched.
    m_paper->setRect(m_settings->paperRectInModel());
    m_printableArea->setRect(m_settings->printableRectInModel());
    updateSceneBounds();
    updateViewTransform({});
    emit cursorLeft();
}

/// @brief PDFの実寸配置範囲を更新し、用紙・図形と合わせて移動可能な範囲を整える。
/// @param bounds 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
void DrawingView::setContentBounds(QRectF bounds)
{
    m_contentBounds = bounds;
    updateSceneBounds();
    setViewCenter(m_viewCenter);
}

/// @brief 用紙・PDF・確定図形を含むシーン範囲を更新する。カメラ位置の記録更新は抑制する。
void DrawingView::updateSceneBounds()
{
    // updating: スクロール等の再入でカメラ中心が変わらないようにする表示更新ガード。
    QScopedValueRollback<bool> updating(m_updatingView, true);
    // margin: シーン範囲の周囲へ加える余白（実寸mm）。
    const double margin = 10 * m_settings->scaleDenominator();
    // paperBounds: 余白込みの用紙範囲（実寸mm）。
    const QRectF paperBounds = m_paper->rect().adjusted(-margin, -margin, margin, margin);
    // bounds: 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
    QRectF bounds = m_contentBounds.isEmpty() ? paperBounds : paperBounds.united(m_contentBounds);
    // shape: 描画順に処理する確定済み図形データ。
    for (const auto &shape : m_document->shapes())
        bounds = bounds.united(m_document->shapeBounds(shape).adjusted(-1, -1, 1, 1));
    scene()->setSceneRect(bounds);
}

/// @brief 指定中心を保ち、用紙換算と画面ズームを合わせた表示変換を適用する。
/// @param center 用紙中心基準の実寸座標（mm）。
void DrawingView::updateViewTransform(QPointF center)
{
    if (m_updatingView) return;
    // updating: スクロール等の再入でカメラ中心が変わらないようにする表示更新ガード。
    QScopedValueRollback<bool> updating(m_updatingView, true);
    // paper: 用紙の物理寸法（mm）。
    const QSizeF paper = m_settings->paperSizeMm();
    // Scroll bars can appear during zoom. Their appearance must not redefine
    // the base fit scale, otherwise zoom and resize recursively change it.
    // available: スクロールバー表示に依存しない、利用可能な最大画面寸法（px）。
    const QSize available = maximumViewportSize();
    // The minimum also keeps Qt's inverse transform numerically usable at the
    // largest supported denominator and smallest display zoom.
    // pixelsPerPaperMm: 物理的な用紙1mmを画面上の何pxで表示するか。
    const double pixelsPerPaperMm = std::max(0.2, std::min(
        available.width() / (paper.width() + 20),
        available.height() / (paper.height() + 20)));
    // pixelsPerModelMm: 実寸1mmを画面上の何pxで表示するか。
    const double pixelsPerModelMm = pixelsPerPaperMm * m_displayZoom
        / m_settings->scaleDenominator();
    // Invert only the view's Y axis. Geometry in the scene remains in real mm.
    setTransform(QTransform::fromScale(pixelsPerModelMm, -pixelsPerModelMm));
    setViewCenter(center);
}

/// @brief カメラ中心を実寸mmで設定し、パン操作用の表示範囲を確保する。
/// @param center 用紙中心基準の実寸座標（mm）。
void DrawingView::setViewCenter(QPointF center)
{
    // updating: スクロール等の再入でカメラ中心が変わらないようにする表示更新ガード。
    QScopedValueRollback<bool> updating(m_updatingView, true);
    m_viewCenter = center;
    // visible: 現在の画面が覆う実寸幅・高さ（mm）。
    const QSizeF visible(std::max(1, viewport()->width()) / std::abs(transform().m11()),
                         std::max(1, viewport()->height()) / std::abs(transform().m22()));
    // Navigation margins are view-only. Panning and cursor-centered zoom must
    // also work in the gray area, without changing model items or paper bounds.
    // navigation: 現在位置の周囲へパンの余地を確保する実寸mmの範囲。
    const QRectF navigation(center.x() - visible.width() * 1.5,
                            center.y() - visible.height() * 1.5,
                            visible.width() * 3, visible.height() * 3);
    setSceneRect(scene()->sceneRect().united(navigation));
    centerOn(center);
}

/// @brief カーソル位置を基準にズームする。移動途中の図形は確定せず取り消す。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::wheelEvent(QWheelEvent *event)
{
    endShapeMove(false);
    // delta: ホイールの縦方向入力。angleDeltaは1/8度、代替のpixelDeltaはpx。
    const double delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->pixelDelta().y();
    if (delta == 0) { event->ignore(); return; }
    // position: 入力・表示位置を比較する画面座標（px）。
    const QPoint position = event->position().toPoint();
    // fixedPoint: 用紙中心基準の実寸座標（mm）。
    const QPointF fixedPoint = mapToScene(position);
    // zoom: 用紙基準の表示に対する画面ズーム倍率。
    const double zoom = std::clamp(m_displayZoom * std::pow(1.25, delta / 120.0), 0.1, 10.0);
    setDisplayZoom(zoom);
    setViewCenter(m_viewCenter + fixedPoint - mapToScene(position));
    // Qt scroll bars use integer device positions. Correct the final device
    // offset directly rather than accumulating rounded scene-center errors.
    // error: ズーム後に固定点の画面位置へ生じた丸めずれ（px）。
    const QPointF error = viewportTransform().map(fixedPoint) - event->position();
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + qRound(error.x()));
    verticalScrollBar()->setValue(verticalScrollBar()->value() + qRound(error.y()));
    viewport()->update();
    event->accept();
}

/// @brief カーソル座標を通知し、パン・移動・測定・作成途中のプレビューを更新する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        // delta: 直前の押下・移動位置からの画面移動量（px）。
        const QPoint delta = event->pos() - m_lastPanPosition;
        setViewCenter(mapToScene(viewport()->rect().center() - delta));
        m_lastPanPosition = event->pos();
        emit cursorPositionChanged(mapToScene(event->pos()));
        event->accept(); return;
    }
    // point: 用紙中心基準の実寸座標（mm）。
    const QPointF point = mapToScene(event->pos());
    emit cursorPositionChanged(point);
    if (m_selectionPressed) {
        if (!m_movingShapes && (event->pos() - m_selectionPressPixel).manhattanLength() < QApplication::startDragDistance()) {
            event->accept(); return;
        }
        m_movingShapes = true;
        // delta: 開始位置から加える移動量（実寸mm）。
        const QPointF delta = point - m_selectionPressMm;
        // ids: 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
        QList<QUuid> ids;
        // origin: 移動前の表示アイテムと配置移動量の組。
        for (const auto &origin : m_moveOrigins) ids.append(shapeId(origin.first));
        if (m_document->canTranslateShapes(ids, delta)) {
            m_moveDelta = delta;
            // origin: 移動前の表示アイテムと配置移動量の組。
            for (const auto &origin : m_moveOrigins) origin.first->setPos(origin.second + delta);
        }
        else emit drawingHintChanged(tr("移動後の座標は±10000000mm以内にしてください。"));
        viewport()->update(); event->accept(); return;
    }
    if (m_picking) {
        if (event->buttons().testFlag(Qt::LeftButton)
            && (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
            m_dragged = true;
        if (m_pickKind == DistanceKind::Target || m_pdfBounds.contains(point)) m_hover = point;
        else m_hover.reset();
        viewport()->update();
    } else if (m_drawingEnabled && m_document->canCreateShape() && isDrawingTool()) {
        if (m_drawPressed && (event->pos() - m_pressPosition).manhattanLength() >= QApplication::startDragDistance())
            m_dragged = true;
        m_draftHover = validPoint(point) ? std::optional<QPointF>(point) : std::nullopt;
        m_drawShift = event->modifiers().testFlag(Qt::ShiftModifier);
        viewport()->update();
    }
    QGraphicsView::mouseMoveEvent(event);
}

/// @brief 描画や移動を取り消して、PDF補正用の2点測定を開始する。
/// @param kind 図形の種類、または現在距離／新しい距離の測定種別。
/// @param pdfBounds PDFの配置領域（実寸mm）。現在距離の測定点を制限する。
void DrawingView::startDistancePick(DistanceKind kind, QRectF pdfBounds)
{
    endShapeMove(false); m_selectionMenu->hide();
    cancelDrawing();
    clearDistancePick();
    m_pickKind = kind;
    m_pdfBounds = pdfBounds;
    m_picking = true;
    updateInteractionCursor();
    setFocus();
}

/// @brief 距離測定の点・プレビュー・操作状態を解除する。
void DrawingView::clearDistancePick()
{
    m_picking = false;
    m_panning = false;
    m_first.reset(); m_second.reset(); m_hover.reset();
    updateInteractionCursor();
    viewport()->update();
}

/// @brief パン・測定・選択・描画の優先順でマウス押下を受け付ける。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_activeTool == DrawingTool::Pan && !m_picking)) {
        endShapeMove(false);
        m_drawPressed = false;
        m_panning = true; m_panButton = event->button(); m_lastPanPosition = event->pos();
        setFocus(); updateInteractionCursor(); event->accept(); return;
    }
    if (m_picking) {
        m_pressPosition = event->pos(); m_dragged = false;
        event->accept(); return;
    }
    if (m_drawingEnabled && m_activeTool == DrawingTool::Selection && event->button() == Qt::LeftButton) {
        endShapeMove(false); setFocus();
        // hit: クリック位置で見つかった最前面の図形アイテム。
        auto *hit = shapeAt(event->pos());
        // shift: 押下時にShiftが指定されているかどうか。
        const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
        if (!hit) { if (!shift) clearShapeSelection(); }
        else if (shift) m_selectionModel->toggle(shapeId(hit));
        else {
            if (!m_selectionModel->contains(shapeId(hit))) m_selectionModel->selectOnly(shapeId(hit));
            m_selectionPressed = true; m_selectionPressPixel = event->pos();
            m_selectionPressMm = mapToScene(event->pos());
            // item: シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
            for (auto *item : selectedShapeItems()) m_moveOrigins.append({item, item->pos()});
        }
        event->accept(); return;
    }
    if (m_drawingEnabled && m_document->canCreateShape() && isDrawingTool() && event->button() == Qt::LeftButton) {
        m_ignoreDrawingRelease = false;
        m_pressPosition = event->pos(); m_dragged = false; m_drawPressed = true;
        setFocus(); event->accept(); return;
    }
    event->accept();
}

/// @brief パン終了、移動確定、点指定、距離測定を現在の状態に従って処理する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning) {
        if (event->button() == m_panButton) { m_panning = false; updateInteractionCursor(); }
        event->accept(); return;
    }
    if (m_selectionPressed && event->button() == Qt::LeftButton) {
        endShapeMove(true); event->accept(); return;
    }
    if (!m_picking) {
        if (event->button() == Qt::LeftButton && m_ignoreDrawingRelease) {
            m_ignoreDrawingRelease = false; m_drawPressed = false; event->accept(); return;
        }
        if (m_drawingEnabled && m_document->canCreateShape() && isDrawingTool() && event->button() == Qt::LeftButton) {
            // pressed: 対応する描画用押下が先に受け付けられていたかどうか。
            const bool pressed = m_drawPressed; m_drawPressed = false;
            event->accept();
            if (!pressed || m_dragged) return;
            // point: 用紙中心基準の実寸座標（mm）。
            const QPointF point = mapToScene(event->pos());
            if (m_activeTool == DrawingTool::Polygon && !m_draftPoints.isEmpty()
                && (nearDraftVertex(point, m_draftPoints.last()) || nearDraftVertex(point, m_draftPoints.first()))) {
                emit drawingHintChanged(tr("始点か終点をダブルクリックすると多角形を完了します。"));
                return;
            }
            submitDrawingPoint(point, event->modifiers().testFlag(Qt::ShiftModifier));
            return;
        }
        QGraphicsView::mouseReleaseEvent(event); return;
    }
    event->accept();
    if (event->button() != Qt::LeftButton || m_dragged) return;
    // point: 用紙中心基準の実寸座標（mm）。
    const QPointF point = mapToScene(event->pos());
    if (m_pickKind == DistanceKind::Current && !m_pdfBounds.contains(point)) {
        emit pickHintChanged(tr("現在の距離はPDFの内側の2点を指定してください。Escで戻ります。"));
        return;
    }
    if (!m_first) {
        m_first = point;
        emit pickHintChanged(tr("終点をクリックしてください。Escで測定を中止します。"));
    } else {
        // distance: 指定した2点間の実寸距離（mm）。
        const double distance = QLineF(*m_first, point).length();
        if (!std::isfinite(distance) || distance <= 0) {
            emit pickHintChanged(tr("始点とは異なる点を指定してください。"));
            return;
        }
        m_second = point;
        m_picking = false;
        updateInteractionCursor();
        emit distancePicked(m_pickKind, *m_first, *m_second);
    }
    viewport()->update();
}

/// @brief 多角形の始点または終点でのダブルクリックを完了操作として処理する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_picking && m_activeTool == DrawingTool::Selection) {
        if (m_drawingEnabled && event->button() == Qt::LeftButton) {
            auto *item = shapeAt(event->pos()); // 通常表示で編集可能な文字だけを対象とする。
            const auto data = item ? m_document->shape(shapeId(item)) : std::nullopt; // 値として取得する。
            if (data && data->kind() == ShapeKind::Text) {
                endShapeMove(false); m_selectionModel->selectOnly(data->id); emit textEditRequested(data->id);
            }
        }
        event->accept(); return;
    }
    if (!m_picking && m_drawingEnabled && m_document->canCreateShape() && isDrawingTool() && event->button() == Qt::LeftButton) {
        m_drawPressed = false; m_ignoreDrawingRelease = true;
        if (m_activeTool == DrawingTool::Polygon && !m_draftPoints.isEmpty()) {
            // point: 用紙中心基準の実寸座標（mm）。
            const QPointF point = mapToScene(event->pos());
            if (nearDraftVertex(point, m_draftPoints.first()) || nearDraftVertex(point, m_draftPoints.last()))
                finishPolygon();
        }
        event->accept(); return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

/// @brief Escによる取消、Delによる削除・頂点戻し、Shiftによる角度制限を処理する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::keyPressEvent(QKeyEvent *event)
{
    if (m_picking && event->key() == Qt::Key_Escape) {
        clearDistancePick(); emit distancePickCanceled(); event->accept(); return;
    }
    if (m_panning && event->key() == Qt::Key_Escape) {
        m_panning = false; updateInteractionCursor(); event->accept(); return;
    }
    if (m_drawingEnabled && !m_picking && m_activeTool == DrawingTool::Selection) {
        if (event->key() == Qt::Key_Escape) {
            if (m_selectionPressed) endShapeMove(false); else clearShapeSelection();
            event->accept(); return;
        }
        if (event->key() == Qt::Key_Delete) { deleteSelectedShapes(); event->accept(); return; }
    }
    if (m_drawingEnabled && !m_draftPoints.isEmpty()) {
        if (event->key() == Qt::Key_Escape) {
            cancelDrawing(); event->accept(); return;
        }
        if (event->key() == Qt::Key_Delete && m_activeTool == DrawingTool::Polygon) {
            m_draftPoints.removeLast(); notifyDrawingState(); event->accept(); return;
        }
        if (event->key() == Qt::Key_Shift) { m_drawShift = true; viewport()->update(); }
    }
    QGraphicsView::keyPressEvent(event);
}

/// @brief Shiftを離した際の角度制限プレビューを解除する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Shift) { m_drawShift = false; viewport()->update(); }
    QGraphicsView::keyReleaseEvent(event);
}

/// @brief カーソルがビューを離れたことを通知し、ホバーのプレビューを消す。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::leaveEvent(QEvent *event)
{
    emit cursorLeft();
    m_hover.reset();
    m_draftHover.reset();
    viewport()->update();
    QGraphicsView::leaveEvent(event);
}

/// @brief 画面サイズ変更に合わせて、論理的なカメラ中心を保った表示変換へ更新する。
/// @param event Qtから渡された入力または画面更新のイベント。
void DrawingView::resizeEvent(QResizeEvent *event)
{
    // alreadyUpdating: リサイズの開始時点ですでに表示変換を更新していたかどうか。
    const bool alreadyUpdating = m_updatingView;
    {
        // updating: スクロール等の再入でカメラ中心が変わらないようにする表示更新ガード。
        QScopedValueRollback<bool> updating(m_updatingView, true);
        QGraphicsView::resizeEvent(event);
    }
    if (!alreadyUpdating) updateViewTransform(m_viewCenter);
}

/// @brief 原点、距離測定の点と線、作成途中の図形を最前面へ描画する。
/// @param painter 呼び出し元から受け取る描画先。保存・復元で描画状態を保つ。
void DrawingView::drawForeground(QPainter *painter, const QRectF &)
{
    painter->save();
    // pen: 画面上の線や選択マーカーに使う描画ペン。
    QPen pen(QColor("#b52c39"), 1);
    pen.setCosmetic(true);
    painter->setPen(pen);
    // radius: 原点マーカーを画面上の6pxに保つための実寸mm半径。
    const double radius = 6.0 / std::abs(transform().m11());
    painter->drawLine(QPointF(-radius, 0), QPointF(radius, 0));
    painter->drawLine(QPointF(0, -radius), QPointF(0, radius));
    if (m_first) {
        pen.setWidth(2);
        pen.setColor(m_pickKind == DistanceKind::Current ? QColor("#d52d41") : QColor("#11804b"));
        painter->setPen(pen);
        painter->setBrush(pen.color());
        // marker: マーカーを一定の画面サイズに保つための実寸mm半径。
        const double marker = 4.0 / std::abs(transform().m11());
        painter->drawEllipse(*m_first, marker, marker);
        if (m_second) {
            painter->drawLine(*m_first, *m_second);
            painter->drawEllipse(*m_second, marker, marker);
        } else if (m_hover) {
            pen.setStyle(Qt::DashLine); painter->setPen(pen);
            painter->drawLine(*m_first, *m_hover);
        }
    }
    if (m_drawingEnabled && !m_draftPoints.isEmpty()) {
        // draftPen: 作成途中の図形を破線で表示するペン。
        QPen draftPen(QColor("#1768b3"), 1.5, Qt::DashLine); draftPen.setCosmetic(true);
        painter->setPen(draftPen); painter->setBrush(shapeBrush());
        // first: 用紙中心基準の実寸座標（mm）。
        const QPointF first = m_draftPoints.first();
        if (m_activeTool == DrawingTool::Polygon) {
            painter->setBrush(Qt::NoBrush);
            painter->drawPolyline(m_draftPoints);
            if (m_draftHover) painter->drawLine(m_draftPoints.last(), *m_draftHover);
        } else if (m_draftHover) {
            // second: 用紙中心基準の実寸座標（mm）。
            const QPointF second = m_activeTool == DrawingTool::Line && m_drawShift
                ? constrainedLinePoint(first, *m_draftHover) : *m_draftHover;
            if (m_activeTool == DrawingTool::Line) painter->drawLine(first, second);
            else if (m_activeTool == DrawingTool::Rectangle) painter->drawRect(QRectF(first, second).normalized());
            else if (m_activeTool == DrawingTool::Circle) {
                // radius: 中心とカーソルの間から求める円の半径（実寸mm）。
                const double radius = QLineF(first, second).length();
                painter->drawEllipse(first, radius, radius);
            }
        }
        painter->setBrush(QColor("#1768b3"));
        // marker: マーカーを一定の画面サイズに保つための実寸mm半径。
        const double marker = 3.0 / std::abs(transform().m11());
        // point: 用紙中心基準の実寸座標（mm）。
        for (const QPointF point : m_draftPoints) painter->drawEllipse(point, marker, marker);
    }
    painter->restore();
}
