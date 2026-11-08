cmake_minimum_required(VERSION 3.19)
project(PDFOpen LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Widgets
    Pdf
    PdfWidgets
)

qt_standard_project_setup()

# アプリ本体とGUI回帰テストで共用する、手書きの実装・宣言ファイル。
set(STAGECAD_SOURCES
    objectinfopanel.h
    objectinfopanel.cpp
    shapeselectionmodel.h
    shapeselectionmodel.cpp
    shapedata.h
    shapedata.cpp
    drawingdocument.h
    drawingdocument.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    drawingsettings.cpp
    drawingsettings.h
    drawingview.cpp
    drawingview.h
    scaledialog.cpp
    scaledialog.h
    pdfplacement.cpp
    pdfplacement.h
    pdfpageitem.cpp
    pdfpageitem.h
    pdfcalibrationdialog.cpp
    pdfcalibrationdialog.h
    drawingtool.h
    toolpalette.cpp
    toolpalette.h
)

qt_add_executable(PDFOpen
    WIN32 MACOSX_BUNDLE
    main.cpp
    ${STAGECAD_SOURCES}
)

target_link_libraries(PDFOpen
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Pdf
        Qt::PdfWidgets
)

install(TARGETS PDFOpen
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# deploy_scriptにはQtが生成する配布用スクリプトのパスが格納される。
qt_generate_deploy_app_script(
    TARGET PDFOpen
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# BUILD_TESTINGで検証用ターゲットの有無を切り替える。通常のアプリ実行ではOFFにできる。
include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    qt_add_executable(stagecad_tests
        tests/test_coordinates.cpp
        ${STAGECAD_SOURCES}
    )
    target_include_directories(stagecad_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(stagecad_tests PRIVATE
        SAMPLE_PDF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sample.pdf")
    target_link_libraries(stagecad_tests PRIVATE Qt::Core Qt::Widgets Qt::Pdf Qt::PdfWidgets Qt::Test)
    add_test(NAME stagecad_tests COMMAND stagecad_tests)
    set_tests_properties(stagecad_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(stagecad_pdf_tests
        tests/test_pdfcalibration.cpp
        ${STAGECAD_SOURCES}
    )
    target_include_directories(stagecad_pdf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(stagecad_pdf_tests PRIVATE
        SAMPLE_PDF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sample.pdf")
    target_link_libraries(stagecad_pdf_tests PRIVATE Qt::Core Qt::Widgets Qt::Pdf Qt::PdfWidgets Qt::Test)
    add_test(NAME stagecad_pdf_tests COMMAND stagecad_pdf_tests)
    set_tests_properties(stagecad_pdf_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(stagecad_palette_tests tests/test_toolpalette.cpp ${STAGECAD_SOURCES})
    target_include_directories(stagecad_palette_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(stagecad_palette_tests PRIVATE
        SAMPLE_PDF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sample.pdf")
    target_link_libraries(stagecad_palette_tests PRIVATE Qt::Core Qt::Widgets Qt::Pdf Qt::PdfWidgets Qt::Test)
    add_test(NAME stagecad_palette_tests COMMAND stagecad_palette_tests)
    set_tests_properties(stagecad_palette_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(stagecad_drawing_tests tests/test_drawing.cpp ${STAGECAD_SOURCES})
    target_include_directories(stagecad_drawing_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(stagecad_drawing_tests PRIVATE
        SAMPLE_PDF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sample.pdf")
    target_link_libraries(stagecad_drawing_tests PRIVATE Qt::Core Qt::Widgets Qt::Pdf Qt::PdfWidgets Qt::Test)
    add_test(NAME stagecad_drawing_tests COMMAND stagecad_drawing_tests)
    set_tests_properties(stagecad_drawing_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(stagecad_document_tests tests/test_document.cpp
        shapedata.cpp shapedata.h drawingdocument.cpp drawingdocument.h)
    target_include_directories(stagecad_document_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(stagecad_document_tests PRIVATE Qt::Core Qt::Gui Qt::Test)
    add_test(NAME stagecad_document_tests COMMAND stagecad_document_tests)
    set_tests_properties(stagecad_document_tests PROPERTIES TIMEOUT 30)

    # 選択状態の検証はQCoreApplicationのみで実行し、表示アイテムへ依存しないことも確認する。
    qt_add_executable(stagecad_selection_tests tests/test_selection.cpp
        shapeselectionmodel.cpp shapeselectionmodel.h
        shapedata.cpp shapedata.h drawingdocument.cpp drawingdocument.h)
    target_include_directories(stagecad_selection_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(stagecad_selection_tests PRIVATE Qt::Core Qt::Gui Qt::Test)
    add_test(NAME stagecad_selection_tests COMMAND stagecad_selection_tests)
    set_tests_properties(stagecad_selection_tests PROPERTIES TIMEOUT 30)

    # 情報表示はPDFに依存せず、図形と選択モデルの読み取りだけで検証する。
    qt_add_executable(stagecad_objectinfo_tests tests/test_objectinfo.cpp
        objectinfopanel.cpp objectinfopanel.h shapeselectionmodel.cpp shapeselectionmodel.h
        shapedata.cpp shapedata.h drawingdocument.cpp drawingdocument.h)
    target_include_directories(stagecad_objectinfo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(stagecad_objectinfo_tests PRIVATE Qt::Core Qt::Widgets Qt::Test)
    add_test(NAME stagecad_objectinfo_tests COMMAND stagecad_objectinfo_tests)
    set_tests_properties(stagecad_objectinfo_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)

    qt_add_executable(stagecad_shutdown_probe
        tests/shutdown_probe.cpp
        ${STAGECAD_SOURCES}
    )
    target_include_directories(stagecad_shutdown_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(stagecad_shutdown_probe PRIVATE
        SAMPLE_PDF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/sample.pdf")
    target_link_libraries(stagecad_shutdown_probe PRIVATE Qt::Core Qt::Widgets Qt::Pdf Qt::PdfWidgets)
    # scenarioは終了時の操作状態名。状態ごとに別プロセスで破棄処理を検証する。
    foreach(scenario IN ITEMS empty pdf calibrated dialog picking canceled floating-palette closed-palette drawing-draft selected-shapes moving-shapes floating-object-info closed-object-info)
        add_test(NAME shutdown_${scenario} COMMAND stagecad_shutdown_probe ${scenario})
        set_tests_properties(shutdown_${scenario} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
    endforeach()
endif()
