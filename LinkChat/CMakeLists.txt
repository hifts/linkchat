# LinkChat/CMakeLists.txt

cmake_minimum_required(VERSION 3.14)

# 项目名称
project(LinkChat LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 全局开启 Qt 的自动生成机制 (Moc, Uic, Rcc)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 设置输出目录
# 让编译出来的 Server.exe 和 Client.exe 都跑到同一个 bin 目录下，方便调试
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 添加子目录
# 顺序很重要：common 被别人依赖，所以要先加
add_subdirectory(common)
add_subdirectory(Server)
add_subdirectory(Client)

# 添加测试目录（如果存在）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()
