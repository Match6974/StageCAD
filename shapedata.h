#ifndef SHAPEDATA_H
#define SHAPEDATA_H

#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QUuid>
#include <variant>

// Document IDs identify instances; they are unrelated to feature.csv IDs.
enum class ShapeKind { Line, Rectangle, Circle, Polygon };
struct CircleGeometry {
    QPointF centerMm;
    double radiusMm = 0;
};
using ShapeGeometry = std::variant<QLineF, QRectF, CircleGeometry, QPolygonF>;

// Geometry and translation are both real mm. Screen zoom and paper scale never
// enter this value. Polygon vertex order is preserved, with an implicit closure.
struct ShapeData {
    QUuid id;
    ShapeGeometry geometryMm;
    QPointF positionMm;

    ShapeKind kind() const;
    QPainterPath localPath() const;
    QRectF boundsMm() const;
    bool isValid() const;
    static bool validPoint(QPointF point);
    static bool validPolygon(const QPolygonF &points);
    static constexpr double CoordinateLimitMm = 10000000;
};

#endif
