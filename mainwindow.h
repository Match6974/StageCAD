/**
 * @file mainwindow.h
 * @brief 作業領域・PDF参照・各ツールのUIと操作状態を統合する。
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QHash>
#include <QPointer>
#include "pdfplacement.h"

class DrawingSettings;
class DrawingView;
class QLabel;
class QAction;
class QPdfDocument;
class QPdfView;
class QSpinBox;
class QTabWidget;
class PdfCalibrationDialog;
class ToolPalette;
class ObjectInfoPanel;
class QToolBar;
class QDoubleSpinBox;
class QPushButton;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

/// @brief 子QObjectとして画面・設定・PDFを管理し、操作の競合を防ぐメイン画面。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// @brief 画面・各モデル・操作を接続し、起動時のPDF選択を行う。
    /// @param parent Qtの親オブジェクト。親子関係で寿命を管理し、nullptrなら親を設定しない。
    explicit MainWindow(QWidget *parent = nullptr);
    /// @brief Designer生成のUI補助オブジェクトを解放する。子QObjectはQtの親子関係で破棄される。
    ~MainWindow() override;
    /// @brief PDFを検証・描画してから現在の文書を置き換える。失敗時は既存の表示・配置を保つ。
    /// @param path 読み込むPDFのファイルパス。空なら選択を取り消す。
    /// @param error 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
    bool importPdf(const QString &path, QString *error = nullptr);

private:
    /// @brief 縮尺ダイアログを開き、OKで確定された場合だけ作業領域の設定へ反映する。
    void editScale();
    /// @brief 現在の縮尺と、用紙が覆う実寸mmの範囲を表示する。
    void updateScaleDisplay();
    /// @brief ファイル選択からPDFを取り込み、失敗時は理由を表示する。
    void choosePdf();
    /// @brief ページ別の補正配置を保存・復元して表示ページを切り替える。失敗時は現在ページを保つ。
    /// @param page 対象のPDFページ番号。モデルは0始まり、入力欄は1始まり。
    /// @param error 失敗理由を受け渡す文字列。ポインタ引数の場合はnullptrなら書き込まない。
    bool showPdfPage(int page, QString *error);
    /// @brief PDFの距離補正画面を開き、測定・適用・取消と操作制限を接続する。
    void editPdfCalibration();
    /// @brief 表示タブと補正中の状態から、PDF・描画・縮尺操作の使用可否を更新する。
    void updatePdfActions();
    /// @brief 現在のツールと作成状態に応じて、座標入力・多角形完了ボタンを更新する。
    void updateDrawingControls();
    /// @brief 選択モデルから件数・単独選択の種類を読み取り、状態欄を更新する。
    void updateSelectionDisplay();

    // ui: Qt Designer生成のUI補助オブジェクト。MainWindowのデストラクタで解放する。
    Ui::MainWindow *ui;
    // m_drawingSettings: メイン画面が子QObjectとして所有する作業領域の設定。
    DrawingSettings *m_drawingSettings;
    // m_drawingView: タブが所有するCAD作業領域のビュー。
    DrawingView *m_drawingView;
    // m_scaleLabel: 現在の縮尺1:Nを表示するラベル。親ツールバーが所有する。
    QLabel *m_scaleLabel;
    // m_coordinates: カーソルの実寸座標を表示するラベル。メイン画面が所有する。
    QLabel *m_coordinates;
    // m_selectionStatus: メイン画面が所有する、現在の選択数と種類を示すラベル。
    QLabel *m_selectionStatus;
    // m_paperCoverage: 用紙が覆う実寸mm範囲を表示するラベル。メイン画面が所有する。
    QLabel *m_paperCoverage;
    // m_pdfPlacement: メイン画面が子QObjectとして所有するPDF配置モデル。
    PdfPlacement *m_pdfPlacement;
    // m_pdfDocument: 読み込み済みのPDF文書。親はメイン画面で、文書置換時は明示的に解放する。
    QPdfDocument *m_pdfDocument = nullptr;
    // m_pdfView: タブが所有する、補正を適用しないPDF参照ビュー。
    QPdfView *m_pdfView;
    // m_tabs: CAD作業領域とPDF参照を切り替えるタブ。メイン画面が所有する。
    QTabWidget *m_tabs;
    // m_scaleAction: 作業領域の縮尺設定を開くアクション。
    QAction *m_scaleAction;
    // m_undoAction: 編集メニューと表示バーで共有する、文書操作を戻すアクション。
    QAction *m_undoAction;
    // m_redoAction: 戻した文書操作をやり直す共有アクション。
    QAction *m_redoAction;
    // m_importPdfAction: PDFの選択・取り込みを開始するアクション。
    QAction *m_importPdfAction;
    // m_calibratePdfAction: PDF距離補正ダイアログを開くアクション。
    QAction *m_calibratePdfAction;
    // m_zoomInAction: 作業領域の表示倍率を上げるアクション。
    QAction *m_zoomInAction;
    // m_zoomOutAction: 作業領域の表示倍率を下げるアクション。
    QAction *m_zoomOutAction;
    // m_fitPaperAction: 用紙中心と基準ズームに戻すアクション。
    QAction *m_fitPaperAction;
    // m_pdfPageInput: 表示するPDFページを1始まりで指定する入力欄。親ツールバーが所有する。
    QSpinBox *m_pdfPageInput;
    // m_pdfPageCount: PDFの総ページ数を表示するラベル。
    QLabel *m_pdfPageCount;
    // m_pdfCalibrationDialog: 開いている補正ダイアログへの弱い参照。破棄時に自動で空になる。
    QPointer<PdfCalibrationDialog> m_pdfCalibrationDialog;
    // m_pdfPageGeometry: 0始まりのページ番号に対応する、補正済みPDF配置のキャッシュ。
    QHash<int, PdfGeometry> m_pdfPageGeometry;
    // m_pdfPage: 現在表示しているPDFページ番号（0始まり）。
    int m_pdfPage = 0;
    // m_importDenominator: PDF取り込み時の縮尺分母。ページ切り替えでも同じ実寸換算を使う。
    double m_importDenominator = 1;
    // m_pdfPath: 現在取り込んでいるPDFのファイルパス。
    QString m_pdfPath;
    // m_toolPalette: 基本ドックが所有する、描画・選択ツールのパレット。
    ToolPalette *m_toolPalette;
    // m_objectInfoPanel: 右側ドックが所有する、選択図形のプロパティ表示と直線・四角形・円の数値編集。
    ObjectInfoPanel *m_objectInfoPanel;
    // m_toolModeLabel: 選択ツールとモードを表示するラベル。親ツールバーが所有する。
    QLabel *m_toolModeLabel;
    // m_toolOptionsToolbar: ツールのモードと座標入力欄を置く、メイン画面所有のツールバー。
    QToolBar *m_toolOptionsToolbar;
    // m_pointInputPanel: ツールバーが所有するX/Y座標入力と点指定ボタンのコンテナ。
    QWidget *m_pointInputPanel;
    // m_xPointInput: X座標を実寸mmで指定する入力欄。親パネルが所有する。
    QDoubleSpinBox *m_xPointInput;
    // m_yPointInput: Y座標を実寸mmで指定する入力欄。親パネルが所有する。
    QDoubleSpinBox *m_yPointInput;
    // m_addPointButton: 数値入力されたX/Yを1点として渡すボタン。親パネルが所有する。
    QPushButton *m_addPointButton;
    // m_finishPolygonButton: 数値指定でも多角形を閉じられる完了ボタン。親パネルが所有する。
    QPushButton *m_finishPolygonButton;
};
#endif // MAINWINDOW_H
