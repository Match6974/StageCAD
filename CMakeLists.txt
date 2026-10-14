cmake_minimum_required(VERSION 3.19)
project(PDFOpen LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Widgets
    Pdf
    PdfWidgets
)

qt_standard_project_setup()

qt_add_executable(PDFOpen
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
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

qt_generate_deploy_app_script(
    TARGET PDFOpen
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
