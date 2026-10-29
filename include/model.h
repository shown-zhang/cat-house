#ifndef CAT_HOUSE_MODEL_H
#define CAT_HOUSE_MODEL_H

#include <stddef.h>
#include <stdint.h>

/* 模型库契约：由外部模型库项目实现（glb 解析 + 表型烘焙）。
 * 只产出 CPU 数据，不依赖任何图形 API；上传 GPU 由本项目 renderer 完成。
 * 顶点为位置 / 法线 / UV，与 glTF 一期的属性一致。 */

typedef struct {
    float position[3];
    float normal[3];
    float uv[2];
} ModelVertex;

/* 贴图：RGBA8，行优先，原点在左上（与 glTF 的 UV 约定一致） */
typedef struct {
    uint8_t *pixels;
    int32_t width;
    int32_t height;
} ModelTexture;

typedef struct {
    ModelVertex *vertices;
    uint32_t vertex_count;
    uint32_t *indices;
    uint32_t index_count;
    int32_t texture;       /* 贴图下标，-1 表示没有 */
    float base_color[4];   /* 材质 baseColorFactor */
} ModelPrimitive;

/* 节点 × 图元：节点世界矩阵 + 要绘制的图元 */
typedef struct {
    float transform[16];   /* 列主序，与 OpenGL 一致 */
    uint32_t primitive;
} ModelDrawItem;

typedef struct {
    ModelPrimitive *primitives;
    size_t primitive_count;
    ModelTexture *textures;
    size_t texture_count;
    ModelDrawItem *items;
    size_t item_count;
    float center[3];       /* 世界空间包围盒中心 */
    float radius;          /* 包围球半径 */
} Model;

/* 加载 glTF / GLB 并按表型烘焙贴图。成功返回 0，失败返回 -1。
 * 纯 CPU 实现，不需要 OpenGL 上下文。 */
int model_load(Model *model, const char *path);
void model_free(Model *model);

#endif
