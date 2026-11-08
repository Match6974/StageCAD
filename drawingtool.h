#ifndef DRAWINGTOOL_H
#define DRAWINGTOOL_H

#include <QMetaType>

enum class DrawingTool {
    Selection,
    Pan,
    Line,
    Rectangle,
    Circle,
    Polygon,
    Reshape,
    Join,
    Dimension,
    Text
};

Q_DECLARE_METATYPE(DrawingTool)

#endif
