#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QPdfDocument>
#include <QPdfView>
#include <QVBoxLayout>
#include <QFileDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // PDFドキュメント
    QPdfDocument* document = new QPdfDocument(this);

    QString filePath = QFileDialog::getOpenFileName(
        this,
        "PDFを選択",
        "",
        "PDF Files (*.pdf)"
        );

    if (!filePath.isEmpty())
    {
        document->load(filePath);
    }

    // PDF読み込み
    //document->load("D:/D/Qt/PDFOpen/sample.pdf");

    // PDFビュー
    QPdfView* pdfView = new QPdfView(this);

    // ドキュメント設定
    pdfView->setDocument(document);

    // 中央Widget
    QWidget* central = new QWidget(this);

    // レイアウト
    QVBoxLayout* layout = new QVBoxLayout(central);

    layout->addWidget(pdfView);

    setCentralWidget(central);
}

MainWindow::~MainWindow()
{
    delete ui;
}
