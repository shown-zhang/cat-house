#ifndef CAT_HOUSE_RENDERER_H
#define CAT_HOUSE_RENDERER_H

#include <stddef.h>
#include <stdint.h>
#include <cglm/cglm.h>

#include "model.h"

/* 最简前向渲染：单方向光 + 环境光 + 基础高光，暂时不做阴影 */
typedef struct {
    uint32_t program;
    int32_t u_model;
    int32_t u_view;
    int32_t u_proj;
    int32_t u_normal_matrix;
    int32_t u_base_color;
    int32_t u_has_texture;
    int32_t u_texture;
    int32_t u_tint;
    int32_t u_light_dir;
    int32_t u_camera_pos;
} Renderer;

/* GPU 侧图元：由 renderer_upload_model 从 Model 上传得到 */
typedef struct {
    uint32_t vao;
    uint32_t vbo;
    uint32_t ebo;
    uint32_t index_count;
    int32_t texture;       /* renderer.textures 的下标，-1 表示没有 */
    vec4 base_color;
} GpuPrimitive;

typedef struct {
    mat4 transform;
    uint32_t primitive;
} GpuDrawItem;

/* GPU 侧模型：绘制与释放都走这里，CPU 的 Model 可以提前释放 */
typedef struct {
    GpuPrimitive *primitives;
    size_t primitive_count;
    uint32_t *textures;
    size_t texture_count;
    GpuDrawItem *items;
    size_t item_count;
    vec3 center;
    float radius;
} GpuModel;

int renderer_init(Renderer *renderer);
void renderer_destroy(Renderer *renderer);

/* 每帧开始：设置相机与光照，清屏由调用方负责 */
void renderer_begin(Renderer *renderer, const mat4 view, const mat4 proj,
                    const vec3 camera_pos);

/* 把 CPU 模型上传为 GPU 对象。成功返回 0，失败返回 -1。 */
int renderer_upload_model(Renderer *renderer, const Model *model, GpuModel *out);
void renderer_destroy_model(Renderer *renderer, GpuModel *model);

/* 绘制整个 GPU 模型，tint 用于按表型整体着色 */
void renderer_draw_model(Renderer *renderer, const GpuModel *model, const vec3 tint);

#endif
