#ifndef CAT_HOUSE_RENDERER_H
#define CAT_HOUSE_RENDERER_H

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

int renderer_init(Renderer *renderer);
void renderer_destroy(Renderer *renderer);

/* 每帧开始：设置相机与光照，清屏由调用方负责 */
void renderer_begin(Renderer *renderer, const mat4 view, const mat4 proj,
                    const vec3 camera_pos);

/* 按表型色调 tint 绘制整个模型 */
void renderer_draw_model(Renderer *renderer, const Model *model, const vec3 tint);

#endif
