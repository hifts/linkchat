# LinkChat/common/CMakeLists.txt

# 1. 设置最低版本要求 (好习惯)
cmake_minimum_required(VERSION 3.14)

# 2. 定义项目名称
project(LinkChatCommon)

# 3. 创建静态库
# 注意：指令中间必须是下划线 add_library，不能是空格
find_package(Qt5 REQUIRED COMPONENTS Core)

add_library(LinkChatCommon STATIC
    packet.h
    packet.cpp
    logger.h
)

target_link_libraries(LinkChatCommon PRIVATE Qt5::Core)

# 4. 暴露头文件路径
# 注意：
# (1) 指令是 target_include_directories (有下划线)
# (2) 变量必须用 ${} 包裹，且是大写加下划线 CMAKE_CURRENT_SOURCE_DIR
target_include_directories(LinkChatCommon PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
)
