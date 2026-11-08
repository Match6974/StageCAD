/**
 * @file rectangleanchorselector.cpp
 * @brief 9点の選択表示と、CADの上向きY軸に対応する固定点の相対位置を提供する。
 */
#include "rectangleanchorselector.h"
#include <QButtonGroup>
#include <QGridLayout>
#include <QToolButton>
#include <array>

namespace {
// AnchorNames: 画面の左上から右下へ行優先で並ぶ、固定点の翻訳用表示名。
const std::array<const char*, 9> AnchorNames = {
    QT_TRANSLATE_NOOP("RectangleAnchorSelector","左上"), QT_TRANSLATE_NOOP("RectangleAnchorSelector","上（中央）"),
    QT_TRANSLATE_NOOP("RectangleAnchorSelector","右上"), QT_TRANSLATE_NOOP("RectangleAnchorSelector","左（中央）"),
    QT_TRANSLATE_NOOP("RectangleAnchorSelector","中心"), QT_TRANSLATE_NOOP("RectangleAnchorSelector","右（中央）"),
    QT_TRANSLATE_NOOP("RectangleAnchorSelector","左下"), QT_TRANSLATE_NOOP("RectangleAnchorSelector","下（中央）"),
    QT_TRANSLATE_NOOP("RectangleAnchorSelector","右下")};
}
/// @brief 黒い四角で選択点を示すボタンを作り、排他的な9点選択へ接続する。
/// @param parent コントロールと子ボタンを所有するQtの親。
RectangleAnchorSelector::RectangleAnchorSelector(QWidget *parent) : QWidget(parent)
{
    setObjectName("rectangleAnchorSelector");
    // layout: 9点を3×3に並べる、このウィジェット所有のレイアウト。
    auto *layout = new QGridLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(2);
    setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);
    m_group = new QButtonGroup(this); m_group->setExclusive(true);
    // index: 画面上の左上を0とする行優先の固定点番号。
    for (int index = 0; index < AnchorCount; ++index) {
        // button: このウィジェットが所有する、1つの固定点を示すボタン。
        auto *button = new QToolButton(this); button->setCheckable(true); button->setFixedSize(26,26);
        button->setText(QString::fromUtf8("□")); button->setObjectName(QStringLiteral("rectangleAnchor_%1").arg(index));
        button->setToolTip(tr(AnchorNames[index])); button->setAccessibleName(tr(AnchorNames[index]));
        // checked: 排他選択の状態。チェック時だけ塗りつぶした四角を表示する。
        // コールバック: 図形を変更せず、選択点の塗りつぶし表示だけを切り替える。
        connect(button,&QToolButton::toggled,button,[button](bool checked) {
            button->setText(checked ? QString::fromUtf8("■") : QString::fromUtf8("□"));
        });
        m_group->addButton(button,index); layout->addWidget(button,index/3,index%3);
    }
    m_group->button(m_anchorIndex)->setChecked(true);
    // index: ユーザーがクリックした固定点の番号。
    // コールバック: 同じ点のクリックは除外し、新しい基準だけを通知する。
    connect(m_group,&QButtonGroup::idClicked,this,[this](int index) {
        if (m_anchorIndex == index) return;
        m_anchorIndex = index; emit anchorChanged(index);
    });
}
/// @brief 有効な点だけを選択し、ボタン表示を同期して1回通知する。
/// @param index 0〜8の固定点番号。
bool RectangleAnchorSelector::setAnchorIndex(int index)
{
    if (index < 0 || index >= AnchorCount) return false;
    if (m_anchorIndex == index) return true;
    m_anchorIndex = index; m_group->button(index)->setChecked(true); emit anchorChanged(index);
    return true;
}
/// @brief 画面の上段をCADの上側（Y=1）へ対応付け、固定点の相対座標を返す。
QPointF RectangleAnchorSelector::normalizedAnchor() const
{
    return {0.5 * (m_anchorIndex % 3), 1 - 0.5 * (m_anchorIndex / 3)};
}
/// @brief 現在の固定点に対応する、翻訳済みの表示名を返す。
QString RectangleAnchorSelector::anchorName() const { return tr(AnchorNames[m_anchorIndex]); }
