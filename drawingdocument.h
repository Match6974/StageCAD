/**
 * @file drawingdocument.h
 * @brief 確定済み図形のID・描画順と、追加・形状更新・移動・削除の変更通知を管理する。
 */

#ifndef DRAWINGDOCUMENT_H
#define DRAWINGDOCUMENT_H

#include "shapedata.h"
#include "drawinglayer.h"
#include <QObject>
#include <QHash>
#include <QList>
#include <optional>
class QUndoStack;
class DocumentEditCommand;
class LayerEditCommand;

// Owns committed CAD data without depending on a scene or widget. DrawingView
// owns only its rendering items and temporary interaction previews.
/// @brief 確定済み図形を所有するモデル。シーンやウィジェットを持たず、変更を通知する。
class DrawingDocument : public QObject {
    Q_OBJECT
public:
    /// @brief 親QObjectを設定して、空の図形モデルを作る。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit DrawingDocument(QObject *parent = nullptr);
    /// @brief 保持する履歴コマンドを文書の図形より先に解放する。
    ~DrawingDocument() override;
    /// @brief 描画順に並んだ確定済み図形を読み取り専用で参照する。
    const QList<ShapeData> &shapes() const { return m_shapes; }
    /// @brief IDに対応する図形のコピーを返す。存在しない場合は空のoptionalを返す。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    std::optional<ShapeData> shape(const QUuid &id) const;
    /// @brief 有効な形状へ新しいUUIDを付けて追加する。不正値や再入時は空のUUIDを返す。
    /// @param geometryMm 配置前の形状データ。図形座標は実寸mm、文字サイズはpt。
    QUuid addShape(ShapeGeometry geometryMm);
    /// @brief 対象IDと移動後の形状を全件検証する。モデルは変更しない。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    /// @param deltaMm 図形全体へ加える移動量（実寸mm）。
    bool canTranslateShapes(const QList<QUuid> &ids, QPointF deltaMm) const;
    /// @brief 全件を検証して移動量を一括反映する。失敗時は全図形を変更しない。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    /// @param deltaMm 図形全体へ加える移動量（実寸mm）。
    bool translateShapes(const QList<QUuid> &ids, QPointF deltaMm);
    /// @brief 存在を全件確認して図形を削除する。重複IDは1回だけ処理する。
    /// @param ids 操作対象の図形UUID一覧。重複は一括操作でまとめて扱う。
    bool removeShapes(const QList<QUuid> &ids);
    /// @brief 直線の両端を実寸座標で更新する。ID・描画順は保ち、不正値では変更しない。
    /// 同じ実寸形状なら通知しない。更新時は形状を実寸座標へ統合し、配置移動量を0にする。
    /// @param id 更新対象の直線UUID。他の種類や存在しないIDは拒否する。
    /// @param lineMm 始点・終点を用紙中心基準の実寸mmで表した線分。
    bool setLineEndpoints(const QUuid &id, const QLineF &lineMm);
    /// @brief 四角形を実寸の矩形へ更新する。固定位置の計算は呼び出し側が決める。
    /// ID・描画順を保持し、不正形状は拒否する。更新時の配置移動量は0へ統合する。
    /// @param id 更新対象の四角形UUID。他の種類は拒否する。
    /// @param boundsMm 配置移動量を反映済みの実寸矩形（mm）。幅・高さは正の値が必要。
    bool setRectangleBounds(const QUuid &id, const QRectF &boundsMm);
    /// @brief 円の中心を保って半径だけを更新する。真円とUUIDを維持する。
    /// @param id 更新対象の円UUID。他の種類や存在しないIDは拒否する。
    /// @param radiusMm 新しい半径（実寸mm）。有限の正値で、外接範囲も有効な値に限る。
    bool setCircleRadius(const QUuid &id, double radiusMm);

    /// @brief 同じUUID・種類の形状を検証後に一括更新する。不正値・再入は変更せずfalseを返す。
    /// @param candidate 更新する実寸形状と属性のコピー。UUIDと描画順は維持する。
    bool updateShape(const ShapeData &candidate);

    /// @brief 描画順に並ぶデザインレイヤーを読み取り専用で返す。
    const QList<DrawingLayer> &layers() const { return m_layers; }
    /// @brief UUIDに対応するレイヤーを返す。存在しなければ空を返す。
    /// @param id レイヤーUUID。
    std::optional<DrawingLayer> layer(const QUuid &id) const;
    /// @brief 新しい図形を配置するアクティブレイヤーUUIDを返す。
    QUuid activeLayerId() const { return m_activeLayerId; }
    /// @brief 名前が重複しないレイヤーを作成する。空の名前ではレイヤーNを自動採番する。
    /// @param name 作成する名前。明示名が不正なら空UUIDを返す。
    QUuid addLayer(const QString &name = {});
    /// @brief 名前を検証して変更する。無変更では履歴を追加せず、不正値ではfalseを返す。
    /// @param id 対象レイヤーのUUID。
    /// @param name 新しい名前。空白のみ・制御文字・重複は不可。
    bool renameLayer(const QUuid &id,const QString &name);
    /// @brief アクティブを切り替え、対象を通常表示に戻す。表示操作なので履歴には含めない。
    /// @param id 既存レイヤーUUID。
    bool setActiveLayer(const QUuid &id);
    /// @brief 表示状態を変更する。非表示・グレイの図形は編集できない。
    /// @param id 対象レイヤーUUID。
    /// @param visibility 通常・非表示・グレイのいずれか。
    bool setLayerVisibility(const QUuid &id,LayerVisibility visibility);
    /// @brief 対象レイヤーの縮尺と文字の見た目を変更する。図形寸法・設定pt・履歴は維持する。
    /// @param id 対象レイヤーUUID。
    /// @param denominator 縮尺1:Nの分母。表示設定と共通の有限な許容範囲に限る。
    /// @param scaleText trueなら紙面上の文字も縮尺比で伸縮する。設定ptは維持する。
    bool setLayerScale(const QUuid &id,double denominator,bool scaleText = false);
    /// @brief レイヤー縮尺と文字の表示補正を含むローカル描画パスを返す。
    /// @param data 文書内の図形データ。
    QPainterPath shapePath(const ShapeData &data) const;
    /// @brief 文字の表示補正と配置を含む実寸mmの外接範囲を返す。
    /// @param data 文書内の図形データ。
    QRectF shapeBounds(const ShapeData &data) const;
    /// @brief レイヤーの表示換算を含めて実寸座標範囲を検証する。
    /// @param data 候補の図形データ。
    bool validShape(const ShapeData &data) const;
    /// @brief 手動表示状態と、アクティブレイヤーとの縮尺一致から実際の表示状態を返す。
    /// @param id 既存レイヤーUUID。存在しない場合は非表示を返す。
    LayerVisibility effectiveLayerVisibility(const QUuid &id) const;
    /// @brief アクティブレイヤーに新しい図形を作成できるかを返す。
    bool canCreateShape() const;
    /// @brief 図形が存在し、通常表示レイヤーに所属しているかを返す。
    /// @param id 確認する図形UUID。
    bool canEditShape(const QUuid &id) const;
    /// @brief 戻せる確定済み操作があるかどうかを返す。
    bool canUndo() const;
    /// @brief やり直せる確定済み操作があるかどうかを返す。
    bool canRedo() const;
    /// @brief 直前の確定操作を戻す。履歴なし・再入時はfalseで変更しない。
    bool undo();
    /// @brief 戻した操作をやり直す。履歴なし・再入時はfalseで変更しない。
    bool redo();

signals:
    /// @brief レイヤーの追加・属性・アクティブ状態の変更後に通知する。
    void layersChanged();
    /// @brief 履歴の可否が変わったときに通知する。メニューと表示バーは同じ履歴を参照する。
    void historyChanged();
    /// @brief Undo／Redoの直前に未確定の描画・入力を破棄するために通知する。
    void historyAboutToRestore();
    /// @brief 有効な変更の直前を通知し、ビューの未確定操作を取り消せるようにする。
    void aboutToChange();
    /// @brief 追加された図形IDを通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeAdded(const QUuid &id);
    /// @brief 形状・配置を反映済みの図形IDを通知する。一括処理では全データの更新後に通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeChanged(const QUuid &id);
    /// @brief 削除された図形IDを通知する。
    /// @param id 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    void shapeRemoved(const QUuid &id);

private:
    friend class DocumentEditCommand;
    friend class LayerEditCommand;
    /// @brief 指定名が一意で空白・制御文字だけでないことを検証する。
    /// @param name 前後の空白を除去した候補名。
    /// @param except 名前変更時に重複判定から除外するUUID。
    bool validLayerName(const QString &name,const QUuid &except = {}) const;
    // m_layers: 文書が所有するデザインレイヤー一覧。初期状態でも1件存在する。
    QList<DrawingLayer> m_layers;
    // m_activeLayerId: 新規図形の所属先。必ず既存レイヤーを指す。
    QUuid m_activeLayerId;
    /// @brief 有効な候補リストの差分を1操作として履歴へ追加し、表示へ通知する。
    /// @param next 描画順に並ぶ検証済みの確定候補。文書のUUIDは一意。
    /// @param text 操作を識別する日本語名。
    bool commit(const QList<ShapeData> &next, const QString &text);
    // m_history: 文書が所有する差分コマンドの履歴。外部には直接公開しない。
    QUndoStack *m_history;

    // m_shapes: モデルが値として所有する確定図形。描画・重なり順を保持する。
    // m_textVisualScales: 設定ptと独立した表示倍率。Undoで属性を戻しても縮尺設定は維持する。
    QHash<QUuid,double> m_textVisualScales;
    QList<ShapeData> m_shapes; // In drawing/stacking order.
    // m_mutating: モデルの変更通知中に入れ子の変更を受け付けないための再入防止フラグ。
    bool m_mutating = false;
};

#endif
