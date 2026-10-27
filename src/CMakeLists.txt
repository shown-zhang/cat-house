file(GLOB_RECURSE SRC_LIST CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

add_executable(cat-house ${SRC_LIST})
target_link_libraries(cat-house PRIVATE gene_lib relationship)
