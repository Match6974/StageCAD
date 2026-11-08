#ifndef DRAWINGDOCUMENT_H
#define DRAWINGDOCUMENT_H

#include "shapedata.h"
#include <QObject>
#include <QList>
#include <optional>

// Owns committed CAD data without depending on a scene or widget. DrawingView
// owns only its rendering items and temporary interaction previews.
class DrawingDocument : public QObject {
    Q_OBJECT
public:
    explicit DrawingDocument(QObject *parent = nullptr) : QObject(parent) {}
    const QList<ShapeData> &shapes() const { return m_shapes; }
    std::optional<ShapeData> shape(const QUuid &id) const;
    QUuid addShape(ShapeGeometry geometryMm);
    bool canTranslateShapes(const QList<QUuid> &ids, QPointF deltaMm) const;
    bool translateShapes(const QList<QUuid> &ids, QPointF deltaMm);
    bool removeShapes(const QList<QUuid> &ids);

signals:
    void aboutToChange();
    void shapeAdded(const QUuid &id);
    void shapeChanged(const QUuid &id);
    void shapeRemoved(const QUuid &id);

private:
    QList<ShapeData> m_shapes; // In drawing/stacking order.
    bool m_mutating = false;
};

#endif
