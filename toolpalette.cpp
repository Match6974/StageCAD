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
struct ToolEntry {
    DrawingTool tool;
    const char *featureId;
    const char *name;
    const char *button;
    const char *mode;
    bool available;
};

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

const ToolEntry *entry(DrawingTool tool)
{
    const auto found = std::find_if(tools.begin(), tools.end(), [tool](const auto &item) { return item.tool == tool; });
    return found == tools.end() ? nullptr : &*found;
}

QIcon toolIcon(DrawingTool tool, QColor color)
{
    QIcon icon;
    for (int pixels : {16, 24, 32, 48, 64}) {
        QPixmap pixmap(pixels, pixels); pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap); painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(pixels / 32.0, pixels / 32.0);
        painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        switch (tool) {
        case DrawingTool::Selection:
            painter.setBrush(color);
            painter.drawPolygon(QPolygonF{{7, 4}, {7, 26}, {13, 21}, {17, 29}, {21, 27}, {17, 19}, {25, 18}});
            break;
        case DrawingTool::Pan: {
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
            QFont font = painter.font(); font.setBold(true); font.setPixelSize(27); painter.setFont(font);
            painter.drawText(QRectF(2, 1, 28, 30), Qt::AlignCenter, QStringLiteral("T")); break;
        }
        }
        painter.end(); icon.addPixmap(pixmap);
    }
    return icon;
}
}

ToolPalette::ToolPalette(QWidget *parent) : QWidget(parent)
{
    setObjectName("toolPalette");
    setMinimumWidth(100);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    m_scroll = new QScrollArea(this); m_scroll->setObjectName("toolPaletteScroll");
    m_scroll->setWidgetResizable(true); m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget(m_scroll); m_content->setObjectName("toolPaletteContent");
    m_scroll->setWidget(m_content); layout->addWidget(m_scroll);
    m_hint = new QLabel(this); m_hint->setObjectName("toolPaletteHint");
    m_hint->setWordWrap(true); m_hint->setMargin(4);
    layout->addWidget(m_hint);
    m_group = new QActionGroup(this); m_group->setExclusive(true);
    for (const auto &item : tools) {
        auto *action = new QAction(toolIcon(item.tool, palette().color(QPalette::WindowText)), tr(item.button), m_group);
        action->setObjectName(QStringLiteral("tool_%1").arg(QString::fromLatin1(item.featureId)));
        action->setProperty("featureId", QString::fromLatin1(item.featureId));
        action->setProperty("drawingTool", QVariant::fromValue(item.tool));
        action->setCheckable(true); action->setEnabled(item.available);
        const bool modeOnly = item.tool == DrawingTool::Selection;
        action->setToolTip(tr(item.name) + (!item.available ? tr("（未対応）")
            : modeOnly ? tr("（現在はモード切替のみ）") : QString()));
        connect(action, &QAction::triggered, this, [this, tool = item.tool] { setCurrentTool(tool); });
        auto *button = new QToolButton(m_content);
        button->setDefaultAction(action); button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        button->setIconSize({28, 28}); button->setFixedSize(80, 68);
        m_buttons.append(button);
    }
    toolAction(m_currentTool)->setChecked(true);
    m_scroll->viewport()->installEventFilter(this);
    scheduleLayout();
}

void ToolPalette::setToolHint(const QString &text) { m_hint->setText(text); }

QAction *ToolPalette::toolAction(DrawingTool tool) const
{
    for (auto *action : m_group->actions())
        if (action->property("drawingTool").value<DrawingTool>() == tool) return action;
    return nullptr;
}

bool ToolPalette::setCurrentTool(DrawingTool tool)
{
    auto *action = toolAction(tool);
    if (!action || !action->isEnabled()) return false;
    action->setChecked(true);
    if (m_currentTool != tool) { m_currentTool = tool; emit toolChanged(tool); }
    return true;
}

QString ToolPalette::toolName(DrawingTool tool)
{
    const auto *item = entry(tool); return item ? tr(item->name) : QString();
}

QString ToolPalette::modeText(DrawingTool tool)
{
    const auto *item = entry(tool); return item ? tr(item->mode) : QString();
}

bool ToolPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Resize) scheduleLayout();
    return QWidget::eventFilter(watched, event);
}

void ToolPalette::scheduleLayout()
{
    if (m_layoutPending) return;
    m_layoutPending = true;
    QTimer::singleShot(0, this, [this] { m_layoutPending = false; reflow(); });
}

void ToolPalette::reflow()
{
    // A tall one-column layout may currently show a scroll bar even though a
    // two-column layout would fit without it. Consider that wider viewport too.
    const QSize maximum = m_scroll->maximumViewportSize();
    const int withoutScroll = std::max(1, (maximum.width() - 4) / 84);
    const int rowsWithoutScroll = (m_buttons.size() + withoutScroll - 1) / withoutScroll;
    const bool fitsWithoutScroll = rowsWithoutScroll * 72 + 4 <= maximum.height();
    const int available = fitsWithoutScroll ? maximum.width() : m_scroll->viewport()->width();
    const int columns = std::max(1, (available - 4) / 84);
    if (columns == m_columns) return;
    m_columns = columns;
    delete m_grid;
    m_grid = new QGridLayout(m_content);
    m_grid->setContentsMargins(4, 4, 4, 4); m_grid->setSpacing(4);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    for (int i = 0; i < m_buttons.size(); ++i) m_grid->addWidget(m_buttons[i], i / columns, i % columns);
    const int rows = (m_buttons.size() + columns - 1) / columns;
    m_content->setMinimumHeight(rows * 72 + 4);
}
