#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "drawingsettings.h"
#include "drawingview.h"
#include "scaledialog.h"
#include "pdfcalibrationdialog.h"
#include "pdfpageitem.h"
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
QImage renderPage(QPdfDocument *document, int page, QString *error)
{
    const QSizeF points = document->pagePointSize(page);
    if (!std::isfinite(points.width()) || !std::isfinite(points.height())
        || points.width() <= 0 || points.height() <= 0) {
        if (error) *error = QObject::tr("PDFのページ寸法が不正です。");
        return {};
    }
    const double factor = std::min(2.0, 2400.0 / std::max(points.width(), points.height()));
    QImage image = document->render(page, QSize(std::max(1, qRound(points.width() * factor)),
                                               std::max(1, qRound(points.height() * factor))));
    if (image.isNull() && error) *error = QObject::tr("PDFのページを描画できませんでした。");
    return image;
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowTitle(tr("StageCAD"));
    resize(1100, 800);
    m_drawingSettings = new DrawingSettings(this);
    m_drawingSettings->setObjectName("drawingSettings");
    m_pdfPlacement = new PdfPlacement(this);
    m_pdfPlacement->setObjectName("pdfPlacement");
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("workspaceTabs");
    m_drawingView = new DrawingView(m_drawingSettings, m_tabs);
    m_tabs->addTab(m_drawingView, tr("作業領域"));
    m_drawingView->scene()->addItem(new PdfPageItem(m_pdfPlacement));
    connect(m_pdfPlacement, &PdfPlacement::placementChanged, this, [this] {
        m_drawingView->setContentBounds(m_pdfPlacement->boundsMm());
    });
    m_pdfView = new QPdfView(m_tabs);
    m_pdfView->setObjectName("pdfReferenceView");
    m_tabs->addTab(m_pdfView, tr("PDF参照"));
    setCentralWidget(m_tabs);

    auto *toolbar = addToolBar(tr("作業領域"));
    toolbar->setObjectName("drawingToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_importPdfAction = toolbar->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton), tr("PDF取り込み…"));
    m_importPdfAction->setObjectName("importPdfAction");
    m_importPdfAction->setToolTip(tr("現在のPDFを置き換えて作業領域に取り込みます。元のPDFファイルは変更しません。"));
    connect(m_importPdfAction, &QAction::triggered, this, &MainWindow::choosePdf);
    toolbar->addWidget(new QLabel(tr(" ページ "), toolbar));
    m_pdfPageInput = new QSpinBox(toolbar);
    m_pdfPageInput->setObjectName("pdfPageInput");
    m_pdfPageInput->setRange(1, 1);
    toolbar->addWidget(m_pdfPageInput);
    m_pdfPageCount = new QLabel(tr(" / — "), toolbar); toolbar->addWidget(m_pdfPageCount);
    connect(m_pdfPageInput, &QSpinBox::valueChanged, this, [this](int page) {
        QString error;
        if (!showPdfPage(page - 1, &error)) {
            QSignalBlocker block(m_pdfPageInput);
            m_pdfPageInput->setValue(m_pdfPage + 1);
            if (!error.isEmpty()) QMessageBox::warning(this, tr("ページ表示エラー"), error);
        }
    });
    m_calibratePdfAction = toolbar->addAction(tr("PDFの伸縮…"));
    m_calibratePdfAction->setObjectName("calibratePdfAction");
    connect(m_calibratePdfAction, &QAction::triggered, this, &MainWindow::editPdfCalibration);
    toolbar->addSeparator();
    m_scaleAction = toolbar->addAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("縮尺…"));
    m_scaleAction->setObjectName("editScaleAction");
    m_scaleAction->setToolTip(tr("作業領域の縮尺を設定します。図形やPDFの実寸は変わりません。"));
    connect(m_scaleAction, &QAction::triggered, this, &MainWindow::editScale);
    m_scaleLabel = new QLabel(toolbar);
    m_scaleLabel->setObjectName("drawingScaleLabel"); toolbar->addWidget(m_scaleLabel);
    toolbar->addSeparator();
    m_zoomOutAction = toolbar->addAction(tr("縮小")); m_zoomOutAction->setObjectName("zoomOutAction");
    m_zoomInAction = toolbar->addAction(tr("拡大")); m_zoomInAction->setObjectName("zoomInAction");
    m_fitPaperAction = toolbar->addAction(tr("用紙全体")); m_fitPaperAction->setObjectName("fitPaperAction");
    connect(m_zoomOutAction, &QAction::triggered, this, [this] {
        m_drawingView->setDisplayZoom(m_drawingView->displayZoom() / 1.25);
    });
    connect(m_zoomInAction, &QAction::triggered, this, [this] {
        m_drawingView->setDisplayZoom(m_drawingView->displayZoom() * 1.25);
    });
    connect(m_fitPaperAction, &QAction::triggered, this, [this] { m_drawingView->setDisplayZoom(1); });

    m_coordinates = new QLabel(this); m_coordinates->setObjectName("coordinatesLabel");
    m_paperCoverage = new QLabel(this); m_paperCoverage->setObjectName("paperCoverageLabel");
    statusBar()->addWidget(m_coordinates, 1); statusBar()->addPermanentWidget(m_paperCoverage);
    connect(m_drawingView, &DrawingView::cursorPositionChanged, this, [this](QPointF point) {
        m_coordinates->setText(tr("実寸座標  X: %1 mm  Y: %2 mm")
            .arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2));
    });
    connect(m_drawingView, &DrawingView::cursorLeft, this, [this] {
        m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        updatePdfActions();
        m_coordinates->setText(index == 0 ? tr("実寸座標  X: — mm  Y: — mm")
            : tr("PDF参照表示（作業領域の縮尺やPDFの補正は適用されません）"));
        m_scaleLabel->setVisible(index == 0); m_paperCoverage->setVisible(index == 0);
    });
    connect(m_drawingSettings, &DrawingSettings::scaleChanged, this, &MainWindow::updateScaleDisplay);
    connect(m_drawingView, &DrawingView::pickHintChanged, this, [this](const QString &text) { statusBar()->showMessage(text); });
    connect(m_drawingView, &DrawingView::distancePicked, this,
        [this](DrawingView::DistanceKind kind, QPointF first, QPointF second) {
            if (!m_pdfCalibrationDialog) return;
            if (kind == DrawingView::DistanceKind::Current)
                m_pdfCalibrationDialog->setCurrentMeasurement(first, second);
            else m_pdfCalibrationDialog->setTargetMeasurement(first, second);
            statusBar()->clearMessage();
            m_pdfCalibrationDialog->show(); m_pdfCalibrationDialog->raise(); m_pdfCalibrationDialog->activateWindow();
        });
    connect(m_drawingView, &DrawingView::distancePickCanceled, this, [this] {
        statusBar()->clearMessage();
        if (m_pdfCalibrationDialog) { m_pdfCalibrationDialog->show(); m_pdfCalibrationDialog->raise(); }
    });
    updateScaleDisplay(); updatePdfActions();
    m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));

    const QString path = QFileDialog::getOpenFileName(this, tr("PDFを選択"), {}, tr("PDF Files (*.pdf)"));
    if (!path.isEmpty()) {
        QString error;
        if (importPdf(path, &error)) m_tabs->setCurrentIndex(1); // Preserve the original startup reference display.
        else QMessageBox::warning(this, tr("PDFを開けません"), error);
    }
}

bool MainWindow::importPdf(const QString &path, QString *error)
{
    if (error) error->clear();
    if (m_pdfCalibrationDialog) {
        if (error) *error = tr("PDFの伸縮を完了またはキャンセルしてから取り込んでください。");
        return false;
    }
    auto next = std::make_unique<QPdfDocument>();
    const auto result = next->load(path);
    if (result != QPdfDocument::Error::None || next->status() != QPdfDocument::Status::Ready || next->pageCount() < 1) {
        if (error) *error = result == QPdfDocument::Error::IncorrectPassword
            ? tr("パスワード付きPDFには対応していません。") : tr("ファイルを開けません。PDF形式とファイルの存在を確認してください。");
        return false;
    }
    const QImage image = renderPage(next.get(), 0, error);
    if (image.isNull()) return false;
    const double denominator = m_drawingSettings->scaleDenominator();
    if (!m_pdfPlacement->setPage(image, next->pagePointSize(0), denominator)) {
        if (error) *error = tr("PDFの実寸サイズが描画可能な範囲を超えています。");
        return false;
    }
    m_pdfView->setDocument(nullptr);
    delete m_pdfDocument;
    next->setParent(this); m_pdfDocument = next.release();
    m_pdfDocument->setObjectName("pdfDocument"); m_pdfView->setDocument(m_pdfDocument);
    m_pdfPath = path; m_pdfPage = 0; m_importDenominator = denominator;
    m_pdfPageGeometry.clear();
    {
        QSignalBlocker block(m_pdfPageInput);
        m_pdfPageInput->setRange(1, m_pdfDocument->pageCount()); m_pdfPageInput->setValue(1);
    }
    m_pdfPageCount->setText(tr(" / %1 ").arg(m_pdfDocument->pageCount()));
    m_drawingView->clearDistancePick(); m_drawingView->setDisplayZoom(1);
    setWindowTitle(tr("%1 — StageCAD").arg(QFileInfo(path).fileName()));
    m_tabs->setCurrentIndex(0); updatePdfActions();
    return true;
}

void MainWindow::choosePdf()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("PDFを取り込む（現在のPDFを置換）"),
        m_pdfPath.isEmpty() ? QString() : QFileInfo(m_pdfPath).absolutePath(), tr("PDF Files (*.pdf)"));
    if (path.isEmpty()) return;
    QString error;
    if (!importPdf(path, &error)) QMessageBox::warning(this, tr("PDFを開けません"), error);
}

bool MainWindow::showPdfPage(int page, QString *error)
{
    if (error) error->clear();
    if (!m_pdfDocument || m_pdfCalibrationDialog || page < 0 || page >= m_pdfDocument->pageCount()) return false;
    if (page == m_pdfPage) return true;
    const QImage image = renderPage(m_pdfDocument, page, error);
    if (image.isNull()) return false;
    std::optional<PdfGeometry> saved;
    if (m_pdfPageGeometry.contains(page)) saved = m_pdfPageGeometry.value(page);
    const PdfGeometry previous = m_pdfPlacement->geometry();
    if (!m_pdfPlacement->setPage(image, m_pdfDocument->pagePointSize(page), m_importDenominator, saved)) {
        if (error) *error = tr("ページの実寸サイズが描画可能な範囲を超えています。");
        return false;
    }
    m_pdfPageGeometry.insert(m_pdfPage, previous); m_pdfPage = page;
    m_pdfView->pageNavigator()->jump(page, {});
    m_drawingView->clearDistancePick();
    updatePdfActions();
    return true;
}

void MainWindow::editPdfCalibration()
{
    if (!m_pdfPlacement->hasPage() || m_pdfCalibrationDialog) return;
    m_tabs->setCurrentIndex(0);
    auto *dialog = new PdfCalibrationDialog(this);
    m_pdfCalibrationDialog = dialog;
    auto startPick = [this, dialog](DrawingView::DistanceKind kind) {
        dialog->hide();
        m_drawingView->startDistancePick(kind, m_pdfPlacement->boundsMm());
        activateWindow(); m_drawingView->setFocus();
        statusBar()->showMessage(kind == DrawingView::DistanceKind::Current
            ? tr("PDF上の始点をクリックしてください。この1点目を固定します。Escで戻ります。")
            : tr("新しい距離の始点をクリックしてください。Escで戻ります。"));
    };
    connect(dialog, &PdfCalibrationDialog::measureCurrentRequested, this,
        [startPick] { startPick(DrawingView::DistanceKind::Current); });
    connect(dialog, &PdfCalibrationDialog::measureTargetRequested, this,
        [startPick] { startPick(DrawingView::DistanceKind::Target); });
    connect(dialog, &PdfCalibrationDialog::applyRequested, this, [this, dialog] {
        const auto fixed = dialog->fixedPointMm();
        if (!fixed || !m_pdfPlacement->calibrate(dialog->currentDistanceMm(), dialog->targetDistanceMm(), *fixed)) {
            dialog->setApplyError(tr("補正後のPDFが描画可能な範囲を超えます。距離を確認してください。"));
            return;
        }
        m_pdfPageGeometry.insert(m_pdfPage, m_pdfPlacement->geometry());
        dialog->accept();
    });
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        m_drawingView->clearDistancePick(); statusBar()->clearMessage();
        m_pdfCalibrationDialog.clear(); updatePdfActions(); dialog->deleteLater();
    });
    updatePdfActions(); dialog->show();
}

void MainWindow::updatePdfActions()
{
    const bool idle = !m_pdfCalibrationDialog;
    const bool workspace = m_tabs->currentIndex() == 0;
    m_importPdfAction->setEnabled(idle);
    m_calibratePdfAction->setEnabled(idle && workspace && m_pdfPlacement->hasPage());
    m_scaleAction->setEnabled(idle && workspace);
    m_pdfPageInput->setEnabled(idle && m_pdfDocument && m_pdfDocument->pageCount() > 1);
    m_tabs->tabBar()->setEnabled(idle);
    m_zoomInAction->setEnabled(workspace); m_zoomOutAction->setEnabled(workspace); m_fitPaperAction->setEnabled(workspace);
}

void MainWindow::editScale()
{
    ScaleDialog dialog(m_drawingSettings, this);
    if (dialog.exec() == QDialog::Accepted) m_drawingSettings->setScaleDenominator(dialog.scaleDenominator());
}

void MainWindow::updateScaleDisplay()
{
    const double denominator = m_drawingSettings->scaleDenominator();
    m_scaleLabel->setText(tr("  縮尺 1:%1").arg(denominator, 0, 'g', 15));
    const QSizeF realSize = m_drawingSettings->paperSizeMm() * denominator;
    m_paperCoverage->setText(tr("A4 / 実寸範囲 %1 × %2 mm / 原点: 用紙中心")
        .arg(realSize.width(), 0, 'g', 15).arg(realSize.height(), 0, 'g', 15));
}

MainWindow::~MainWindow() { delete ui; }
