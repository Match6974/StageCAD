#include "drawingdocument.h"
#include <QScopedValueRollback>
#include <QSet>
#include <algorithm>
#include <cmath>

std::optional<ShapeData> DrawingDocument::shape(const QUuid &id) const
{
    for (const auto &shape : m_shapes) if (shape.id == id) return shape;
    return std::nullopt;
}

QUuid DrawingDocument::addShape(ShapeGeometry geometryMm)
{
    if (m_mutating) return {};
    ShapeData data{QUuid::createUuid(), std::move(geometryMm), {}};
    if (!data.isValid()) return {};
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    m_shapes.append(data); emit shapeAdded(data.id);
    return data.id;
}

bool DrawingDocument::canTranslateShapes(const QList<QUuid> &ids, QPointF delta) const
{
    if (ids.isEmpty() || !std::isfinite(delta.x()) || !std::isfinite(delta.y())) return false;
    for (const auto &id : ids) {
        auto data = shape(id);
        if (!data) return false;
        data->positionMm += delta;
        if (!data->isValid()) return false;
    }
    return true;
}

bool DrawingDocument::translateShapes(const QList<QUuid> &ids, QPointF delta)
{
    if (m_mutating || !canTranslateShapes(ids, delta)) return false;
    const QSet<QUuid> unique(ids.cbegin(), ids.cend());
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    // Validate the whole operation first, then update all data before notifying views.
    for (auto &data : m_shapes) if (unique.contains(data.id)) data.positionMm += delta;
    for (const auto &id : unique) emit shapeChanged(id);
    return true;
}

bool DrawingDocument::removeShapes(const QList<QUuid> &ids)
{
    if (m_mutating || ids.isEmpty()) return false;
    for (const auto &id : ids) if (!shape(id)) return false;
    const QSet<QUuid> unique(ids.cbegin(), ids.cend());
    QScopedValueRollback<bool> mutating(m_mutating, true);
    emit aboutToChange();
    m_shapes.erase(std::remove_if(m_shapes.begin(), m_shapes.end(),
        [&](const ShapeData &data) { return unique.contains(data.id); }), m_shapes.end());
    for (const auto &id : unique) emit shapeRemoved(id);
    return true;
}
