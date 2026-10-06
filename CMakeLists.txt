cmake_minimum_required(VERSION 3.25)

project(
    TCGPrintNative
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(
    Qt6 6.11 REQUIRED
    COMPONENTS
        Core
        Gui
        Qml
        Quick
        QuickControls2
        Test
)

qt_standard_project_setup()

qt_add_executable(tcgprint
    app/main.cpp
    src/app/AppInfo.cpp
    src/app/AppInfo.h
)

qt_add_qml_module(tcgprint
    URI TCGPrint
    VERSION 1.0
    QML_FILES
        app/qml/App.qml
)

target_include_directories(tcgprint
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

set_target_properties(tcgprint PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

enable_testing()

qt_add_executable(tcgprint_tests
    tests/AppInfoTest.cpp
    src/app/AppInfo.cpp
    src/app/AppInfo.h
)

target_include_directories(tcgprint_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tcgprint_tests
    PRIVATE
        Qt6::Core
        Qt6::Test
)

add_test(
    NAME tcgprint_tests
    COMMAND tcgprint_tests
)
