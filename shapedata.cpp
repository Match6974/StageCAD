/**
 * @file shapedata.cpp
 * @brief 図形の種類別形状、実寸mmの配置、IDと形状検証を定義する。
 */

#include "shapedata.h"
#include <cmath>
#include <limits>

/// @brief 文字固有の水平3種類・垂直4種類だけを受け付ける。
/// @param horizontal 左・中央・右の水平揃え。
/// @param vertical 上・先頭行ベースライン・中央・下の垂直基準。
bool validTextAlignment(TextHorizontalAlignment horizontal,TextVerticalAlignment vertical) {
    return horizontal >= TextHorizontalAlignment::Left && horizontal <= TextHorizontalAlignment::Right
        && vertical >= TextVerticalAlignment::Top && vertical <= TextVerticalAlignment::Bottom;
}

/// @brief 形状データの型から図形の種類を返す。
ShapeKind ShapeData::kind() const
{
    if (std::holds_alternative<QLineF>(geometryMm)) return ShapeKind::Line;
    if (std::holds_alternative<QRectF>(geometryMm)) return ShapeKind::Rectangle;
    if (std::holds_alternative<CircleGeometry>(geometryMm)) return ShapeKind::Circle;
    if (std::holds_alternative<TextGeometry>(geometryMm)) return ShapeKind::Text;
    return ShapeKind::Polygon;
}

/// @brief 四角形のローカル左下を動かさず、CAD座標で反時計回りに回転する。
QTransform ShapeData::rectangleTransform() const
{
    // transform: 配置移動量とは分離した四角形の回転変換。
    QTransform transform;
    if (const auto *rect = std::get_if<QRectF>(&geometryMm)) {
        transform.translate(rect->x(),rect->y()); transform.rotate(rectangleAngleDeg);
        transform.translate(-rect->x(),-rect->y());
    }
    return transform;
}
/// @brief サイズ・角度変更時に保持する基準点の実寸座標を返す。
/// @param fraction 基準点の相対位置。CADの下側がY=0。
QPointF ShapeData::referencePoint(QPointF fraction) const
{
    if (const auto *rect = std::get_if<QRectF>(&geometryMm))
        return rectangleTransform().map(rect->topLeft()+QPointF(rect->width()*fraction.x(),rect->height()*fraction.y()))+positionMm;
    if (const auto *text = std::get_if<TextGeometry>(&geometryMm)) return text->anchorMm + positionMm;
    // bounds: 多角形の配置済み外接範囲（実寸mm）。
    const QRectF bounds = boundsMm();
    return bounds.topLeft()+QPointF(bounds.width()*fraction.x(),bounds.height()*fraction.y());
}
/// @brief 円弧・開いた多角形では、内部や閉じていない辺を選択対象にしない。
bool ShapeData::isFilled() const
{
    if (kind() == ShapeKind::Line) return false;
    if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm)) return circle->sweepAngleDeg == 360;
    return kind() != ShapeKind::Polygon || polygonClosed;
}

/// @brief 配置移動量を加える前の形状を、描画・選択判定用のパスへ変換する。
QPainterPath ShapeData::localPath(double textModelScale) const
{
    // path: 配置移動量を加える前の形状パス（実寸mm）。
    QPainterPath path;
    // line: 対象の直線形状またはその描画アイテム。
    if (const auto *line = std::get_if<QLineF>(&geometryMm)) {
        path.moveTo(line->p1()); path.lineTo(line->p2());
    // rect: 処理対象の矩形形状またはその描画アイテム。
    } else if (const auto *rect = std::get_if<QRectF>(&geometryMm)) { path.addRect(*rect); path = rectangleTransform().map(path); }
    // circle: 対象の円形状またはその描画アイテム。
    else if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm))
    {
        if (circle->sweepAngleDeg == 360) path.addEllipse(circle->centerMm,circle->radiusMm,circle->radiusMm);
        else {
            // extent: 円弧の元になる真円の外接範囲（実寸mm）。Qtの角度をCADの上向きYへ変換する。
            const QRectF extent(circle->centerMm-QPointF(circle->radiusMm,circle->radiusMm),QSizeF(2*circle->radiusMm,2*circle->radiusMm));
            path.arcMoveTo(extent,-circle->startAngleDeg); path.arcTo(extent,-circle->startAngleDeg,-circle->sweepAngleDeg);
        }
    }
    // polygon: 頂点順を保持した多角形の形状または描画アイテム。
    else if (const auto *polygon = std::get_if<QPolygonF>(&geometryMm)) {
        path.addPolygon(*polygon); if (polygonClosed) path.closeSubpath();
    }
    if (const auto *text = std::get_if<TextGeometry>(&geometryMm)) {
        // emMm: 設定ptを表示上の実寸mmへ換算した1em。
        const double emMm = text->pointSizePt * (25.4 / 72.0) * textModelScale;
        QTransform transform; // Alignment Pointを固定した配置と字形の拡大だけを行う。
        transform.translate(text->anchorMm.x(),text->anchorMm.y()); transform.scale(emMm,emMm);
        path = transform.map(text->unitPath);
    }
    return path;
}

/// @brief 配置移動量を含めた実寸mmの外接矩形を返す。
QRectF ShapeData::boundsMm(double textModelScale) const {
    if (const auto *text = std::get_if<TextGeometry>(&geometryMm)) {
        const double emMm = text->pointSizePt * (25.4 / 72.0) * textModelScale; // 表示上の1em（実寸mm）。
        QTransform transform; // 空白を含む行ボックスを実寸へ変換する。
        transform.translate(text->anchorMm.x(),text->anchorMm.y()); transform.scale(emMm,emMm);
        return transform.mapRect(text->unitBounds).translated(positionMm);
    }
    return localPath().boundingRect().translated(positionMm);
}

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
bool ShapeData::isValid(double textModelScale) const
{
    if (!std::isfinite(rectangleAngleDeg) || (kind() != ShapeKind::Rectangle && rectangleAngleDeg != 0)) return false;
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
        if (!std::isfinite(circle->startAngleDeg) || !std::isfinite(circle->sweepAngleDeg)
            || circle->sweepAngleDeg <= 0 || circle->sweepAngleDeg > 360) return false;
        if (!validPoint(circle->centerMm) || !std::isfinite(circle->radiusMm) || circle->radiusMm <= 0) return false;
        // extent: 円の外接矩形を得るための半径ベクトル（実寸mm）。
        const QPointF extent(circle->radiusMm, circle->radiusMm);
        if (!validPoint(circle->centerMm - extent) || !validPoint(circle->centerMm + extent)) return false;
    } else if (const auto *text = std::get_if<TextGeometry>(&geometryMm)) {
        if (!validTextAlignment(text->horizontalAlignment,text->verticalAlignment)
            || text->text.trimmed().isEmpty() || text->fontFamily.isEmpty()
            || !std::isfinite(text->pointSizePt) || text->pointSizePt <= 0
            || !std::isfinite(textModelScale) || textModelScale <= 0 || !validPoint(text->anchorMm)
            || text->unitPath.isEmpty() || !text->unitBounds.isValid()) return false;
        // index: キャッシュ字形内の座標も有限であることを確認する走査番号。
        for (int index=0; index<text->unitPath.elementCount(); ++index) {
            const auto element = text->unitPath.elementAt(index); // 1em字形のパス要素。
            if (!std::isfinite(element.x) || !std::isfinite(element.y)) return false;
        }
        if (!text->unitBounds.contains(text->unitPath.boundingRect())) return false;
    } else {
        // points: 開閉に応じて検証する頂点列。開いた形状は2点の線分まで許可する。
        const auto &points = std::get<QPolygonF>(geometryMm);
        if (polygonClosed) { if (!validPolygon(points)) return false; }
        else {
            if (points.size() < 2) return false;
            // point: 有限かつ範囲内である必要がある各頂点（実寸mm）。
            for (const auto point : points) if (!validPoint(point)) return false;
            if (points.boundingRect().width() == 0 && points.boundingRect().height() == 0) return false;
        }
    }
    // bounds: 検証・描画・測定対象の外接矩形（特記がなければ実寸mm）。
    const QRectF bounds = boundsMm(textModelScale);
    return validPoint(bounds.topLeft()) && validPoint(bounds.bottomRight());
}
