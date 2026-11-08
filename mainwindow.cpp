/**
 * @file mainwindow.cpp
 * @brief 作業領域・PDF参照・各ツールのUIと操作状態を統合する。
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "drawingsettings.h"
#include "drawingview.h"
#include "drawingdocument.h"
#include "shapeselectionmodel.h"
#include "objectinfopanel.h"
#include "navigationpanel.h"
#include "scaledialog.h"
#include "pdfcalibrationdialog.h"
#include "pdfpageitem.h"
#include "toolpalette.h"
#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMenuBar>
#include <QMenu>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QKeySequence>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
/// @brief PDFページを最大長辺2400pxの画像へ描画する。失敗時は空画像とエラーを返す。
/// @param document PDFページ情報・画像を取得する文書。所有権は呼び出し側またはメイン画面にある。
/// @param page 対象のPDFページ番号。モデルは0始まり、入力欄は1始まり。
/// @param error 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
QImage renderPage(QPdfDocument *document, int page, QString *error)
{
    // points: PDFページの幅・高さ（1pt=1/72インチ）。
    const QSizeF points = document->pagePointSize(page);
    if (!std::isfinite(points.width()) || !std::isfinite(points.height())
        || points.width() <= 0 || points.height() <= 0) {
        if (error) *error = QObject::tr("PDFのページ寸法が不正です。");
        return {};
    }
    // factor: PDFのポイント寸法から画像pxへの倍率。長辺を最大2400pxに制限する。
    const double factor = std::min(2.0, 2400.0 / std::max(points.width(), points.height()));
    // image: 描画または取り込みに使用するラスタ画像。
    QImage image = document->render(page, QSize(std::max(1, qRound(points.width() * factor)),
                                               std::max(1, qRound(points.height() * factor))));
    if (image.isNull() && error) *error = QObject::tr("PDFのページを描画できませんでした。");
    return image;
}
}

/// @brief 画面・各モデル・操作を接続し、起動時のPDF選択を行う。
/// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
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
    // コールバック: PDFの補正・ページ更新に合わせて、作業領域の表示可能範囲を更新する。
    connect(m_pdfPlacement, &PdfPlacement::placementChanged, this, [this] {
        m_drawingView->setContentBounds(m_pdfPlacement->boundsMm());
    });
    m_pdfView = new QPdfView(m_tabs);
    m_pdfView->setObjectName("pdfReferenceView");
    m_tabs->addTab(m_pdfView, tr("PDF参照"));
    setCentralWidget(m_tabs);

    // toolbar: メイン画面が所有する、PDF・縮尺・ズーム操作のツールバー。
    auto *toolbar = addToolBar(tr("作業領域"));
    toolbar->setObjectName("drawingToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // editMenu: 標準ショートカットと同じ履歴を操作する編集メニュー。
    auto *editMenu = menuBar()->addMenu(tr("編集"));
    m_undoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowBack),tr("取り消し"),this);
    m_redoAction = new QAction(style()->standardIcon(QStyle::SP_ArrowForward),tr("やり直し"),this);
    m_undoAction->setObjectName("undoAction"); m_redoAction->setObjectName("redoAction");
    m_undoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Undo));
    m_redoAction->setShortcuts(QKeySequence::keyBindings(QKeySequence::Redo));
    editMenu->addAction(m_undoAction); editMenu->addAction(m_redoAction);
    toolbar->addAction(m_undoAction); toolbar->addAction(m_redoAction); toolbar->addSeparator();
    // コールバック: 利用可能な場合だけ文書の確定操作を1件戻す。
    connect(m_undoAction,&QAction::triggered,this,[this] { m_drawingView->document()->undo(); });
    // コールバック: 利用可能な場合だけ戻した操作を1件やり直す。
    connect(m_redoAction,&QAction::triggered,this,[this] { m_drawingView->document()->redo(); });
    connect(m_drawingView->document(),&DrawingDocument::historyChanged,this,&MainWindow::updatePdfActions);
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
    // page: 対象のPDFページ番号。モデルは0始まり、入力欄は1始まり。
    // コールバック: 1始まりのページ入力を表示へ反映し、失敗時は元の入力番号へ戻す。
    connect(m_pdfPageInput, &QSpinBox::valueChanged, this, [this](int page) {
        // error: 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
        QString error;
        if (!showPdfPage(page - 1, &error)) {
            // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
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
    // コールバック: 作業領域の表示倍率だけを1段階下げる。
    connect(m_zoomOutAction, &QAction::triggered, this, [this] {
        m_drawingView->setDisplayZoom(m_drawingView->displayZoom() / 1.25);
    });
    // コールバック: 作業領域の表示倍率だけを1段階上げる。
    connect(m_zoomInAction, &QAction::triggered, this, [this] {
        m_drawingView->setDisplayZoom(m_drawingView->displayZoom() * 1.25);
    });
    // コールバック: 用紙中心と基準表示倍率に戻す。
    connect(m_fitPaperAction, &QAction::triggered, this, [this] { m_drawingView->resetView(); });

    // toolDock: メイン画面が所有する、基本ツールパレットのドック。
    auto *toolDock = new QDockWidget(tr("基本パレット"), this);
    toolDock->setObjectName("basicToolDock");
    toolDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    toolDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_toolPalette = new ToolPalette(toolDock);
    toolDock->setWidget(m_toolPalette);
    addDockWidget(Qt::LeftDockWidgetArea, toolDock);
    resizeDocks({toolDock}, {180}, Qt::Horizontal);
    // viewMenu: パレットの再表示を提供する表示メニュー。
    auto *viewMenu = menuBar()->addMenu(tr("表示"));
    viewMenu->addAction(toolDock->toggleViewAction());
    // infoDock: メイン画面が所有する、右列のオブジェクト情報パレット。
    auto *infoDock = new QDockWidget(tr("オブジェクト情報"), this);
    infoDock->setObjectName("objectInfoDock"); infoDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    infoDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    m_objectInfoPanel = new ObjectInfoPanel(m_drawingView->document(), m_drawingView->selectionModel(), infoDock);
    connect(m_objectInfoPanel,&ObjectInfoPanel::vertexHighlighted,m_drawingView,&DrawingView::highlightVertex);
    infoDock->setWidget(m_objectInfoPanel); addDockWidget(Qt::RightDockWidgetArea, infoDock);
    resizeDocks({infoDock}, {260}, Qt::Horizontal);
    viewMenu->addAction(infoDock->toggleViewAction());
    // navigationDock: オブジェクト情報の下に配置するレイヤーナビゲーション。
    auto *navigationDock = new QDockWidget(tr("ナビゲーション"),this);
    navigationDock->setObjectName("navigationDock"); navigationDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_navigationPanel = new NavigationPanel(m_drawingView->document(),m_drawingSettings,navigationDock);
    navigationDock->setWidget(m_navigationPanel); addDockWidget(Qt::RightDockWidgetArea,navigationDock);
    splitDockWidget(infoDock,navigationDock,Qt::Vertical);
    resizeDocks({infoDock,navigationDock},{440,240},Qt::Vertical);
    viewMenu->addAction(navigationDock->toggleViewAction());
    connect(m_drawingView->document(),&DrawingDocument::layersChanged,this,&MainWindow::updatePdfActions);


    addToolBarBreak(Qt::TopToolBarArea);
    m_toolOptionsToolbar = addToolBar(tr("ツール"));
    m_toolOptionsToolbar->setObjectName("toolOptionsToolbar");
    m_toolOptionsToolbar->setMovable(false);
    m_toolModeLabel = new QLabel(m_toolOptionsToolbar);
    m_toolModeLabel->setObjectName("activeToolModeLabel");
    // showMode: ツール名とモードの表示を更新するコールバック。
    // tool: 現在処理するツールの種類。
    // コールバック: 新しいツールの名前とモードをツールバーへ表示する。
    auto showMode = [this](DrawingTool tool) {
        m_toolModeLabel->setText(ToolPalette::toolName(tool) + QStringLiteral(" / ") + ToolPalette::modeText(tool));
    };
    m_toolOptionsToolbar->addWidget(m_toolModeLabel);
    m_pointInputPanel = new QWidget(m_toolOptionsToolbar);
    m_pointInputPanel->setObjectName("drawingPointPanel");
    // pointLayout: X/Y入力と点指定ボタンを横に並べるレイアウト。
    auto *pointLayout = new QHBoxLayout(m_pointInputPanel);
    pointLayout->setContentsMargins(8, 0, 0, 0); pointLayout->setSpacing(4);
    m_xPointInput = new QDoubleSpinBox(m_pointInputPanel);
    m_yPointInput = new QDoubleSpinBox(m_pointInputPanel);
    m_xPointInput->setObjectName("drawingXInput");
    m_yPointInput->setObjectName("drawingYInput");
    m_xPointInput->setPrefix(tr("X: ")); m_yPointInput->setPrefix(tr("Y: "));
    m_xPointInput->setAccessibleName(tr("X座標（実寸mm）"));
    m_yPointInput->setAccessibleName(tr("Y座標（実寸mm）"));
    // input: 数値やファイル名の入力欄。所有権は所属する画面にある。
    for (auto *input : {m_xPointInput, m_yPointInput}) {
        input->setRange(-10000000, 10000000); input->setDecimals(6);
        input->setSuffix(tr(" mm")); input->setKeyboardTracking(false);
        input->setButtonSymbols(QAbstractSpinBox::NoButtons);
        input->setMinimumWidth(130);
        pointLayout->addWidget(input);
    }
    m_addPointButton = new QPushButton(tr("点を指定"), m_pointInputPanel);
    m_addPointButton->setObjectName("submitDrawingPointButton"); pointLayout->addWidget(m_addPointButton);
    m_finishPolygonButton = new QPushButton(tr("多角形を完了"), m_pointInputPanel);
    m_finishPolygonButton->setObjectName("finishPolygonButton"); pointLayout->addWidget(m_finishPolygonButton);
    m_toolOptionsToolbar->addWidget(m_pointInputPanel);
    // コールバック: 有効なX/Y入力を実寸mmの1点として渡し、成功したら作業領域へフォーカスを戻す。
    connect(m_addPointButton, &QPushButton::clicked, this, [this] {
        if (!m_xPointInput->hasAcceptableInput() || !m_yPointInput->hasAcceptableInput()) return;
        m_xPointInput->interpretText(); m_yPointInput->interpretText();
        if (m_drawingView->submitDrawingPoint({m_xPointInput->value(), m_yPointInput->value()}))
            m_drawingView->setFocus();
    });
    // コールバック: 数値指定で作った多角形を閉じ、成功したら作業領域へフォーカスを戻す。
    connect(m_finishPolygonButton, &QPushButton::clicked, this, [this] {
        if (m_drawingView->finishPolygon()) m_drawingView->setFocus();
    });
    // tool: 現在処理するツールの種類。
    // コールバック: ツールの変更をビューへ反映し、拒否された場合はパレットの選択を戻す。
    connect(m_toolPalette, &ToolPalette::toolChanged, this, [this, showMode](DrawingTool tool) {
        if (m_pdfCalibrationDialog || !m_drawingView->setActiveTool(tool)) {
            // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
            QSignalBlocker block(m_toolPalette);
            m_toolPalette->setCurrentTool(m_drawingView->activeTool());
            return;
        }
        showMode(tool);
    });
    connect(m_drawingView, &DrawingView::activeToolChanged, m_toolPalette, &ToolPalette::setCurrentTool);
    connect(m_drawingView, &DrawingView::activeToolChanged, this, &MainWindow::updateDrawingControls);
    connect(m_drawingView, &DrawingView::drawingStateChanged, this, &MainWindow::updateDrawingControls);
    connect(m_drawingView, &DrawingView::drawingHintChanged, m_toolPalette, &ToolPalette::setToolHint);
    m_toolPalette->setToolHint(m_drawingView->drawingHint());
    showMode(m_toolPalette->currentTool());

    m_coordinates = new QLabel(this); m_coordinates->setObjectName("coordinatesLabel");
    m_paperCoverage = new QLabel(this); m_paperCoverage->setObjectName("paperCoverageLabel");
    statusBar()->addWidget(m_coordinates, 1); statusBar()->addPermanentWidget(m_paperCoverage);
    m_selectionStatus = new QLabel(this); m_selectionStatus->setObjectName("selectionStatusLabel");
    statusBar()->addPermanentWidget(m_selectionStatus);
    connect(m_drawingView->selectionModel(), &ShapeSelectionModel::selectionChanged,
            this, &MainWindow::updateSelectionDisplay);
    updateSelectionDisplay();
    // point: 用紙中心基準の実寸座標（mm）。
    // コールバック: カーソルの実寸X/Yを小数点以下2桁でステータス欄へ表示する。
    connect(m_drawingView, &DrawingView::cursorPositionChanged, this, [this](QPointF point) {
        m_coordinates->setText(tr("実寸座標  X: %1 mm  Y: %2 mm")
            .arg(point.x(), 0, 'f', 2).arg(point.y(), 0, 'f', 2));
    });
    // コールバック: カーソル退出時は座標が未指定であることを表示する。
    connect(m_drawingView, &DrawingView::cursorLeft, this, [this] {
        m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));
    });
    // index: コンボボックスまたはタブで選択されたインデックス。
    // コールバック: タブに合わせて操作可否と縮尺・座標表示の見せ方を切り替える。
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        updatePdfActions();
        m_coordinates->setText(index == 0 ? tr("実寸座標  X: — mm  Y: — mm")
            : tr("PDF参照表示（作業領域の縮尺やPDFの補正は適用されません）"));
        m_scaleLabel->setVisible(index == 0); m_paperCoverage->setVisible(index == 0);
    });
    connect(m_drawingSettings, &DrawingSettings::scaleChanged, this, &MainWindow::updateScaleDisplay);
    // text: 表示または検証に使用する文字列。
    // コールバック: 距離測定の案内をステータス欄へ表示する。
    connect(m_drawingView, &DrawingView::pickHintChanged, this, [this](const QString &text) { statusBar()->showMessage(text); });
    connect(m_drawingView, &DrawingView::distancePicked, this,
        // kind: 図形の種類、または現在距離／新しい距離の測定種別。
        // first: 用紙中心基準の実寸座標（mm）。
        // second: 用紙中心基準の実寸座標（mm）。
        // コールバック: 完了した2点測定を対応する入力欄へ渡し、補正ダイアログを再表示する。
        [this](DrawingView::DistanceKind kind, QPointF first, QPointF second) {
            if (!m_pdfCalibrationDialog) return;
            if (kind == DrawingView::DistanceKind::Current)
                m_pdfCalibrationDialog->setCurrentMeasurement(first, second);
            else m_pdfCalibrationDialog->setTargetMeasurement(first, second);
            statusBar()->clearMessage();
            m_pdfCalibrationDialog->show(); m_pdfCalibrationDialog->raise(); m_pdfCalibrationDialog->activateWindow();
        });
    // コールバック: 測定を中止したら案内を消し、補正ダイアログへ戻る。
    connect(m_drawingView, &DrawingView::distancePickCanceled, this, [this] {
        statusBar()->clearMessage();
        if (m_pdfCalibrationDialog) { m_pdfCalibrationDialog->show(); m_pdfCalibrationDialog->raise(); }
    });
    updateScaleDisplay(); updatePdfActions();
    m_coordinates->setText(tr("実寸座標  X: — mm  Y: — mm"));

    // path: 読み込むPDFのファイルパス。空なら選択を取り消す。
    const QString path = QFileDialog::getOpenFileName(this, tr("PDFを選択"), {}, tr("PDF Files (*.pdf)"));
    if (!path.isEmpty()) {
        // error: 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
        QString error;
        if (importPdf(path, &error)) m_tabs->setCurrentIndex(1); // Preserve the original startup reference display.
        else QMessageBox::warning(this, tr("PDFを開けません"), error);
    }
}

/// @brief PDFを検証・描画してから現在の文書を置き換える。失敗時は既存の表示・配置を保つ。
/// @param path 読み込むPDFのファイルパス。空なら選択を取り消す。
/// @param error 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
bool MainWindow::importPdf(const QString &path, QString *error)
{
    if (error) error->clear();
    if (m_pdfCalibrationDialog) {
        if (error) *error = tr("PDFの伸縮を完了またはキャンセルしてから取り込んでください。");
        return false;
    }
    // next: 読み込み成功が確認されるまで一時所有する、新しいPDF文書。
    auto next = std::make_unique<QPdfDocument>();
    // result: PDF読み込みの成否を表すQt PDFのエラー種別。
    const auto result = next->load(path);
    if (result != QPdfDocument::Error::None || next->status() != QPdfDocument::Status::Ready || next->pageCount() < 1) {
        if (error) *error = result == QPdfDocument::Error::IncorrectPassword
            ? tr("パスワード付きPDFには対応していません。") : tr("ファイルを開けません。PDF形式とファイルの存在を確認してください。");
        return false;
    }
    // image: 描画または取り込みに使用するラスタ画像。
    const QImage image = renderPage(next.get(), 0, error);
    if (image.isNull()) return false;
    // denominator: 縮尺1:Nの分母N。
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
        // block: 入力値の反映中にシグナルを止め、変更処理の循環を防ぐガード。
        QSignalBlocker block(m_pdfPageInput);
        m_pdfPageInput->setRange(1, m_pdfDocument->pageCount()); m_pdfPageInput->setValue(1);
    }
    m_pdfPageCount->setText(tr(" / %1 ").arg(m_pdfDocument->pageCount()));
    m_drawingView->clearDistancePick(); m_drawingView->resetView();
    setWindowTitle(tr("%1 — StageCAD").arg(QFileInfo(path).fileName()));
    m_tabs->setCurrentIndex(0); updatePdfActions();
    return true;
}

/// @brief ファイル選択からPDFを取り込み、失敗時は理由を表示する。
void MainWindow::choosePdf()
{
    // path: 読み込むPDFのファイルパス。空なら選択を取り消す。
    const QString path = QFileDialog::getOpenFileName(this, tr("PDFを取り込む（現在のPDFを置換）"),
        m_pdfPath.isEmpty() ? QString() : QFileInfo(m_pdfPath).absolutePath(), tr("PDF Files (*.pdf)"));
    if (path.isEmpty()) return;
    // error: 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
    QString error;
    if (!importPdf(path, &error)) QMessageBox::warning(this, tr("PDFを開けません"), error);
}

/// @brief ページ別の補正配置を保存・復元して表示ページを切り替える。失敗時は現在ページを保つ。
/// @param page 対象のPDFページ番号。モデルは0始まり、入力欄は1始まり。
/// @param error 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
bool MainWindow::showPdfPage(int page, QString *error)
{
    if (error) error->clear();
    if (!m_pdfDocument || m_pdfCalibrationDialog || page < 0 || page >= m_pdfDocument->pageCount()) return false;
    if (page == m_pdfPage) return true;
    // image: 描画または取り込みに使用するラスタ画像。
    const QImage image = renderPage(m_pdfDocument, page, error);
    if (image.isNull()) return false;
    // saved: 復元するページ配置。未指定なら取り込み時の縮尺から初期配置を計算する。
    std::optional<PdfGeometry> saved;
    if (m_pdfPageGeometry.contains(page)) saved = m_pdfPageGeometry.value(page);
    // previous: ページ切り替え前のPDF配置。ページ別キャッシュに保存する。
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

/// @brief PDFの距離補正画面を開き、測定・適用・取消と操作制限を接続する。
void MainWindow::editPdfCalibration()
{
    if (!m_pdfPlacement->hasPage() || m_pdfCalibrationDialog) return;
    m_tabs->setCurrentIndex(0);
    // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
    auto *dialog = new PdfCalibrationDialog(this);
    m_pdfCalibrationDialog = dialog;
    // startPick: 補正画面を隠して指定種類の距離測定を始めるコールバック。
    // kind: 図形の種類、または現在距離／新しい距離の測定種別。
    // コールバック: 補正画面を一時的に隠し、作業領域で指定種類の距離測定を始める。
    auto startPick = [this, dialog](DrawingView::DistanceKind kind) {
        dialog->hide();
        m_drawingView->startDistancePick(kind, m_pdfPlacement->boundsMm());
        activateWindow(); m_drawingView->setFocus();
        statusBar()->showMessage(kind == DrawingView::DistanceKind::Current
            ? tr("PDF上の始点をクリックしてください。この1点目を固定します。Escで戻ります。")
            : tr("新しい距離の始点をクリックしてください。Escで戻ります。"));
    };
    connect(dialog, &PdfCalibrationDialog::measureCurrentRequested, this,
        // コールバック: 現在距離の測定要求を、PDF内の2点指定へ切り替える。
        [startPick] { startPick(DrawingView::DistanceKind::Current); });
    connect(dialog, &PdfCalibrationDialog::measureTargetRequested, this,
        // コールバック: 新しい距離の取得要求を、作業領域の2点指定へ切り替える。
        [startPick] { startPick(DrawingView::DistanceKind::Target); });
    // コールバック: 固定点と距離を検証して補正し、成功した配置をページ別に保存して確定する。
    connect(dialog, &PdfCalibrationDialog::applyRequested, this, [this, dialog] {
        // fixed: 補正ダイアログから得た固定点。未測定なら空。
        const auto fixed = dialog->fixedPointMm();
        if (!fixed || !m_pdfPlacement->calibrate(dialog->currentDistanceMm(), dialog->targetDistanceMm(), *fixed)) {
            dialog->setApplyError(tr("補正後のPDFが描画可能な範囲を超えます。距離を確認してください。"));
            return;
        }
        m_pdfPageGeometry.insert(m_pdfPage, m_pdfPlacement->geometry());
        dialog->accept();
    });
    // コールバック: 測定状態を解除し、操作制限を戻してダイアログを遅延破棄する。
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        m_drawingView->clearDistancePick(); statusBar()->clearMessage();
        m_pdfCalibrationDialog.clear(); updatePdfActions(); dialog->deleteLater();
    });
    updatePdfActions(); dialog->show();
}

/// @brief 表示タブと補正中の状態から、PDF・描画・縮尺操作の使用可否を更新する。
void MainWindow::updatePdfActions()
{
    // idle: PDF補正ダイアログが開いておらず、通常操作を許可できる状態。
    const bool idle = !m_pdfCalibrationDialog;
    // workspace: CAD作業領域タブが現在選択されているかどうか。
    const bool workspace = m_tabs->currentIndex() == 0;
    m_drawingView->setDrawingEnabled(idle && workspace);
    m_selectionStatus->setVisible(workspace);
    m_objectInfoPanel->setEnabled(idle && workspace);
    m_navigationPanel->setEnabled(idle && workspace);
    updateDrawingControls();
    m_toolPalette->setEnabled(idle && workspace);
    m_toolOptionsToolbar->setEnabled(idle && workspace);
    m_undoAction->setEnabled(idle && workspace && m_drawingView->document()->canUndo());
    m_redoAction->setEnabled(idle && workspace && m_drawingView->document()->canRedo());
    m_importPdfAction->setEnabled(idle);
    m_calibratePdfAction->setEnabled(idle && workspace && m_pdfPlacement->hasPage());
    m_scaleAction->setEnabled(idle && workspace);
    m_pdfPageInput->setEnabled(idle && m_pdfDocument && m_pdfDocument->pageCount() > 1);
    m_tabs->tabBar()->setEnabled(idle);
    m_zoomInAction->setEnabled(workspace); m_zoomOutAction->setEnabled(workspace); m_fitPaperAction->setEnabled(workspace);
}

/// @brief 現在のツールと作成状態に応じて、座標入力・多角形完了ボタンを更新する。
void MainWindow::updateDrawingControls()
{
    // tool: 現在処理するツールの種類。
    const DrawingTool tool = m_drawingView->activeTool();
    // shapeTool: X/Y数値による点指定を使う描画ツールが選択されているかどうか。
    const bool shapeTool = tool == DrawingTool::Line || tool == DrawingTool::Rectangle
        || tool == DrawingTool::Circle || tool == DrawingTool::Polygon;
    m_pointInputPanel->setVisible(shapeTool);
    m_pointInputPanel->setEnabled(shapeTool && m_drawingView->document()->canCreateShape() && m_drawingView->drawingEnabled() && !m_drawingView->isPickingDistance());
    m_finishPolygonButton->setVisible(tool == DrawingTool::Polygon);
    m_finishPolygonButton->setEnabled(m_drawingView->canFinishPolygon());
}

/// @brief 選択モデルの現在値から、選択件数と単独図形の種類を表示する。
void MainWindow::updateSelectionDisplay()
{
    // selection: ビュー所有の選択モデル。表示アイテムへの参照は持ち込まない。
    const auto *selection = m_drawingView->selectionModel();
    // single: 単独選択の場合だけ取得できる、現在の図形データのコピー。
    const auto single = selection->singleShape();
    if (!single) {
        m_selectionStatus->setText(tr("選択: %1個").arg(selection->count()));
        return;
    }
    // name: 単独選択している図形の種類を示す表示文字列。
    QString name;
    switch (single->kind()) {
    case ShapeKind::Line: name = tr("直線"); break;
    case ShapeKind::Rectangle: name = tr("四角"); break;
    case ShapeKind::Circle: name = tr("円"); break;
    case ShapeKind::Polygon: name = tr("多角形"); break;
    }
    m_selectionStatus->setText(tr("選択: %1（1個）").arg(name));
}

/// @brief 縮尺ダイアログを開き、OKで確定された場合だけ作業領域の設定へ反映する。
void MainWindow::editScale()
{
    // dialog: 操作対象のダイアログ。Qtの親子関係またはスタックで寿命を管理する。
    // target: ダイアログを開いた時点の対象UUID。別のレイヤーへ誤適用しない。
    const auto target = m_drawingView->document()->activeLayerId();
    ScaleDialog dialog(m_drawingSettings, this);
    dialog.setWindowTitle(tr("縮尺 — %1").arg(m_drawingView->document()->layer(target)->name));
    if (dialog.exec() == QDialog::Accepted) m_drawingView->document()->setLayerScale(target,dialog.scaleDenominator());
}

/// @brief 現在の縮尺と、用紙が覆う実寸mmの範囲を表示する。
void MainWindow::updateScaleDisplay()
{
    // denominator: 縮尺1:Nの分母N。
    const double denominator = m_drawingSettings->scaleDenominator();
    m_scaleLabel->setText(tr("  縮尺 1:%1").arg(denominator, 0, 'g', 15));
    // realSize: 用紙またはPDFが覆う実寸幅・高さ（mm）。
    const QSizeF realSize = m_drawingSettings->paperSizeMm() * denominator;
    m_paperCoverage->setText(tr("A4 / 実寸範囲 %1 × %2 mm / 原点: 用紙中心")
        .arg(realSize.width(), 0, 'g', 15).arg(realSize.height(), 0, 'g', 15));
}

/// @brief 頂点表示の接続を切り離してUI補助オブジェクトを解放する。子QObjectはQtが破棄する。
MainWindow::~MainWindow()
{
    // 子ビューの派生クラス破棄後にモデル破棄通知から頂点マーカーを操作させない。
    disconnect(m_objectInfoPanel,nullptr,m_drawingView,nullptr);
    // インライン編集の終了が子の破棄中に発生しても、破棄済みのMainWindowへ通知しない。
    disconnect(m_drawingView->document(),nullptr,this,nullptr);
    delete ui;
}
