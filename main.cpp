/**
 * @file main.cpp
 * @brief Qtアプリケーションとメインウィンドウを起動する。
 */

#include "mainwindow.h"

#include <QApplication>

/// @brief Qtのイベントループを開始し、終了後にスタック上のメイン画面を破棄する。
/// @param argc コマンドライン引数の数。Qtアプリケーションへ渡す。
/// @param argv コマンドライン引数の文字列配列。Qtアプリケーションへ渡す。
int main(int argc, char *argv[])
{
    // a: イベントループと画面オブジェクトを管理するQtアプリケーション。
    QApplication a(argc, argv);
    // w: スタック上のメイン画面。イベントループ終了後に破棄する。
    MainWindow w;
    w.show();
    return QApplication::exec();
}
