cmake_minimum_required(VERSION 3.16)

project(LoadTester VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network)

add_executable(LoadTester
    main.cpp
)

add_executable(FileTransferRecoveryTest
    filetransfer_recovery_test.cpp
    ../Client/filetransfersession.h
    ../Client/filetransfersession.cpp
)

add_executable(FileTransferE2ETest
    filetransfer_e2e_test.cpp
)

target_link_libraries(LoadTester PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    LinkChatCommon
)

target_link_libraries(FileTransferRecoveryTest PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    LinkChatCommon
)

target_link_libraries(FileTransferE2ETest PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Network
    LinkChatCommon
)
