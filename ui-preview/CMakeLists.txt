cmake_minimum_required(VERSION 3.21)
project(LogAnalyQtPreview VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
qt_add_executable(LogAnalyQtPreview main.cpp)
qt_add_resources(LogAnalyQtPreview preview PREFIX "/preview" FILES
    PreviewMain.qml GlassButton.qml GlassField.qml Glyph.qml PreviewData.js
    assets/glass-background.png assets/app-logo.png)
qt_add_resources(LogAnalyQtPreview icons PREFIX "/icons" BASE "../assets"
    FILES ../assets/materialdesignicons-webfont.ttf)
target_link_libraries(LogAnalyQtPreview PRIVATE Qt6::Quick Qt6::QuickControls2)
set_target_properties(LogAnalyQtPreview PROPERTIES MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER com.loganalyqt.visualpreview
    MACOSX_BUNDLE_BUNDLE_NAME "LogAnalyQt Preview")
enable_testing()
find_program(QML_TEST_RUNNER qmltestrunner HINTS "${Qt6_DIR}/../../../bin"
    /opt/homebrew/opt/qtdeclarative/bin)
if(QML_TEST_RUNNER)
    add_test(NAME preview_interactions COMMAND "${QML_TEST_RUNNER}"
        -input "${CMAKE_CURRENT_SOURCE_DIR}/tests" -platform offscreen)
    set_tests_properties(preview_interactions PROPERTIES ENVIRONMENT "QT_QUICK_CONTROLS_STYLE=Basic")
endif()
