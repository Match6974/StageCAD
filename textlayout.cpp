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
/// @param anchorMm サイズ変更でも動かさないAlignment Point（実寸mm）。
/// @param horizontal 文字の水平揃え。各行を同じ基準Xに合わせる。
/// @param vertical 文字の垂直基準。ベースラインは先頭行を使用する。
std::optional<TextGeometry> makeTextGeometry(QString text,double pointSize,QPointF anchorMm,
    TextHorizontalAlignment horizontal,TextVerticalAlignment vertical) {
    if (!std::isfinite(pointSize) || pointSize <= 0 || !ShapeData::validPoint(anchorMm) || !validTextAlignment(horizontal,vertical)) return {};
    text.replace(QStringLiteral("\r\n"),QStringLiteral("\n")); text.replace('\r','\n');
    if (text.trimmed().isEmpty()) return {};
    TextGeometry result; // 1emのキャッシュ字形と設定値。モデルが値として所有する。
    result.text=text; result.pointSizePt=pointSize; result.anchorMm=anchorMm; result.fontFamily=fixedTextFontFamily();
    result.horizontalAlignment=horizontal; result.verticalAlignment=vertical;
    QFont font(result.fontFamily); // 大きな設定ptでもメモリを膨らませない固定解像度フォント。
    font.setPixelSize(1000);
    const QFontMetricsF metrics(font); // 1000px/emでの行送りと幅。
    auto lines=text.split('\n'); // 空行を保持し、表示タブだけを空白へ展開する。
    QList<double> advances; // 各行の送り幅（固定フォントpx）。水平揃えで使用する。
    double width=0; // 最大行幅（固定フォントpx）。
    for (auto &line : lines) { // GUIスレッドで整形する各行のプレーンテキスト。
        line.replace('\t',QStringLiteral("    "));
        const double advance=metrics.horizontalAdvance(line); // 空白も含めた各行の送り幅。
        advances.append(advance); width=std::max(width,advance);
    }
    const double fraction=0.5*static_cast<int>(horizontal); // 左0・中央0.5・右1の基準X。
    const double height=lines.size()*metrics.lineSpacing(); // 空行を含む行ボックス高（固定フォントpx）。
    double offset=0; // 上端から選択した垂直基準への距離（固定フォントpx）。
    switch (vertical) {
    case TextVerticalAlignment::Top: break;
    case TextVerticalAlignment::FirstBaseline: offset=metrics.ascent(); break;
    case TextVerticalAlignment::Center: offset=height/2; break;
    case TextVerticalAlignment::Bottom: offset=height; break;
    }
    for (qsizetype index=0; index<lines.size(); ++index) { // 各行を共通のAlignment Pointに揃える。
        result.unitPath.addText(QPointF(-advances[index]*fraction,metrics.ascent()+index*metrics.lineSpacing()),font,lines[index]);
    }
    result.unitPath=QTransform::fromScale(0.001,-0.001).map(result.unitPath).translated(0,offset/1000);
    result.unitBounds=QRectF(-width*fraction/1000,(-height+offset)/1000,width/1000,height/1000)
        .united(result.unitPath.boundingRect());
    return result;
}
