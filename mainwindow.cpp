#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "drawingsettings.h"
#include "drawingview.h"
#include "scaledialog.h"
#include <QAction>
#include <QLabel>
#include <QPdfDocument>
#include <QPdfView>
#include <QFileDialog>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QToolBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(tr("StageCAD"));
    resize(1100, 800);

    m_drawingSettings = new DrawingSettings(this);
    m_drawingSettings->setObjectName("drawingSettings");
    auto *tabs = new QTabWidget(this);
    tabs->setObjectName("workspaceTabs");
    m_drawingView = new DrawingView(m_drawingSettings, tabs);
    tabs->addTab(m_drawingView, tr("作業領域"));

    // Keep the existing standalone PDF viewer as a reference. PDF placement and
    // calibration in CAD coordinates belong to the following roadmap task.
    auto *document = new QPdfDocument(this);
    document->setObjectName("pdfDocument");
    auto *pdfView = new QPdfView(tabs);
    pdfView->setObjectName("pdfReferenceView");
    pdfView->setDocument(document);
    tabs->addTab(pdfView, tr("PDF参照"));
    setCentralWidget(tabs);

    auto *toolbar = addToolBar(tr("作業領域"));
    toolbar->setObjectName("drawingToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    auto *scaleAction = toolbar->addAction(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("縮尺…"));
    scaleAction->setObjectName("editScaleAction");
    scaleAction->setToolTip(tr("作業領域の縮尺を設定します。図形の実寸は変わりません。"));
    connect(scaleAction, &QAction::triggered, this, &MainWindow::editScale);
    m_scaleLabel = new QLabel(toolbar);
    m_scaleLabel->setObjectName("drawingScaleLabel");
    toolbar->addWidget(m_scaleLabel);

    m_coordinates = new QLabel(this);
    m_coordinates->setObjectName("coordinatesLabel");
    m_paperCoverage = new QLabel(this);
    m_paperCoverage->setObjectName("paperCoverageLabel");
    statusBar()->addWidget(m_coordinates, 1);
    statusBar()->addPermanentWidget(m_paperCoverage);
    connect(m_drawingView, &DrawingView::cursorPositionChanged, this, [this](QPointF point) {
        m_coordinates->setText(tr("実寸座標  X: %1 mm  Y: %2 mm")
            .arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2));
    });
    connect(m_drawingView, &DrawingView::cursorLeft, this, [this] {
        m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this, scaleAction](int index) {
        scaleAction->setEnabled(index == 0);
        m_coordinates->setText(index == 0 ? tr("実寸座標  X: — mm  Y: — mm")
            : tr("PDF参照表示（作業領域の縮尺は適用されません）"));
        m_scaleLabel->setVisible(index == 0);
        m_paperCoverage->setVisible(index == 0);
    });
    connect(m_drawingSettings, &DrawingSettings::scaleChanged,
        this, &MainWindow::updateScaleDisplay);
    updateScaleDisplay();
    m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));

    QString filePath = QFileDialog::getOpenFileName(
        this,
        "PDFを選択",
        "",
        "PDF Files (*.pdf)"
        );

    if (!filePath.isEmpty())
    {
        document->load(filePath);
        tabs->setCurrentIndex(1);
    }
}

void MainWindow::editScale()
{
    ScaleDialog dialog(m_drawingSettings, this);
    if (dialog.exec() == QDialog::Accepted)
        m_drawingSettings->setScaleDenominator(dialog.scaleDenominator());
}

void MainWindow::updateScaleDisplay()
{
    const double denominator = m_drawingSettings->scaleDenominator();
    m_scaleLabel->setText(tr("  縮尺 1:%1").arg(denominator, 0, 'g', 15));
    const QSizeF realSize = m_drawingSettings->paperSizeMm() * denominator;
    m_paperCoverage->setText(tr("A4 / 実寸範囲 %1 × %2 mm / 原点: 用紙中心")
        .arg(realSize.width(), 0, 'g', 15).arg(realSize.height(), 0, 'g', 15));
}

MainWindow::~MainWindow()
{
    delete ui;
}
