/**
 * @file drawinglayer.h
 * @brief 図形の所属先となるデザインレイヤーのUUID・名前・表示状態を定義する。
 */
#ifndef DRAWINGLAYER_H
#define DRAWINGLAYER_H
#include <QUuid>
#include <QString>

/// @brief 通常表示・非表示・参照用グレイ表示を区別するレイヤー属性。
enum class LayerVisibility {
    Visible, ///< 通常の色で表示する。
    Hidden,  ///< 作業領域に表示しない。
    Gray     ///< グレイの参照表示にする。
};

/// @brief 文書が所有するデザインレイヤーの値データ。図形は名前ではなくUUIDで所属を保持する。
struct DrawingLayer {
    // id: 名前変更でも変わらないレイヤーの識別子。
    QUuid id;
    // name: ナビゲーションパレットに表示する一意の名前。
    QString name;
    // visibility: このレイヤーの通常・非表示・グレイの指定。
    LayerVisibility visibility = LayerVisibility::Visible;
};
#endif
