/** @file textalignmenteditor.h
 * @brief 文字の水平・垂直揃えとAlignment Pointを選ぶ共用UIを定義する。
 */
#ifndef TEXTALIGNMENTEDITOR_H
#define TEXTALIGNMENTEDITOR_H
#include "shapedata.h"
#include <QWidget>
class QComboBox;
/// @brief 表示バー・文字ダイアログ・情報パレットで共用する揃え入力。モデルは所有しない。
class TextAlignmentEditor : public QWidget {
    Q_OBJECT
public:
    /// @brief 左揃え・上を初期値として入力を作る。
    /// @param parent 子入力を含めて所有するQtの親。
    explicit TextAlignmentEditor(QWidget *parent=nullptr);
    /// @brief 各行に適用する水平揃えを返す。
    TextHorizontalAlignment horizontalAlignment() const;
    /// @brief サイズ変更で固定する垂直基準を返す。
    TextVerticalAlignment verticalAlignment() const;
    /// @brief 有効な揃えを通知なしで表示する。不正値では変更せずfalseを返す。
    /// @param horizontal 左・中央・右。
    /// @param vertical 上・先頭行ベースライン・中央・下。
    bool setAlignment(TextHorizontalAlignment horizontal,TextVerticalAlignment vertical);
signals:
    /// @brief ユーザーが揃えを変更したことを通知する。モデルへの反映は利用側が行う。
    void alignmentChanged();
private:
    QComboBox *m_horizontal; // このウィジェット所有の水平揃え入力。
    QComboBox *m_vertical; // このウィジェット所有の垂直基準入力。
};
#endif
