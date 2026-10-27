cmake_minimum_required(VERSION 3.29)
project(relationship C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # 新增编译命令导出

# 增加调试信息
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 添加包含目录
include_directories(
    ${PROJECT_SOURCE_DIR}/include
)

# 产物目录：在根目录统一设置，所有子目录都会继承
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_subdirectory(src relationship)
add_subdirectory(test relationship-test)



