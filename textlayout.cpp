/** @file textlayout.cpp
 * @brief 正立した日本語を含む横書きの字形・行ボックスを生成する。
 */
#include "textlayout.h"
#include <QFontDatabase>
#include <QFontMetricsF>
#include <cmath>
#include <algorithm>
/// @brief OSの一般フォントを試作版の固定フォントとする。
QString fixedTextFontFamily() { return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family(); }
/// @brief フォントを固定サイズで輪郭化し、CADの上向きYへ反転する。
/// @param text 改行を含む入力文字列。
/// @param pointSize 設定サイズ（pt）。極端な値の描画用フォントは作らない。
/// @param anchorMm 行ボックス左上（実寸mm）。
std::optional<TextGeometry> makeTextGeometry(QString text,double pointSize,QPointF anchorMm) {
    if (!std::isfinite(pointSize) || pointSize <= 0 || !ShapeData::validPoint(anchorMm)) return {};
    text.replace(QStringLiteral("\r\n"),QStringLiteral("\n")); text.replace('\r','\n');
    if (text.trimmed().isEmpty()) return {};
    TextGeometry result; // 1emのキャッシュ字形と設定値。モデルが値として所有する。
    result.text=text; result.pointSizePt=pointSize; result.anchorMm=anchorMm; result.fontFamily=fixedTextFontFamily();
    QFont font(result.fontFamily); // 大きな設定ptでもメモリを膨らませない固定解像度フォント。
    font.setPixelSize(1000);
    const QFontMetricsF metrics(font); // 1000px/emでの行送りと幅。
    const auto lines=text.split('\n'); // 末尾を含む空行も保持する。
    double width=0; // 最大行幅（固定解像度px）。
    for (qsizetype index=0; index<lines.size(); ++index) {
        const QString line=QString(lines[index]).replace('\t',QStringLiteral("    ")); // タブの表示文字列。
        result.unitPath.addText(QPointF(0,metrics.ascent()+index*metrics.lineSpacing()),font,line);
        width=std::max(width,metrics.horizontalAdvance(line));
    }
    result.unitPath=QTransform::fromScale(0.001,-0.001).map(result.unitPath);
    result.unitBounds=QRectF(0,-lines.size()*metrics.lineSpacing()/1000.0,width/1000.0,lines.size()*metrics.lineSpacing()/1000.0)
        .united(result.unitPath.boundingRect());
    return result;
}
