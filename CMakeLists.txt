cmake_minimum_required(VERSION 3.20)
project(OhMyDesktop VERSION 1.0.2 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(LayerShellQt REQUIRED)

set(SOURCES
    src/applicationicons.cpp
    src/desktopapplaunch.cpp
    src/dolphinintegration.cpp
    src/pagecarousel.cpp
    src/pagepager.cpp
    src/pagesettings.cpp
    src/pagecontrol.cpp
    src/desktopscene_pages.cpp
    src/desktoplayout.cpp
    src/trashmonitor.cpp
    src/filetransfer.cpp
    src/main.cpp
    src/desktopwindow.cpp
    src/desktopscene.cpp
    src/desktopscene_rename.cpp
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
    src/applicationicons.h
    src/desktopapplaunch.h
    src/desktoptextdialog.h
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
    Qt6::Network
    LayerShellQt::Interface
)

# Keep private Python helpers beside the real executable, also for staged installs.
target_compile_definitions(oh-my-desktop PRIVATE OMD_VERSION="${PROJECT_VERSION}")
set(OMD_PRIVATE_DIR "${CMAKE_INSTALL_LIBEXECDIR}/oh-my-desktop")
set(OMD_PRIVATE_FULL_DIR "${CMAKE_INSTALL_FULL_LIBEXECDIR}/oh-my-desktop")
configure_file(packaging/oh-my-desktop.in oh-my-desktop-launcher @ONLY)
configure_file(packaging/oh-my-desktop.service.in oh-my-desktop.service @ONLY)
install(TARGETS oh-my-desktop RUNTIME DESTINATION "${OMD_PRIVATE_DIR}")
install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/oh-my-desktop-launcher"
    DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME oh-my-desktop)
install(PROGRAMS src/desktop_pages.py DESTINATION "${CMAKE_INSTALL_BINDIR}" RENAME oh-my-desktop-pages)
install(FILES src/safe_transfer.py src/open_shortcut.py src/app_probe.py src/application_icon.py
    DESTINATION "${OMD_PRIVATE_DIR}")
install(FILES src/send_to_desktop.py DESTINATION "${OMD_PRIVATE_DIR}" RENAME send-to-desktop.py)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/oh-my-desktop.service"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/user")
install(FILES packaging/io.github.MrGlany.OhMyDesktop.desktop
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/oh-my-desktop")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")

configure_file(src/application_icon.py application_icon.py COPYONLY)
configure_file(src/app_probe.py app_probe.py COPYONLY)
configure_file(src/safe_transfer.py safe_transfer.py COPYONLY)
configure_file(src/open_shortcut.py open_shortcut.py COPYONLY)
configure_file(src/desktop_pages.py desktop_pages.py COPYONLY)
configure_file(src/send_to_desktop.py send-to-desktop.py COPYONLY)
enable_testing()
add_test(NAME installer_workflow COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_installer.py)
add_test(NAME application_icon_discovery COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_application_icon.py)
add_test(NAME application_availability COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_app_probe.py)
add_test(NAME file_safety COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_safe_transfer.py)
add_test(NAME unavailable_shortcuts COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_open_shortcut.py)
add_test(NAME desktop_links COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/test_desktop_links.py)
option(OMD_BUILD_DOLPHIN_CHECK "Build isolated KDE context menu lifecycle check" OFF)
if(OMD_BUILD_DOLPHIN_CHECK)
    find_package(KF6KIO REQUIRED)
    find_package(KF6Service REQUIRED)
    add_executable(dolphin-menu-check tests/dolphin_menu_check.cpp src/dolphinintegration.cpp)
    target_include_directories(dolphin-menu-check PRIVATE src)
    target_link_libraries(dolphin-menu-check PRIVATE Qt6::Widgets KF6::KIOWidgets KF6::Service)
    add_executable(dolphin-menu-presence tests/dolphin_menu_presence.cpp)
    target_link_libraries(dolphin-menu-presence PRIVATE Qt6::Widgets KF6::KIOWidgets)
    add_test(NAME dolphin_menu_lifecycle COMMAND ${CMAKE_COMMAND} -E env
        QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic
        XDG_DATA_HOME=${CMAKE_BINARY_DIR}/dolphin-check-data
        XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/dolphin-check-config
        $<TARGET_FILE:dolphin-menu-check>)
endif()
option(OMD_BUILD_LAUNCHER_CHECK "Build independent app launch check" OFF)
if(OMD_BUILD_LAUNCHER_CHECK)
    add_executable(launcher-check tests/launcher_check.cpp src/launcherutil.cpp)
    target_include_directories(launcher-check PRIVATE src)
    target_link_libraries(launcher-check PRIVATE Qt6::Core)
    add_test(NAME independent_app_launch COMMAND launcher-check)
endif()

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
    target_link_libraries(properties-check PRIVATE Qt6::Widgets Qt6::Network Qt6::GuiPrivate Qt6::Test LayerShellQt::Interface)
    add_test(NAME inline_rename COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_inline_rename_check.py $<TARGET_FILE:properties-check>)
    add_test(NAME primary_monitor_selection COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_primary_monitor_check.py $<TARGET_FILE:properties-check>)
    add_test(NAME native_app_launch COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_native_launch_check.py $<TARGET_FILE:properties-check>)
    add_test(NAME application_icons COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_application_icons_check.py $<TARGET_FILE:properties-check>)
    add_test(NAME missing_app_dialog COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_missing_app_check.py $<TARGET_FILE:properties-check>)
    add_test(NAME text_input_focus COMMAND ${CMAKE_COMMAND} -E env
        QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=generic
        XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/properties-check-focus-config
        $<TARGET_FILE:properties-check> --text-input-focus-check)
    if(OMD_BUILD_DOLPHIN_CHECK)
        target_compile_definitions(properties-check PRIVATE OMD_DOLPHIN_LAUNCH_CHECK)
        target_link_libraries(properties-check PRIVATE KF6::KIOWidgets KF6::Service)
        add_test(NAME desktop_pages COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_pages_check.py $<TARGET_FILE:properties-check> --private-dbus)
    else()
        add_test(NAME desktop_pages COMMAND /usr/bin/python3 ${CMAKE_SOURCE_DIR}/tests/run_pages_check.py $<TARGET_FILE:properties-check>)
    endif()
endif()
