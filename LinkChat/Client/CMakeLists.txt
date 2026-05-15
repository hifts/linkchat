cmake_minimum_required(VERSION 3.16)

project(Client VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt和必要的模块
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network Core Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Core Concurrent)

set(PROJECT_SOURCES
    main.cpp
    networkmanager.h
    networkmanager.cpp
    logindialog.h logindialog.cpp logindialog.ui
    registerdialog.h registerdialog.cpp registerdialog.ui
    mainwindow.h mainwindow.cpp mainwindow.ui
    chat_ui/ChatData.h
    chat_ui/chatmodel.h chat_ui/chatmodel.cpp
    chat_ui/chatdelegate.h chat_ui/chatdelegate.cpp
    res.qrc
    chat_ui/contactdelegate.h chat_ui/contactdelegate.cpp
    filetransferthread.h filetransferthread.cpp
    filetransfersession.h filetransfersession.cpp
    filetransfermanager.h filetransfermanager.cpp
    filereceiver.h filereceiver.cpp
    groupdialog.h groupdialog.cpp groupdialog.ui
    heartbeatmanager.h heartbeatmanager.cpp
    reconnectmanager.h reconnectmanager.cpp
    transferstatemanager.h transferstatemanager.cpp
    reconnecttransfermanager.h reconnecttransfermanager.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Client
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(Client ${PROJECT_SOURCES})
endif()

# 链接模块
target_link_libraries(Client PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    LinkChatCommon              # 连接common里的协议
)

target_include_directories(Client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/chat_ui)

# 平台特定设置
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Client)
endif()

set_target_properties(Client PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Client
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Client)
endif()
