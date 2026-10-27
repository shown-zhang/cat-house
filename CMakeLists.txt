cmake_minimum_required(VERSION 3.29)
project(cat-house C CXX)  # CXX 供 Dear ImGui 使用

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # 导出编译命令，供 clangd 使用

# 调试信息由 CMAKE_BUILD_TYPE=Debug 自动带上；警告选项只加在本项目代码上，
# 避免第三方源码的警告淹没输出

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 项目自身头文件目录
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# 产物目录：静态库进 lib，可执行程序进 bin
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory(third-lib)   # 第三方预编译库
add_subdirectory(src)         # 游戏主程序
add_subdirectory(test)        # 单元测试入口
