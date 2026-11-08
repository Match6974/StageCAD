#include "shapedata.h"
#include <cmath>
#include <limits>

ShapeKind ShapeData::kind() const
{
    if (std::holds_alternative<QLineF>(geometryMm)) return ShapeKind::Line;
    if (std::holds_alternative<QRectF>(geometryMm)) return ShapeKind::Rectangle;
    if (std::holds_alternative<CircleGeometry>(geometryMm)) return ShapeKind::Circle;
    return ShapeKind::Polygon;
}

QPainterPath ShapeData::localPath() const
{
    QPainterPath path;
    if (const auto *line = std::get_if<QLineF>(&geometryMm)) {
        path.moveTo(line->p1()); path.lineTo(line->p2());
    } else if (const auto *rect = std::get_if<QRectF>(&geometryMm)) path.addRect(*rect);
    else if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm))
        path.addEllipse(circle->centerMm, circle->radiusMm, circle->radiusMm);
    else if (const auto *polygon = std::get_if<QPolygonF>(&geometryMm)) {
        path.addPolygon(*polygon); path.closeSubpath();
    }
    return path;
}

QRectF ShapeData::boundsMm() const { return localPath().boundingRect().translated(positionMm); }

bool ShapeData::validPoint(QPointF point)
{
    return std::isfinite(point.x()) && std::isfinite(point.y())
        && std::abs(point.x()) <= CoordinateLimitMm && std::abs(point.y()) <= CoordinateLimitMm;
}

bool ShapeData::validPolygon(const QPolygonF &points)
{
    if (points.size() < 3) return false;
    for (QPointF point : points) if (!validPoint(point)) return false;
    const QPointF first = points[0];
    for (qsizetype i = 1; i + 1 < points.size(); ++i) {
        const QPointF a = points[i] - first, b = points[i + 1] - first;
        const long double lhs = static_cast<long double>(a.x()) * b.y();
        const long double rhs = static_cast<long double>(a.y()) * b.x();
        const long double tolerance = 64 * std::numeric_limits<double>::epsilon()
            * (std::abs(lhs) + std::abs(rhs));
        if (std::abs(lhs - rhs) > tolerance) return true;
    }
    return false;
}

bool ShapeData::isValid() const
{
    if (id.isNull() || !std::isfinite(positionMm.x()) || !std::isfinite(positionMm.y())) return false;
    if (const auto *line = std::get_if<QLineF>(&geometryMm)) {
        if (!validPoint(line->p1()) || !validPoint(line->p2()) || line->length() <= 0) return false;
    } else if (const auto *rect = std::get_if<QRectF>(&geometryMm)) {
        if (!validPoint(rect->topLeft()) || !validPoint(rect->bottomRight())
            || !(rect->width() > 0) || !(rect->height() > 0)) return false;
    } else if (const auto *circle = std::get_if<CircleGeometry>(&geometryMm)) {
        if (!validPoint(circle->centerMm) || !std::isfinite(circle->radiusMm) || circle->radiusMm <= 0) return false;
        const QPointF extent(circle->radiusMm, circle->radiusMm);
        if (!validPoint(circle->centerMm - extent) || !validPoint(circle->centerMm + extent)) return false;
    } else if (!validPolygon(std::get<QPolygonF>(geometryMm))) return false;
    const QRectF bounds = boundsMm();
    return validPoint(bounds.topLeft()) && validPoint(bounds.bottomRight());
}
