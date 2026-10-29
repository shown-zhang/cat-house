# 第三方库统一入口：一个库一个目录，各自在本目录内暴露 CMake 目标。
#
# 目录约定：
#   <库名>/include   对外头文件
#   <库名>/src       需要编译的源码（若有）
#   <库名>/lib       预编译静态库（若有）
#   <库名>/CMakeLists.txt

add_subdirectory(sdl2)          # 窗口 / 事件 / OpenGL 上下文
add_subdirectory(glad)          # OpenGL 4.5 core 函数加载
add_subdirectory(cglm)          # 矩阵 / 向量（header-only）

# 外部逻辑库（预编译 .a + 单一头文件）
add_subdirectory(gene-lib)      # 基因生成与表达
add_subdirectory(relationship)  # 族谱

# 模型加载与贴图烘焙由外部模型库项目实现，完成后在此加入：
# add_subdirectory(model-lib)
