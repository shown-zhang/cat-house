# 单元测试直接编译 src 下的实现（排除入口 main.c），避免为测试再造一个库
file(GLOB_RECURSE GAME_SRC CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM GAME_SRC "${PROJECT_SOURCE_DIR}/src/main.c")

add_executable(cat-house-test main.c cutest/CuTest.c ${GAME_SRC})
target_include_directories(cat-house-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cutest)
target_link_libraries(cat-house-test PRIVATE gene_lib relationship)
