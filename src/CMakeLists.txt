cmake_minimum_required(VERSION 3.21)
project(dropterm VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
find_package(LayerShellQt REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(dropterm
    main.cpp
    vtermbridge.cpp
    ptyiface.cpp
)

# TextRender is exposed to QML via QML_ELEMENT, so it belongs to the module's
# SOURCES for type registration to be generated. Panel.qml is compiled into the
# binary at qrc:/qt/qml/dropterm/Panel.qml.
#
# OUTPUT_DIRECTORY is not cosmetic: the module's generated files default to a
# directory named after the URI, which here collides with the executable of the
# same name and makes the link fail with "cannot open output file: Is a
# directory". The resource path is derived from URI, not from this, so the QML
# still lands at qrc:/qt/qml/dropterm/Panel.qml.
qt_add_qml_module(dropterm
    URI dropterm
    VERSION 1.0
    QML_FILES Panel.qml SettingsWindow.qml
    SOURCES textrender.cpp textrender.h windowcontroller.h settings.cpp settings.h ipc.h
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/dropterm
)

target_link_libraries(dropterm PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    LayerShellQt::Interface
    PkgConfig::VTERM
    util           # forkpty(3)
)

install(TARGETS dropterm
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
