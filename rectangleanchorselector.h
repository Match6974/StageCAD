/**
 * @file rectangleanchorselector.h
 * @brief 長方形のサイズ変更で固定する9点を選ぶパレット用コントロール。
 */
#ifndef RECTANGLEANCHORSELECTOR_H
#define RECTANGLEANCHORSELECTOR_H
#include <QWidget>
#include <QPointF>
class QButtonGroup;

/// @brief 左上から右下の9点を排他的に選ぶ。図形データ自体は所有・変更しない。
class RectangleAnchorSelector : public QWidget {
    Q_OBJECT
public:
    /// @brief 3×3のボタンを作り、中心を初期選択にする。
    /// @param parent このコントロールと子ボタンを所有するQtの親。
    explicit RectangleAnchorSelector(QWidget *parent = nullptr);
    /// @brief 左上0・中心4・右下8の行優先インデックスを返す。
    int anchorIndex() const { return m_anchorIndex; }
    /// @brief 指定点を選ぶ。不正インデックスは拒否し、同じ点では通知しない。
    /// @param index 0〜8の行優先インデックス。
    bool setAnchorIndex(int index);
    /// @brief 左下を(0,0)、右上を(1,1)とする固定点の相対位置を返す（無次元）。
    QPointF normalizedAnchor() const;
    /// @brief 現在選択中の固定点の表示名を返す。
    QString anchorName() const;
    // AnchorCount: 角4点・辺中央4点・中心1点の合計。
    static constexpr int AnchorCount = 9;
signals:
    /// @brief 固定点が変更された後に通知する。図形のサイズは変更しない。
    /// @param index 新しい行優先インデックス（0〜8）。
    void anchorChanged(int index);
private:
    // m_group: 9点の排他選択を管理する、このウィジェット所有のグループ。
    QButtonGroup *m_group;
    // m_anchorIndex: 初期は中心4。パレットが生存する間、最後の選択を保持する。
    int m_anchorIndex = 4;
};
#endif
