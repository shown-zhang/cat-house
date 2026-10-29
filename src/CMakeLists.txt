file(GLOB_RECURSE SRC_LIST CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(cat-house ${SRC_LIST})

target_compile_options(cat-house PRIVATE -Wall -Wextra)

target_link_libraries(cat-house PRIVATE
    gene_lib        # 基因生成 / 表型
    relationship    # 族谱（后续接入）
    SDL2::SDL2      # 窗口 / 事件 / OpenGL 上下文
    glad            # OpenGL 4.5 core
    cglm            # 数学
)
