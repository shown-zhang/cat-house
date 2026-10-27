# 预编译第三方静态库。
# IMPORTED 目标不参与构建，只把库文件和头文件目录传递给链接方；
# GLOBAL 使其在 src / test 目录中也能被 link 到。

add_library(gene_lib STATIC IMPORTED GLOBAL)
set_target_properties(gene_lib PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/libgene-lib.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)

# libgene-lib.a 内部用到 logf / roundf / sqrtf
if(NOT MSVC)
    set_property(TARGET gene_lib APPEND PROPERTY INTERFACE_LINK_LIBRARIES m)
endif()

add_library(relationship STATIC IMPORTED GLOBAL)
set_target_properties(relationship PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/librelationship.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}"
)
