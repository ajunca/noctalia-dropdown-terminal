cmake_minimum_required(VERSION 3.19)
project(dropterm VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

add_library(dropterm MODULE
    plugin.cpp
    textrender.cpp
    vtermbridge.cpp
    ptyiface.cpp
)

target_link_libraries(dropterm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    PkgConfig::VTERM
    util
)

set(QML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dropterm")

# Generate qmldir
file(WRITE "${QML_OUTPUT_DIR}/qmldir" "module dropterm\nplugin dropterm\n")

# Copy plugin to output dir after build
add_custom_command(TARGET dropterm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:dropterm> "${QML_OUTPUT_DIR}/"
)

install(
    DIRECTORY "${QML_OUTPUT_DIR}/"
    DESTINATION "lib/qt-6/qml/dropterm"
)
