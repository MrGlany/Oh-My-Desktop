cmake_minimum_required(VERSION 3.20)
project(OhMyDesktop VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(LayerShellQt REQUIRED)

set(SOURCES
    src/desktoplayout.cpp
    src/trashmonitor.cpp
    src/filetransfer.cpp
    src/main.cpp
    src/desktopwindow.cpp
    src/desktopscene.cpp
    src/desktopscene_grid.cpp
    src/desktopscene_dragdrop.cpp
    src/desktopscene_ipc.cpp
    src/desktopscene_externaldrag.cpp
    src/desktopscene_clipboard.cpp
    src/desktopicon.cpp
    src/exeiconutil.cpp
    src/launcherutil.cpp
    src/propertiesdialog.cpp
    src/filepropertiesdialog.cpp
    src/inlinemenu.cpp
)

set(HEADERS
    src/desktoplayout.h
    src/desktopwindow.h
    src/desktopscene.h
    src/desktopicon.h
    src/exeiconutil.h
    src/launcherutil.h
    src/propertiesdialog.h
    src/filepropertiesdialog.h
    src/inlinemenu.h
)

set(RESOURCES
    assets/resources.qrc
)

add_executable(oh-my-desktop ${SOURCES} ${HEADERS} ${RESOURCES})

target_include_directories(oh-my-desktop PRIVATE src)

target_link_libraries(oh-my-desktop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    LayerShellQt::Interface
)

# Permanent installation intentionally disabled in this experimental safety build.

configure_file(src/safe_transfer.py safe_transfer.py COPYONLY)
enable_testing()
add_test(NAME file_safety COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_safe_transfer.py)

option(OMD_BUILD_SMOKE "Build isolated Qt interaction harness" OFF)
if(OMD_BUILD_SMOKE)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    target_sources(oh-my-desktop PRIVATE src/smoketest.cpp)
    target_link_libraries(oh-my-desktop PRIVATE Qt6::Test)
    target_compile_definitions(oh-my-desktop PRIVATE OMD_BUILD_SMOKE)
endif()

option(OMD_BUILD_MENU_PREVIEW "Build offscreen menu visual check" OFF)
if(OMD_BUILD_MENU_PREVIEW)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(menu-preview tests/menu_preview.cpp src/inlinemenu.cpp src/inlinemenu.h)
    target_include_directories(menu-preview PRIVATE src)
    target_link_libraries(menu-preview PRIVATE Qt6::Widgets Qt6::Test)
endif()

option(OMD_BUILD_PROPERTIES_CHECK "Build isolated properties integration check" OFF)
if(OMD_BUILD_PROPERTIES_CHECK)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(PROPERTIES_SOURCES ${SOURCES})
    list(REMOVE_ITEM PROPERTIES_SOURCES src/main.cpp)
    add_executable(properties-check tests/properties_check.cpp ${PROPERTIES_SOURCES} ${HEADERS} ${RESOURCES})
    target_include_directories(properties-check PRIVATE src)
    target_link_libraries(properties-check PRIVATE Qt6::Widgets Qt6::GuiPrivate Qt6::Test LayerShellQt::Interface)
endif()
