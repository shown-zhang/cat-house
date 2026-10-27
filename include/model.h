#ifndef CAT_HOUSE_MODEL_H
#define CAT_HOUSE_MODEL_H

#include <stddef.h>
#include <stdint.h>
#include <cglm/cglm.h>

/* 一个可绘制的图元：顶点数据已上传 GPU，一期不做蒙皮 */
typedef struct {
    uint32_t vao;
    uint32_t vbo;
    uint32_t ebo;
    uint32_t index_count;
    uint32_t texture;      /* 基础色贴图，0 表示没有 */
    vec4 base_color;       /* 材质 baseColorFactor */
    vec3 bmin;             /* 局部包围盒 */
    vec3 bmax;
} ModelPrimitive;

/* 节点 × 图元：节点世界矩阵 + 要绘制的图元 */
typedef struct {
    mat4 transform;
    uint32_t primitive;
} ModelDrawItem;

typedef struct Model {
    ModelPrimitive *primitives;
    size_t primitive_count;
    ModelDrawItem *items;
    size_t item_count;
    vec3 center;           /* 世界空间包围盒中心 */
    float radius;          /* 包围球半径 */
} Model;

/* 加载 glTF / GLB。成功返回 0，失败返回 -1。需要已创建 OpenGL 上下文。 */
int model_load(Model *model, const char *path);
void model_destroy(Model *model);

#endif
