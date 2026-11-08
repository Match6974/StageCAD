/**
 * @file drawingview.h
 * @brief CAD座標を表示し、描画・選択・移動・削除・PDF距離測定の入力を処理する。
 */

#ifndef DRAWINGVIEW_H
#define DRAWINGVIEW_H

#include <QGraphicsView>
#include <optional>
#include "drawingtool.h"
#include <QPolygonF>
#include <QHash>
#include <QUuid>

class DrawingSettings;
class DrawingDocument;
class QMenu;
class QGraphicsRectItem;

// Scene positions are CAD mm, never pixels or paper mm.
/// @brief 図形モデルの表示と一時的な入力状態を管理する。座標は右が+X、上が+Yの実寸mm。
class DrawingView : public QGraphicsView
{
    Q_OBJECT
public:
    /// @brief 図形モデル・シーン・用紙を作り、座標表示と入力処理を接続する。
    /// @param settings 作業領域の縮尺・用紙設定。参照先は呼び出し側が管理する。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit DrawingView(DrawingSettings *settings, QWidget *parent = nullptr);
    /// @brief 用紙表示の基準倍率に対する画面ズーム倍率を返す。
    double displayZoom() const { return m_displayZoom; }
    /// @brief 有効なズーム倍率を設定する。実寸座標を保ち、不正倍率ではfalseを返す。
    /// @param zoom 用紙基準の表示に対する画面ズーム倍率。
    bool setDisplayZoom(double zoom);
    /// @brief 用紙中心と基準ズームへ戻す。図形やPDFの実寸は変更しない。
    void resetView();
    /// @brief ビューで現在有効なツールを返す。
    DrawingTool activeTool() const { return m_activeTool; }
    /// @brief 対応済みのツールへ切り替える。測定中は拒否し、未確定操作は取り消す。
    /// @param tool 現在処理するツールの種類。
    bool setActiveTool(DrawingTool tool);
    /// @brief 実寸mmの指定点を作成途中へ追加し、必要な点数で図形を確定する。不正点は拒否する。
    /// @param pointMm 指定点の実寸座標（mm）。
    /// @param constrainLine 直線の終点にShift相当の角度制限を適用するかどうか。
    bool submitDrawingPoint(QPointF pointMm, bool constrainLine = false);
    /// @brief 頂点順を保った多角形をモデルへ追加する。不成立なら作成途中の頂点を残す。
    bool finishPolygon();
    /// @brief 作成途中の頂点とプレビューを破棄する。確定済み図形は残す。
    void cancelDrawing();
    /// @brief 作業領域の編集可否を切り替え、無効化時は未確定の操作を取り消す。
    /// @param enabled 描画・選択編集を許可するかどうか。
    void setDrawingEnabled(bool enabled);
    /// @brief 作業領域の描画・選択編集が許可されているかを返す。
    bool drawingEnabled() const { return m_drawingEnabled; }
    /// @brief 作成途中の図形で指定済みの点数を返す。
    int draftPointCount() const { return m_draftPoints.size(); }
    /// @brief 現在の多角形を、測定やパンと競合せず有効な面として閉じられるかを返す。
    bool canFinishPolygon() const;
    /// @brief 現在のツールと作成途中の状態に対応する操作案内を返す。
    QString drawingHint() const;
    /// @brief 描画順の表示アイテムを参照する。形状変更は共通モデルを通して行う。
    const QList<QGraphicsItem*> &shapeItems() const { return m_shapes; }
    /// @brief 現在選択されている、管理対象の表示アイテムを描画順で返す。
    QList<QGraphicsItem*> selectedShapeItems() const;
    /// @brief 未確定の移動を取り消し、全図形の選択を解除する。
    void clearShapeSelection();
    /// @brief 選択ツールが有効な場合に選択図形をモデルから削除する。対象なしや操作禁止時はfalse。
    bool deleteSelectedShapes();
    /// @brief このビューが所有する図形モデルを返す。確定データの変更窓口として使う。
    DrawingDocument *document() const { return m_document; }
    /// @brief このビューが管理する表示アイテムの図形UUIDを返す。管理外なら空のUUID。
    /// @param item シーンが所有する図形の表示アイテムへの参照。正本データはUUIDでモデルから取得する。
    QUuid shapeId(const QGraphicsItem *item) const;
    // ShapeIdRole: 描画アイテムに図形UUIDを格納するQtデータロール。
    static constexpr int ShapeIdRole = 0x5344;
    // ShapeToolRole: 描画アイテムに作成ツールの種類を格納するQtデータロール。
    static constexpr int ShapeToolRole = 0x5343;
    /// @brief PDF補正で測定する現在距離と新しい距離の区別。
    enum class DistanceKind { /* PDF上の現在距離を測定する種別。 */ Current, /* 補正後に合わせる新しい距離を測定する種別。 */ Target };
    Q_ENUM(DistanceKind)
    /// @brief 描画や移動を取り消して、PDF補正用の2点測定を開始する。
    /// @param kind 図形の種類、または現在距離／新しい距離の測定種別。
    /// @param pdfBounds PDFの配置領域（実寸mm）。現在距離の測定点を制限する。
    void startDistancePick(DistanceKind kind, QRectF pdfBounds);
    /// @brief 距離測定の点・プレビュー・操作状態を解除する。
    void clearDistancePick();
    /// @brief PDF補正用の2点測定を受け付けているかを返す。
    bool isPickingDistance() const { return m_picking; }
    /// @brief PDFの実寸配置範囲を更新し、用紙・図形と合わせて移動可能な範囲を整える。
    /// @param bounds 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
    void setContentBounds(QRectF bounds);

signals:
    /// @brief カーソル位置を実寸mmで通知する。
    /// @param modelMm 用紙中心基準の実寸座標（mm）。
    void cursorPositionChanged(QPointF modelMm);
    /// @brief カーソルが作業領域から離れたことを通知する。
    void cursorLeft();
    /// @brief 2点の距離測定が完了した種類と実寸座標を通知する。
    /// @param kind 図形の種類、または現在距離／新しい距離の測定種別。
    /// @param firstMm 指定した1点目の実寸座標（mm）。
    /// @param secondMm 指定した2点目の実寸座標（mm）。
    void distancePicked(DrawingView::DistanceKind kind, QPointF firstMm, QPointF secondMm);
    /// @brief 距離測定が取り消されたことを通知する。
    void distancePickCanceled();
    /// @brief 距離測定に対応する操作案内を通知する。
    /// @param text 表示または検証に使用する文字列。
    void pickHintChanged(const QString &text);
    /// @brief 有効なツールの切り替えを通知する。
    /// @param tool 現在処理するツールの種類。
    void activeToolChanged(DrawingTool tool);
    /// @brief 作成途中の点数や編集可否など、描画操作状態の変化を通知する。
    void drawingStateChanged();
    /// @brief 描画・選択操作の案内文字列を通知する。
    /// @param text 表示または検証に使用する文字列。
    void drawingHintChanged(const QString &text);
    /// @brief 確定図形が追加されたツールの種類を通知する。
    /// @param tool 現在処理するツールの種類。
    void shapeCreated(DrawingTool tool);
    /// @brief 図形の選択状態が更新されたことを通知する。
    void shapeSelectionChanged();

protected:
    /// @brief カーソル座標を通知し、パン・移動・測定・作成途中のプレビューを更新する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void mouseMoveEvent(QMouseEvent *event) override;
    /// @brief パン・測定・選択・描画の優先順でマウス押下を受け付ける。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void mousePressEvent(QMouseEvent *event) override;
    /// @brief パン終了、移動確定、点指定、距離測定を現在の状態に従って処理する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void mouseReleaseEvent(QMouseEvent *event) override;
    /// @brief Escによる取消、Delによる削除・頂点戻し、Shiftによる角度制限を処理する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void keyPressEvent(QKeyEvent *event) override;
    /// @brief カーソル位置を基準にズームする。移動途中の図形は確定せず取り消す。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void wheelEvent(QWheelEvent *event) override;
    /// @brief 選択ツールで右クリックした図形を選び、削除メニューを表示する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void contextMenuEvent(QContextMenuEvent *event) override;
    /// @brief 多角形の始点または終点でのダブルクリックを完了操作として処理する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /// @brief Shiftを離した際の角度制限プレビューを解除する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void keyReleaseEvent(QKeyEvent *event) override;
    /// @brief カーソルがビューを離れたことを通知し、ホバーのプレビューを消す。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void leaveEvent(QEvent *event) override;
    /// @brief 画面サイズ変更に合わせて、論理的なカメラ中心を保った表示変換へ更新する。
    /// @param event Qtから渡された入力または画面更新のイベント。
    void resizeEvent(QResizeEvent *event) override;
    /// @brief 原点、距離測定の点と線、作成途中の図形を最前面へ描画する。
    /// @param painter 呼び出し元から受け取る描画先。保存・復元で描画状態を保つ。
    /// @param rect 処理対象の矩形形状またはその描画アイテム。
    void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
    /// @brief 縮尺に応じて用紙範囲と表示変換を更新する。確定図形の実寸は維持する。
    void updatePaper();
    /// @brief 指定中心を保ち、用紙換算と画面ズームを合わせた表示変換を適用する。
    /// @param centerMm 中心の実寸座標（mm）。
    void updateViewTransform(QPointF centerMm);
    /// @brief カメラ中心を実寸mmで設定し、パン操作用の表示範囲を確保する。
    /// @param centerMm 中心の実寸座標（mm）。
    void setViewCenter(QPointF centerMm);
    /// @brief パン・測定・描画などの現在状態に合うカーソルを設定する。
    void updateInteractionCursor();
    /// @brief 用紙・PDF・確定図形を含むシーン範囲を更新する。カメラ位置の記録更新は抑制する。
    void updateSceneBounds();
    /// @brief プレビューを再描画し、描画状態と操作案内の変更を通知する。
    void notifyDrawingState();
    /// @brief 現在のツールが直線・四角・円・多角形の描画ツールかを返す。
    bool isDrawingTool() const;
    /// @brief 2点から直線・四角・円を作り、共通モデルへの追加が成功した場合だけ確定する。
    /// @param secondMm 指定した2点目の実寸座標（mm）。
    bool commitShape(QPointF secondMm);
    /// @brief 2点が画面上の6px以内にあるかを判定する。ズームで判定幅を変えない。
    /// @param pointMm 指定点の実寸座標（mm）。
    /// @param vertexMm 比較対象の頂点の実寸座標（mm）。
    bool nearDraftVertex(QPointF pointMm, QPointF vertexMm) const;

    /// @brief 画面座標から手前の図形を探す。細線も6pxの許容幅で判定する。
    /// @param viewportPoint 入力・表示位置を比較する画面座標（px）。
    QGraphicsItem *shapeAt(QPoint viewportPoint) const;
    /// @brief 選択表示の再描画と選択状態・操作案内の変更を通知する。
    void notifySelection();
    /// @brief ドラッグの一時状態を終了する。確定時だけモデルを更新し、取消時は元の表示へ戻す。
    /// @param commit 一時的な移動をモデルへ確定するか、元の位置へ戻すか。
    void endShapeMove(bool commit);
    /// @brief モデルの形状・配置を表示へ反映する。新規図形の場合は描画アイテムも作る。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void syncShape(const QUuid &id);
    /// @brief 削除通知の図形IDに対応する表示アイテムを破棄し、選択表示と範囲を更新する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void removeShapeItem(const QUuid &id);

    // m_settings: ビューが参照する作業領域設定。所有権は呼び出し側のQObjectにある。
    DrawingSettings *m_settings;
    // m_paper: シーンが所有する用紙背景の矩形アイテム。
    QGraphicsRectItem *m_paper;
    // m_printableArea: シーンが所有する印刷可能範囲の矩形アイテム。
    QGraphicsRectItem *m_printableArea;
    // m_displayZoom: 用紙表示の基準倍率に対する画面ズーム倍率。
    double m_displayZoom = 1.0;
    // m_picking: PDF距離測定の点指定を受け付けている状態。
    bool m_picking = false;
    // m_dragged: 押下からドラッグ閾値以上動いた状態。クリックによる点指定と区別する。
    bool m_dragged = false;
    // m_pickKind: 現在距離と新しい距離のどちらを測定しているか。
    DistanceKind m_pickKind = DistanceKind::Current;
    // m_pdfBounds: 現在距離の測定点を制限するPDFの実寸mm範囲。
    QRectF m_pdfBounds;
    // m_contentBounds: 表示中PDFの実寸mm範囲。用紙・図形と合わせた表示範囲に使う。
    QRectF m_contentBounds;
    // m_pressPosition: 入力・表示位置を比較する画面座標（px）。
    QPoint m_pressPosition;
    // m_first: 距離測定で最初に指定した点（実寸mm）。
    std::optional<QPointF> m_first;
    // m_second: 距離測定で確定した2点目（実寸mm）。
    std::optional<QPointF> m_second;
    // m_hover: 距離測定のプレビューに使うカーソル位置（実寸mm）。
    std::optional<QPointF> m_hover;
    // m_activeTool: ビューで有効なツール。初期状態は選択ツール。
    DrawingTool m_activeTool = DrawingTool::Selection;
    // m_panning: 画面のドラッグ移動を行っている状態。
    bool m_panning = false;
    // m_updatingView: 表示変換中にスクロール通知でカメラ中心を上書きしないためのガード。
    bool m_updatingView = false;
    // m_panButton: 現在のパン操作を開始したマウスボタン。
    Qt::MouseButton m_panButton = Qt::NoButton;
    // m_lastPanPosition: 入力・表示位置を比較する画面座標（px）。
    QPoint m_lastPanPosition;
    // m_viewCenter: ビュー中央が指す論理的なカメラ位置（実寸mm）。
    QPointF m_viewCenter;
    // m_drawingEnabled: 作業領域の描画・選択編集を許可するフラグ。
    bool m_drawingEnabled = true;
    // m_drawPressed: 描画用の左ボタン押下を受け付け、離すのを待っている状態。
    bool m_drawPressed = false;
    // m_ignoreDrawingRelease: ダブルクリック完了後の解放イベントで余分な頂点を作らないためのフラグ。
    bool m_ignoreDrawingRelease = false;
    // m_drawShift: 描画プレビューにShift角度制限を適用する状態。
    bool m_drawShift = false;
    // m_draftPoints: 作成途中の図形に指定した点列（実寸mm）。
    QPolygonF m_draftPoints;
    // m_draftHover: 作成途中のプレビュー用カーソル位置（実寸mm）。
    std::optional<QPointF> m_draftHover;
    // m_document: ビューがQObjectの子として所有する、確定済み図形のモデル。
    DrawingDocument *m_document;
    // m_shapeGraphics: 図形UUIDから、シーンが所有する表示アイテムを引く対応表。
    QHash<QUuid, QGraphicsItem*> m_shapeGraphics;
    // m_shapes: シーンが所有する描画アイテムへの参照を表示順に保持する。正本の形状はモデルにある。
    QList<QGraphicsItem*> m_shapes; // Display order, not the authoritative geometry.
    // m_moveDelta: ドラッグ中で最後に有効だった移動量（実寸mm）。確定するまでモデルへ反映しない。
    QPointF m_moveDelta;
    // m_selectionMenu: ビューが所有する、図形削除用の右クリックメニュー。
    QMenu *m_selectionMenu;
    // m_selectionPressed: 選択図形の左ボタン押下後、移動確定または取消を待っている状態。
    bool m_selectionPressed = false;
    // m_movingShapes: ドラッグ閾値を超え、選択図形の移動プレビューを表示している状態。
    bool m_movingShapes = false;
    // m_selectionPressPixel: 入力・表示位置を比較する画面座標（px）。
    QPoint m_selectionPressPixel;
    // m_selectionPressMm: 図形移動量の基準となる押下位置（実寸mm）。
    QPointF m_selectionPressMm;
    // m_moveOrigins: ドラッグ前の表示アイテムと配置移動量。取消時の表示復元に使う。
    QList<QPair<QGraphicsItem*, QPointF>> m_moveOrigins;
};

#endif
