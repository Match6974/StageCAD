/**
 * @file toolpalette.cpp
 * @brief 基本ツールの排他選択、アイコン表示、幅に応じた再配置を扱う。
 */

#include "toolpalette.h"

#include <QAction>
#include <QActionGroup>
#include <QEvent>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <array>

namespace {
/// @brief ツールの種類、仕様ID、表示文字列、使用可否を結び付ける定義。
struct ToolEntry {
    // tool: 現在処理するツールの種類。
    DrawingTool tool;
    // featureId: 対応するCSVの機能ID文字列。
    const char *featureId;
    // name: 画面部品の識別名または表示名。
    const char *name;
    // button: ツールに対応する表示ボタンまたはそのラベル。
    const char *button;
    // mode: ツールの選択モード表示。
    const char *mode;
    // available: このツールが実装済みで、選択操作を許可するかどうか。
    bool available;
};

// tools: ツールの表示順、CSV機能ID、初期モード、使用可否をまとめた定数表。
const std::array<ToolEntry, 10> tools = {{
    {DrawingTool::Selection, "140301", QT_TRANSLATE_NOOP("ToolPalette", "セレクションツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "選択"), QT_TRANSLATE_NOOP("ToolPalette", "セレクションモード"), true},
    {DrawingTool::Pan, "140401", QT_TRANSLATE_NOOP("ToolPalette", "パンツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "パン"), QT_TRANSLATE_NOOP("ToolPalette", "パンモード"), true},
    {DrawingTool::Line, "140501", QT_TRANSLATE_NOOP("ToolPalette", "直線ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "直線"), QT_TRANSLATE_NOOP("ToolPalette", "任意角度・頂点モード"), true},
    {DrawingTool::Rectangle, "140601", QT_TRANSLATE_NOOP("ToolPalette", "四角ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "四角"), QT_TRANSLATE_NOOP("ToolPalette", "対角からコーナーモード"), true},
    {DrawingTool::Circle, "140701", QT_TRANSLATE_NOOP("ToolPalette", "円ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "円"), QT_TRANSLATE_NOOP("ToolPalette", "半径モード"), true},
    {DrawingTool::Polygon, "140801", QT_TRANSLATE_NOOP("ToolPalette", "多角形ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "多角形"), QT_TRANSLATE_NOOP("ToolPalette", "頂点モード"), true},
    {DrawingTool::Reshape, "141001", QT_TRANSLATE_NOOP("ToolPalette", "変形ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "変形"), "", false},
    {DrawingTool::Join, "141401", QT_TRANSLATE_NOOP("ToolPalette", "結合／合成ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "結合・合成"), "", false},
    {DrawingTool::Dimension, "141901", QT_TRANSLATE_NOOP("ToolPalette", "縦横寸法ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "縦横寸法"), "", false},
    {DrawingTool::Text, "142001", QT_TRANSLATE_NOOP("ToolPalette", "文字ツール"),
        QT_TRANSLATE_NOOP("ToolPalette", "文字"), "", false}
}};

/// @brief 指定ツールの静的定義を探す。未定義ならnullptrを返す。
/// @param tool 現在処理するツールの種類。
const ToolEntry *entry(DrawingTool tool)
{
    // found: 対応するツール定義の探索結果。末尾なら未定義。
    // item: 静的なツール定義表の1要素。表示名・種類・使用可否を参照する。
    // コールバック: 定義表のツール種類が、検索対象と一致する要素を選ぶ。
    const auto found = std::find_if(tools.begin(), tools.end(), [tool](const auto &item) { return item.tool == tool; });
    return found == tools.end() ? nullptr : &*found;
}

/// @brief 指定ツールのアイコンを複数のピクセルサイズで描画して返す。
/// @param tool 現在処理するツールの種類。
/// @param color アイコン描画に使用する、パレット由来の前景色。
QIcon toolIcon(DrawingTool tool, QColor color)
{
    // icon: 複数解像度の画像をまとめるツールアイコン。
    QIcon icon;
    // pixels: 生成するアイコン画像の一辺のピクセル数。
    for (int pixels : {16, 24, 32, 48, 64}) {
        // pixmap: 透明背景へアイコンを描く一時画像。
        QPixmap pixmap(pixels, pixels); pixmap.fill(Qt::transparent);
        // painter: 一時画像へツールアイコンを描くスタック上の描画コンテキスト。
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(pixels / 32.0, pixels / 32.0);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        switch (tool) {
        case DrawingTool::Selection:
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{{7, 4}, {7, 26}, {13, 21}, {17, 29}, {21, 27}, {17, 19}, {25, 18}});
            break;
        case DrawingTool::Pan: {
            // hand: パンツールの手形を表す描画パス。
            QPainterPath hand;
            hand.moveTo(9, 18); hand.lineTo(9, 8); hand.quadTo(12, 4, 14, 8);
            hand.lineTo(14, 15); hand.lineTo(14, 6); hand.quadTo(17, 2, 19, 6);
            hand.lineTo(19, 15); hand.lineTo(19, 9); hand.quadTo(22, 6, 24, 10);
            hand.lineTo(24, 16); hand.lineTo(26, 13); hand.quadTo(30, 13, 28, 19);
            hand.lineTo(23, 28); hand.lineTo(13, 28); hand.lineTo(5, 20);
            hand.quadTo(2, 14, 6, 16); hand.lineTo(9, 18); painter.drawPath(hand);
            break;
        }
        case DrawingTool::Line: painter.drawLine(6, 26, 26, 6); break;
        case DrawingTool::Rectangle: painter.drawRect(QRectF(5, 8, 22, 17)); break;
        case DrawingTool::Circle: painter.drawEllipse(QRectF(5, 5, 22, 22)); break;
        case DrawingTool::Polygon: painter.drawPolygon(QPolygonF{{5, 21}, {10, 5}, {24, 8}, {28, 24}, {15, 28}}); break;
        case DrawingTool::Reshape:
            painter.drawPolyline(QPolygonF{{5, 24}, {12, 8}, {26, 20}});
            painter.drawRect(QRectF(3, 22, 4, 4)); painter.drawRect(QRectF(10, 6, 4, 4));
            painter.drawRect(QRectF(24, 18, 4, 4)); break;
        case DrawingTool::Join:
            painter.drawLine(5, 25, 25, 5); painter.drawLine(6, 10, 26, 24);
            painter.drawEllipse(QRectF(13, 13, 5, 5)); break;
        case DrawingTool::Dimension:
            painter.drawLine(5, 7, 5, 25); painter.drawLine(27, 7, 27, 25);
            painter.drawLine(5, 14, 27, 14);
            painter.drawPolyline(QPolygonF{{10, 10}, {5, 14}, {10, 18}});
            painter.drawPolyline(QPolygonF{{22, 10}, {27, 14}, {22, 18}}); break;
        case DrawingTool::Text: {
            // font: 文字ツールのアイコン用に調整するフォント。
            QFont font = painter.font(); font.setBold(true); font.setPixelSize(27); painter.setFont(font);
            painter.drawText(QRectF(2, 1, 28, 30), Qt::AlignCenter, QStringLiteral("T")); break;
        }
        }
        painter.end(); icon.addPixmap(pixmap);
    }
    return icon;
}
}

/// @brief 基本ツールのアクションとボタンを作り、排他選択と再配置を接続する。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
ToolPalette::ToolPalette(QWidget *parent) : QWidget(parent)
{
    setObjectName("toolPalette");
    setMinimumWidth(100);
    // layout: 画面部品を縦方向に並べる、親ウィジェット所有のレイアウト。
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this); m_scroll->setObjectName("toolPaletteScroll");
    m_scroll->setWidgetResizable(true); m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scroll); m_content->setObjectName("toolPaletteContent");
    m_scroll->setWidget(m_content); layout->addWidget(m_scroll);
    m_hint = new QLabel(this); m_hint->setObjectName("toolPaletteHint");
    m_hint->setWordWrap(true); m_hint->setMargin(4);
    layout->addWidget(m_hint);
    m_group = new QActionGroup(this); m_group->setExclusive(true);
    // item: 静的なツール定義表の1要素。表示名・種類・使用可否を参照する。
    for (const auto &item : tools) {
        // action: 画面やツールの操作を表すアクション。所有権は所属するQtオブジェクトにある。
        auto *action = new QAction(toolIcon(item.tool, palette().color(QPalette::WindowText)), tr(item.button), m_group);
        action->setObjectName(QStringLiteral("tool_%1").arg(QString::fromLatin1(item.featureId)));
        action->setProperty("featureId", QString::fromLatin1(item.featureId));
        action->setProperty("drawingTool", QVariant::fromValue(item.tool));
        action->setCheckable(true); action->setEnabled(item.available);
        action->setToolTip(tr(item.name) + (!item.available ? tr("（未対応）")
            : QString()));
        // コールバック: 作成時のツール種別を値で保持し、クリックされたボタンのツールを選ぶ。
        connect(action, &QAction::triggered, this, [this, tool = item.tool] { setCurrentTool(tool); });
        // button: ツールに対応する表示ボタンまたはそのラベル。
        auto *button = new QToolButton(m_content);
        button->setDefaultAction(action); button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize({28, 28}); button->setFixedSize(80, 68);
        m_buttons.append(button);
    }
    toolAction(m_currentTool)->setChecked(true);
    m_scroll->viewport()->installEventFilter(this);
    scheduleLayout();
}

/// @brief パレット下部の操作案内を更新する。
/// @param text 表示または検証に使用する文字列。
void ToolPalette::setToolHint(const QString &text) { m_hint->setText(text); }

/// @brief 指定ツールに対応するアクションを返す。見つからない場合はnullptr。
/// @param tool 現在処理するツールの種類。
QAction *ToolPalette::toolAction(DrawingTool tool) const
{
    // action: 画面やツールの操作を表すアクション。所有権は所属するQtオブジェクトにある。
    for (auto *action : m_group->actions())
        if (action->property("drawingTool").value<DrawingTool>() == tool) return action;
    return nullptr;
}

/// @brief 使用可能なツールだけを排他的に選び、変化した場合に通知する。
/// @param tool 現在処理するツールの種類。
bool ToolPalette::setCurrentTool(DrawingTool tool)
{
    // action: 画面やツールの操作を表すアクション。所有権は所属するQtオブジェクトにある。
    auto *action = toolAction(tool);
    if (!action || !action->isEnabled()) return false;
    action->setChecked(true);
    if (m_currentTool != tool) { m_currentTool = tool; emit toolChanged(tool); }
    return true;
}

/// @brief ツールの表示名を現在の翻訳で返す。
/// @param tool 現在処理するツールの種類。
QString ToolPalette::toolName(DrawingTool tool)
{
    // item: 静的なツール定義表の1要素。表示名・種類・使用可否を参照する。
    const auto *item = entry(tool); return item ? tr(item->name) : QString();
}

/// @brief ツールの初期モードの表示文字列を返す。
/// @param tool 現在処理するツールの種類。
QString ToolPalette::modeText(DrawingTool tool)
{
    // item: 静的なツール定義表の1要素。表示名・種類・使用可否を参照する。
    const auto *item = entry(tool); return item ? tr(item->mode) : QString();
}

/// @brief スクロール領域のサイズ変更を検知して再配置を予約する。
/// @param watched サイズ変更を監視するイベント送信元オブジェクト。
/// @param event Qtから渡された入力または画面更新のイベント。
bool ToolPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Resize) scheduleLayout();
    return QWidget::eventFilter(watched, event);
}

/// @brief 再配置をイベントループへ1回だけ予約し、連続リサイズでの重複処理を防ぐ。
void ToolPalette::scheduleLayout()
{
    if (m_layoutPending) return;
    m_layoutPending = true;
    // コールバック: 再配置予約フラグを解除してから、最新のパレット幅でボタンを並べ直す。
    QTimer::singleShot(0, this, [this] { m_layoutPending = false; reflow(); });
}

/// @brief スクロールバーの有無も考慮し、パレット幅に合う列数へボタンを並べ直す。
void ToolPalette::reflow()
{
    // A tall one-column layout may currently show a scroll bar even though a
    // two-column layout would fit without it. Consider that wider viewport too.
    // maximum: スクロールバーがない場合に使えるビューポート寸法（px）。
    const QSize maximum = m_scroll->maximumViewportSize();
    // withoutScroll: スクロールバーなしで配置できる列数。
    const int withoutScroll = std::max(1, (maximum.width() - 4) / 84);
    // rowsWithoutScroll: スクロールバーなしの列数で必要になる行数。
    const int rowsWithoutScroll = (m_buttons.size() + withoutScroll - 1) / withoutScroll;
    // fitsWithoutScroll: ボタン全体がスクロールバーなしの高さに収まるかどうか。
    const bool fitsWithoutScroll = rowsWithoutScroll * 72 + 4 <= maximum.height();
    // available: スクロールバーの必要性を考慮した配置可能な幅（px）。
    const int available = fitsWithoutScroll ? maximum.width() : m_scroll->viewport()->width();
    // columns: 今回のパレット幅に適用する列数。
    const int columns = std::max(1, (available - 4) / 84);
    if (columns == m_columns) return;
    m_columns = columns;
    delete m_grid;
    m_grid = new QGridLayout(m_content);
    m_grid->setContentsMargins(4, 4, 4, 4); m_grid->setSpacing(4);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    // i: 頂点やボタンを走査するインデックス。
    for (int i = 0; i < m_buttons.size(); ++i) m_grid->addWidget(m_buttons[i], i / columns, i % columns);
    // rows: 現在の列数で全ボタンを表示するために必要な行数。
    const int rows = (m_buttons.size() + columns - 1) / columns;
    m_content->setMinimumHeight(rows * 72 + 4);
}
