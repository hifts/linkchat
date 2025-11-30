cmake_minimum_required(VERSION 3.16)

project(Server VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 服务端只需要Core和Network，不需要Widgets
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Sql)

set(PROJECT_SOURCES
    main.cpp
    tcpserver.h tcpserver.cpp
    clientsocket.h clientsocket.cpp
    # 添加其他服务端源文件...
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Server
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    find_package(Qt5 REQUIRED COMPONENTS Core)

    add_executable(Server
        ${PROJECT_SOURCES}
        dbmanager.h dbmanager.cpp
    )
endif()

# 统一链接语句，适配Qt5/Qt6
target_link_libraries(Server PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Sql
)

# 如果需要链接公共模块，确保LinkChatCommon已定义
target_link_libraries(Server PRIVATE LinkChatCommon)
target_link_libraries(Server PRIVATE Qt5::Core)

# 服务端通常是控制台程序，移除图形界面相关设置
set_target_properties(Server PROPERTIES
    WIN32_EXECUTABLE FALSE  # 服务端应该是控制台程序
    MACOSX_BUNDLE FALSE     # 不要创建macOS应用包
)

include(GNUInstallDirs)
install(TARGETS Server
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Server)
endif()
