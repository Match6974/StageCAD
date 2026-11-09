/** @file textlayout.h
 * @brief 固定フォントの横書き文字を、表示解像度に依存しない字形へ変換する。
 */
#ifndef TEXTLAYOUT_H
#define TEXTLAYOUT_H
#include "shapedata.h"
#include <optional>
/// @brief 試作版で固定するシステムの一般フォント名を返す。GUIスレッド専用。
QString fixedTextFontFamily();
/// @brief 横書き・複数行の字形を1emで生成する。空文字・不正サイズでは空を返す。GUIスレッド専用。
/// @param text プレーンテキスト。CRLFをLFに統一し、タブは表示時に4空白へ展開する。
/// @param pointSize 設定する文字サイズ（pt）。
/// @param anchorMm 固定するAlignment Pointの実寸座標（mm）。
/// @param horizontal 各行の水平揃え。初期値は左。
/// @param vertical 垂直基準。ベースラインは先頭行。初期値は上。
std::optional<TextGeometry> makeTextGeometry(QString text,double pointSize,QPointF anchorMm,
    TextHorizontalAlignment horizontal=TextHorizontalAlignment::Left,
    TextVerticalAlignment vertical=TextVerticalAlignment::Top);
#endif
