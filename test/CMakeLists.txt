add_executable(app main.c cutest/CuTest.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cutest)
target_link_libraries(app PRIVATE relationship)
