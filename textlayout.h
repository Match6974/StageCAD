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
/// @param anchorMm 左上の配置位置（実寸mm）。
std::optional<TextGeometry> makeTextGeometry(QString text,double pointSize,QPointF anchorMm);
#endif
