/**
 * @file shapedata.cpp
 * @brief 図形の種類別形状、実寸mmの配置、IDと形状検証を定義する。
 */

#include "shapedata.h"
#include <cmath>
#include <limits>

/// @brief 形状データの型から図形の種類を返す。
ShapeKind ShapeData::kind() const
{
    if (std::holds_alternative<QLineF>(geometryMm)) return ShapeKind::Line;
    if (std::holds_alternative<QRectF>(geometryMm)) return ShapeKind::Rectangle;
    if (std::holds_alternative<CircleGeometry>(geometryMm)) return ShapeKind::Circle;
    return ShapeKind::Polygon;
}

/// @brief 配置移動量を加える前の形状を、描画・選択判定用のパスへ変換する。
QPainterPath ShapeData::localPath() const
{
    // path: 配置移動量を加える前の形状パス（実寸mm）。
    QPainterPath path;
    // line: 対象の直線形状またはその描画アイテム。
    if (const auto *line = std::get_if<QLineF>(&geometryMm)) {
        path.moveTo(line->p1()); path.lineTo(line->p2());
    // rect: 処理対象の矩形形状またはその描画アイテム。
    } else if (const auto *rect = std::get_if<QRectF>(&geometryMm)) path.addRect(*rect);
    // circle: 対象の円形状またはその描画アイテム。
    else if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm))
        path.addEllipse(circle->centerMm, circle->radiusMm, circle->radiusMm);
    // polygon: 頂点順を保持した多角形の形状または描画アイテム。
    else if (const auto *polygon = std::get_if<QPolygonF>(&geometryMm)) {
        path.addPolygon(*polygon); path.closeSubpath();
    }
    return path;
}

/// @brief 配置移動量を含めた実寸mmの外接矩形を返す。
QRectF ShapeData::boundsMm() const { return localPath().boundingRect().translated(positionMm); }

/// @brief 座標の各成分が有限で、実寸mmの許容範囲内にあるかを返す。
/// @param point 用紙中心基準の実寸座標（mm）。
bool ShapeData::validPoint(QPointF point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y())
        && std::abs(point.x()) <= CoordinateLimitMm && std::abs(point.y()) <= CoordinateLimitMm;
}

/// @brief 全頂点が有効で、同一直線上にない3点以上を含むかを確認する。
/// @param points 指定順に並ぶ多角形の頂点列（実寸mm）。
bool ShapeData::validPolygon(const QPolygonF &points)
{
    if (points.size() < 3) return false;
    // point: 用紙中心基準の実寸座標（mm）。
    for (QPointF point : points) if (!validPoint(point)) return false;
    // first: 用紙中心基準の実寸座標（mm）。
    const QPointF first = points[0];
    // i: 頂点やボタンを走査するインデックス。
    for (qsizetype i = 1; i + 1 < points.size(); ++i) {
        // a: 最初の頂点から比較する頂点へ向かう第1のベクトル（実寸mm）。
        // b: 最初の頂点から次の頂点へ向かう第2のベクトル（実寸mm）。
        const QPointF a = points[i] - first, b = points[i + 1] - first;
        // lhs: 外積の第1項。桁落ちを抑えるためlong doubleで計算する。
        const long double lhs = static_cast<long double>(a.x()) * b.y();
        // rhs: 外積の第2項。第1項と比較して非共線性を調べる。
        const long double rhs = static_cast<long double>(a.y()) * b.x();
        // tolerance: 外積の丸め誤差を吸収する相対的な許容差。
        const long double tolerance = 64 * std::numeric_limits<double>::epsilon()
            * (std::abs(lhs) + std::abs(rhs));
        if (std::abs(lhs - rhs) > tolerance) return true;
    }
    return false;
}

/// @brief ID・形状・配置が有効かを確認する。大きさ0や範囲外の形状は拒否する。
bool ShapeData::isValid() const
{
    if (id.isNull() || !std::isfinite(positionMm.x()) || !std::isfinite(positionMm.y())) return false;
    // line: 対象の直線形状またはその描画アイテム。
    if (const auto *line = std::get_if<QLineF>(&geometryMm)) {
        if (!validPoint(line->p1()) || !validPoint(line->p2()) || line->length() <= 0) return false;
    // rect: 処理対象の矩形形状またはその描画アイテム。
    } else if (const auto *rect = std::get_if<QRectF>(&geometryMm)) {
        if (!validPoint(rect->topLeft()) || !validPoint(rect->bottomRight())
            || !(rect->width() > 0) || !(rect->height() > 0)) return false;
    // circle: 対象の円形状またはその描画アイテム。
    } else if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm)) {
        if (!validPoint(circle->centerMm) || !std::isfinite(circle->radiusMm) || circle->radiusMm <= 0) return false;
        // extent: 円の外接矩形を得るための半径ベクトル（実寸mm）。
        const QPointF extent(circle->radiusMm, circle->radiusMm);
        if (!validPoint(circle->centerMm - extent) || !validPoint(circle->centerMm + extent)) return false;
    } else if (!validPolygon(std::get<QPolygonF>(geometryMm))) return false;
    // bounds: 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
    const QRectF bounds = boundsMm();
    return validPoint(bounds.topLeft()) && validPoint(bounds.bottomRight());
}
