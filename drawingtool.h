/**
 * @file drawingtool.h
 * @brief 基本パレットで共有するツールの種類を定義する。
 */

#ifndef DRAWINGTOOL_H
#define DRAWINGTOOL_H

#include <QMetaType>

/// @brief 選択・パン・各描画ツールを識別する。後続タスクのツールも種類だけ定義する。
enum class DrawingTool {
    /* 既存図形の選択・移動・削除。 */ Selection,
    /* 作業領域の画面移動。 */ Pan,
    /* 直線の形状・描画。 */ Line,
    /* 四角形の形状・描画。 */ Rectangle,
    /* 真円の形状・描画。 */ Circle,
    /* 頂点列による多角形の形状・描画。 */ Polygon,
    /* 後続タスクの変形ツール。 */ Reshape,
    /* 後続タスクの結合・合成ツール。 */ Join,
    /* 後続タスクの寸法ツール。 */ Dimension,
    /* 後続タスクの文字ツール。 */ Text
};

// ツール種別をQVariantやシグナルの引数として扱えるよう、Qtの型システムへ登録する。
Q_DECLARE_METATYPE(DrawingTool)

#endif
