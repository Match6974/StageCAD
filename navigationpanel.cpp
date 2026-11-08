/**
 * @file navigationpanel.cpp
 * @brief デザインレイヤーの安定UUIDに基づいてナビゲーションの行と文書を同期する。
 */
#include "navigationpanel.h"
#include "drawingdocument.h"
#include "drawingsettings.h"
#include <QVBoxLayout>
#include <QTreeWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QSignalBlocker>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QIcon>
#include <QStyle>

namespace {
/// @brief 目・バツ・灰色の目を描き、表示状態を色以外の形でも区別する。
/// @param visibility 通常・非表示・グレイの指定。
QIcon visibilityIcon(LayerVisibility visibility) {
    // pixmap: 高DPIでも読みやすい余白を含む、32pxの状態アイコン。
    QPixmap pixmap(32,32); pixmap.fill(Qt::transparent);
    // painter: アイコンのベクトル図形を描くローカル描画器。
    QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(visibility == LayerVisibility::Gray ? QColor("#aaaaaa") : QColor("#24374b"),2));
    if (visibility == LayerVisibility::Hidden) { painter.drawLine(9,9,23,23); painter.drawLine(23,9,9,23); }
    else {
        // eye: 両端が細く中央に瞳がある目の輪郭。
        QPainterPath eye; eye.moveTo(3,16); eye.quadTo(16,1,29,16); eye.quadTo(16,31,3,16);
        painter.drawPath(eye); painter.setBrush(painter.pen().color()); painter.drawEllipse(QPoint(16,16),4,4);
    }
    return QIcon(pixmap);
}
}

/// @brief 一覧のセル操作をUUIDで文書へ渡し、文書から更新された値だけを表示する。
/// @param document 操作する文書。所有権は取得しない。
/// @param settings 現在の縮尺表示の参照先。
/// @param parent パネルの所有者。
NavigationPanel::NavigationPanel(DrawingDocument *document,DrawingSettings *settings,QWidget *parent)
    : QWidget(parent),m_document(document),m_settings(settings)
{
    setObjectName("navigationPanel"); setMinimumWidth(250);
    // layout: レイヤー一覧と追加・エラーをまとめるパネル所有のレイアウト。
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("デザインレイヤー"),this));
    m_list = new QTreeWidget(this); m_list->setObjectName("layerList");
    m_list->setHeaderLabels({tr("表示"),tr("作成先"),tr("レイヤー名"),tr("縮尺")});
    m_list->setRootIsDecorated(false); m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(2,QHeaderView::Stretch);
    m_list->header()->setSectionResizeMode(3,QHeaderView::ResizeToContents);
    layout->addWidget(m_list);
    // add: 自動採番して作成先も切り替える、パネル所有のボタン。
    auto *add = new QPushButton(tr("レイヤーを追加"),this); add->setObjectName("addLayerButton"); layout->addWidget(add);
    m_error = new QLabel(this); m_error->setObjectName("layerError"); m_error->setWordWrap(true); layout->addWidget(m_error);
    // コールバック: 文書が有効な場合だけレイヤーを追加する。
    connect(add,&QPushButton::clicked,this,[this] { if (m_document && isEnabled()) m_document->addLayer(); });
    // item: クリックした行。column: 表示状態または作成先を切り替える列番号。
    // コールバック: クリックされた行のUUIDを使い、ソートや名前に依存しない操作を行う。
    connect(m_list,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem *item,int column) {
        if (!m_document || !isEnabled()) return;
        // id: 行が保持する安定レイヤーUUID。
        const auto id = item->data(0,Qt::UserRole).toUuid();
        if (column == 1) m_document->setActiveLayer(id);
        else if (column == 0) {
            // layer: クリック時点の最新の表示状態。
            const auto layer = m_document->layer(id); if (!layer) return;
            // next: 通常→非表示→グレイ→通常の順で巡回する指定。
            const auto next = layer->visibility == LayerVisibility::Visible ? LayerVisibility::Hidden
                : layer->visibility == LayerVisibility::Hidden ? LayerVisibility::Gray : LayerVisibility::Visible;
            m_document->setLayerVisibility(id,next);
        }
    });
    // item: 名前を編集する行。column: 名前列だけを編集可能にする判定値。
    // コールバック: 名前のダブルクリックに限定してインライン編集を開始する。
    connect(m_list,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int column) {
        if (column == 2 && isEnabled() && m_document) m_list->editItem(item,column);
    });
    // item: 名前を確定した行。column: 変更された列。
    // コールバック: 空白・重複等で名前変更できない場合は正本を表示し、理由を案内する。
    connect(m_list,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column) {
        if (!m_document || column != 2 || !isEnabled()) return;
        if (!m_document->renameLayer(item->data(0,Qt::UserRole).toUuid(),item->text(2))) {
            refresh(); m_error->setText(tr("名前は空白のみ・制御文字・重複を避けて指定してください。"));
        }
    });
    if (document) {
        connect(document,&DrawingDocument::layersChanged,this,&NavigationPanel::refresh);
        // コールバック: 参照先が先に破棄されても古い行や操作を残さない。
        connect(document,&QObject::destroyed,this,[this] { m_document = nullptr; refresh(); });
    }
    if (settings) connect(settings,&DrawingSettings::scaleChanged,this,&NavigationPanel::refresh);
    refresh();
}
/// @brief ツリーや参照モデルの破棄より先に、パネルへの通知を切り離す。
NavigationPanel::~NavigationPanel() {
    disconnect(m_list,nullptr,this,nullptr);
    if (m_document) disconnect(m_document,nullptr,this,nullptr);
    if (m_settings) disconnect(m_settings,nullptr,this,nullptr);
}
/// @brief 同じUUIDの行を再利用し、通常表示・作成先・名前・現在の縮尺を反映する。
void NavigationPanel::refresh()
{
    // blocker: 表示値の更新から名前変更の再入が起きないようにするガード。
    QSignalBlocker blocker(m_list); m_error->clear();
    if (!m_document) { m_list->clear(); setEnabled(false); return; }
    // row: 存在しなくなったレイヤーだけを削除する走査位置。
    for (int row=m_list->topLevelItemCount()-1; row>=0; --row)
        if (!m_document->layer(m_list->topLevelItem(row)->data(0,Qt::UserRole).toUuid())) delete m_list->takeTopLevelItem(row);
    // row: 文書の順序に従う表示位置。
    int row = 0;
    // layer: 文書が所有する各レイヤーの現在値。
    for (const auto &layer : m_document->layers()) {
        // item: 追加・更新する一覧の行。ツリーが所有する。
        auto *item = m_list->topLevelItem(row);
        if (!item || item->data(0,Qt::UserRole).toUuid() != layer.id) {
            item = new QTreeWidgetItem; item->setData(0,Qt::UserRole,layer.id); item->setFlags(item->flags()|Qt::ItemIsEditable);
            m_list->insertTopLevelItem(row,item);
        }
        item->setIcon(0,visibilityIcon(layer.visibility));
        // state: アイコン以外でも現在値が分かる表示・アクセシビリティ用の状態名。
        const QString state = layer.visibility == LayerVisibility::Visible ? tr("通常表示")
            : layer.visibility == LayerVisibility::Hidden ? tr("非表示") : tr("グレイ表示");
        item->setToolTip(0,tr("現在: %1。クリックで通常表示 → 非表示 → グレイ表示を切り替えます。").arg(state));
        item->setData(0,Qt::AccessibleTextRole,state);
        item->setIcon(1,layer.id == m_document->activeLayerId() ? style()->standardIcon(QStyle::SP_DialogApplyButton) : QIcon());
        item->setToolTip(1,tr("クリックで新しい図形の作成先にします。通常表示に戻ります。"));
        item->setText(2,layer.name); item->setToolTip(2,tr("ダブルクリックで名前を変更します。"));
        item->setText(3,QStringLiteral("1:%1").arg(m_settings ? m_settings->scaleDenominator() : 1,0,'g',12));
        ++row;
    }
}
