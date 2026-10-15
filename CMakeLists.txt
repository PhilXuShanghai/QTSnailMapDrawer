cmake_minimum_required(VERSION 3.19)
project(QTSnailMapDrawer LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets  Core5Compat Network)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

qt_standard_project_setup()

qt_add_executable(QTSnailMapDrawer
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    mapview.cpp
    mapview.h
    mappointitem.cpp
    mappointitem.h
    mapdecorationitem.h
    mapdecorationitem.cpp
    maplineitem.cpp
    maplineitem.h
    mapwallitem.h
    mapgrounditem.h
    maproaditem.h
    maproaditem.cpp
    mapio.h
    mapio.cpp
    mapioexception.h
    mapmath.h
    addItemscommand.h addItemscommand.cpp
    snapshotcommand.h snapshotcommand.cpp
    mapscene.h mapscene.cpp
    res.qrc
    moveitemscommand.h moveitemscommand.cpp
    robotworker.h robotworker.cpp
    robotmanager.h robotmanager.cpp
    appconfig.h appconfig.cpp

)

target_include_directories(QTSnailMapDrawer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(QTSnailMapDrawer
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Core5Compat
        Qt6::Network
)

install(TARGETS QTSnailMapDrawer
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET QTSnailMapDrawer
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
