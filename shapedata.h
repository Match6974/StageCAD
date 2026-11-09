/**
 * @file shapedata.h
 * @brief 図形の種類別形状、実寸mmの配置、IDと形状検証を定義する。
 */

#ifndef SHAPEDATA_H
#define SHAPEDATA_H

#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QUuid>
#include <QTransform>
#include <variant>
#include <QString>

// Document IDs identify instances; they are unrelated to feature.csv IDs.
/// @brief 図形データに格納された形状の種類。CSVの機能IDとは別の分類。
enum class ShapeKind { /* 直線の形状・描画。 */ Line, /* 四角形の形状・描画。 */ Rectangle, /* 円・開いた円弧の形状・描画。 */ Circle, /* 頂点列による多角形の形状・描画。 */ Polygon, /* 背景を持たない横書き文字。 */ Text };
/// @brief 円・開いた円弧の中心と半径（実寸mm）、開始角・円弧角（度）を保持する。
struct CircleGeometry {
    // centerMm: 中心の実寸座標（mm）。
    QPointF centerMm;
    // radiusMm: 真円の半径（実寸mm）。
    double radiusMm = 0;
    // startAngleDeg: +Xから反時計回りの開始角度（度）。
    double startAngleDeg = 0;
    // sweepAngleDeg: 0より大きく360以下の円弧角（度）。360は真円。
    double sweepAngleDeg = 360;
};
/// 形状を直線・矩形・円／円弧・頂点列のいずれか1種類として保持する共用型。
/// @brief 文字属性と、GUIスレッドで生成した1emの字形を保持する。フォントサイズと表示倍率を分離する。
struct TextGeometry {
    QString text; // 改行を含む文字列。HTMLではない。
    QString fontFamily; // 試作版の固定フォント名。
    double pointSizePt = 12; // 設定サイズ（pt）。縮尺操作では変更しない。
    QPointF anchorMm; // 行ボックス左上の実寸位置（mm）。
    QPainterPath unitPath; // 1em、Y上向きの字形。背景は含まない。
    QRectF unitBounds; // 空白と改行を含む1emの行ボックス。
};
/// @brief 図形と文字の形状を値として保持する。
using ShapeGeometry = std::variant<QLineF, QRectF, CircleGeometry, QPolygonF, TextGeometry>;

// 形状と配置は実寸mm。縮尺・表示倍率を含めず、頂点順と明示的な開閉状態を保持する。
/// @brief 図形インスタンスの値データ。形状と配置移動量を実寸mmで保持する。
struct ShapeData {
    // id: 図形インスタンスを識別するUUID。CSVの機能IDとは別に扱う。
    QUuid id;
    // geometryMm: 配置前の形状データ（実寸mm）。
    ShapeGeometry geometryMm;
    // positionMm: 形状全体に加える配置移動量（実寸mm）。
    QPointF positionMm;
    // rectangleAngleDeg: 四角形のローカル左下を基準とする反時計回りの回転（度）。
    double rectangleAngleDeg = 0;
    // polygonClosed: 頂点列の終点と始点を結ぶかどうか。既存図形は閉じた状態。
    bool polygonClosed = true;
    // layerId: 所属レイヤーのUUID。文書が所属の存在を検証し、単体形状の検証では扱わない。
    QUuid layerId;
    /// @brief 四角形の左下周りの回転変換を返す。配置移動量は含めない。
    QTransform rectangleTransform() const;
    /// @brief 指定基準点を実寸座標で返す。四角形は回転後、多角形は外接矩形を使う。
    /// @param fraction 左下(0,0)から右上(1,1)の相対位置。
    QPointF referencePoint(QPointF fraction) const;
    /// @brief 面を持つ図形かを返す。開いた多角形と円弧は線として扱う。
    bool isFilled() const;

    /// @brief 形状データの型から図形の種類を返す。
    ShapeKind kind() const;
    /// @brief 配置移動量を加える前の形状を、描画・選択判定用のパスへ変換する。
    /// @param textModelScale 文字の紙面寸法を実寸へ換算する倍率。図形では使用しない。
    QPainterPath localPath(double textModelScale = 1) const;
    /// @brief 配置移動量を含めた実寸mmの外接矩形を返す。
    /// @param textModelScale 文字の紙面寸法から実寸への倍率。
    QRectF boundsMm(double textModelScale = 1) const;
    /// @brief ID・形状・配置が有効かを確認する。大きさ0や範囲外の形状は拒否する。
    /// @param textModelScale 文字の表示範囲を実寸mmで検証する倍率。
    bool isValid(double textModelScale = 1) const;
    /// @brief 座標の各成分が有限で、実寸mmの許容範囲内にあるかを返す。
    /// @param point 用紙中心基準の実寸座標（mm）。
    static bool validPoint(QPointF point);
    /// @brief 全頂点が有効で、同一直線上にない3点以上を含むかを確認する。
    /// @param points 指定順に並ぶ多角形の頂点列（実寸mm）。
    static bool validPolygon(const QPolygonF &points);
    // CoordinateLimitMm: 図形の実寸座標の絶対値上限（mm）。
    static constexpr double CoordinateLimitMm = 10000000;
};

#endif
