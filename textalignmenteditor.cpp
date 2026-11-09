/** @file textalignmenteditor.cpp
 * @brief ベースラインを含むAlignment Pointの選択を各文字UIへ提供する。
 */
#include "textalignmenteditor.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
/// @brief 揃えと固定点の意味を明示し、幅指定・文字の変形とは分離する。
/// @param parent この入力を所有するQtの親。
TextAlignmentEditor::TextAlignmentEditor(QWidget *parent) : QWidget(parent) {
    auto *layout=new QHBoxLayout(this); // ウィジェット所有の横並びレイアウト。
    layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
    m_horizontal=new QComboBox(this); m_horizontal->setObjectName("textHorizontalAlignment");
    m_horizontal->addItems({tr("左揃え"),tr("中央揃え"),tr("右揃え")});
    m_horizontal->setAccessibleName(tr("文字の水平揃え"));
    m_vertical=new QComboBox(this); m_vertical->setObjectName("textVerticalAlignment");
    m_vertical->addItems({tr("上"),tr("ベースライン"),tr("中央"),tr("下")});
    m_vertical->setAccessibleName(tr("文字の垂直基準"));
    m_vertical->setToolTip(tr("ベースラインは先頭行の文字の基線です。フォントサイズ変更では、この水平・垂直揃えで決まるAlignment Pointを固定します。"));
    layout->addWidget(new QLabel(tr("水平"),this)); layout->addWidget(m_horizontal);
    layout->addWidget(new QLabel(tr("垂直"),this)); layout->addWidget(m_vertical);
    connect(m_horizontal,&QComboBox::currentIndexChanged,this,&TextAlignmentEditor::alignmentChanged);
    connect(m_vertical,&QComboBox::currentIndexChanged,this,&TextAlignmentEditor::alignmentChanged);
}
/// @brief 順序を列挙型と共有する水平選択を返す。
TextHorizontalAlignment TextAlignmentEditor::horizontalAlignment() const { return static_cast<TextHorizontalAlignment>(m_horizontal->currentIndex()); }
/// @brief ベースラインを含む垂直选択を返す。
TextVerticalAlignment TextAlignmentEditor::verticalAlignment() const { return static_cast<TextVerticalAlignment>(m_vertical->currentIndex()); }
/// @brief モデル更新に伴う表示反映では、編集通知を出さない。
/// @param horizontal 表示する水平揃え。
/// @param vertical 表示する垂直基準。
bool TextAlignmentEditor::setAlignment(TextHorizontalAlignment horizontal,TextVerticalAlignment vertical) {
    if (!validTextAlignment(horizontal,vertical)) return false;
    const QSignalBlocker blockHorizontal(m_horizontal),blockVertical(m_vertical); // 表示更新からの再入を防ぐ。
    m_horizontal->setCurrentIndex(static_cast<int>(horizontal)); m_vertical->setCurrentIndex(static_cast<int>(vertical));
    return true;
}
