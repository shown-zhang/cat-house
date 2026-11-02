#define SDL_MAIN_HANDLED

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glad/glad.h>
#include <SDL.h>
#include <cglm/cglm.h>

#include "appearance.h"
#include "cat_asset.h"
#include "fur.h"
#include "gene_lib.h"
#include "model.h"
#include "renderer.h"
#include "texbake.h"
#include "ui.h"

/* 原始模型与烘焙输出 */
#define SOURCE_GLB "assets/cat.glb"
#define BAKED_GLB "cat_baked.glb"

/* 环绕相机：绕场景中心旋转 */
typedef struct {
    float yaw;
    float pitch;
    float distance;
} OrbitCamera;

static void camera_eye(const OrbitCamera *camera, const vec3 center, vec3 eye)
{
    const float cos_pitch = cosf(camera->pitch);
    eye[0] = center[0] + camera->distance * cos_pitch * sinf(camera->yaw);
    eye[1] = center[1] + camera->distance * sinf(camera->pitch);
    eye[2] = center[2] + camera->distance * cos_pitch * cosf(camera->yaw);
}

/* ---------- 毛发：把 Model 的绘制项合并成一份对象空间网格 ---------- */

typedef struct {
    float *positions;
    float *normals;
    float *uvs;
    uint32_t *indices;
    uint32_t vertex_count;
    uint32_t index_count;
    float height;          /* 包围盒高度，用来按模型尺寸标定毛长与粗细 */
    float min_y;
    float max_y;
    float min_z;
    float max_z;           /* 头在 +Z 端，用来生成头脸遮罩 */
} MergedMesh;

static void merged_mesh_free(MergedMesh *mesh)
{
    free(mesh->positions);
    free(mesh->normals);
    free(mesh->uvs);
    free(mesh->indices);
    memset(mesh, 0, sizeof(*mesh));
}

/* 毛发库只接受一份网格，这里把全部绘制项按节点世界矩阵烘焙到同一组数组里 */
static int merged_mesh_build(const Model *model, MergedMesh *out)
{
    memset(out, 0, sizeof(*out));
    if (!model || model->item_count == 0) {
        return -1;
    }

    size_t vertex_total = 0;
    size_t index_total = 0;
    for (size_t i = 0; i < model->item_count; ++i) {
        const ModelDrawItem *item = &model->items[i];
        if (item->primitive >= model->primitive_count) {
            continue;
        }
        const ModelPrimitive *primitive = &model->primitives[item->primitive];
        if (!primitive->vertices || !primitive->indices) {
            continue;
        }
        vertex_total += primitive->vertex_count;
        index_total += primitive->index_count;
    }
    if (vertex_total == 0 || index_total == 0) {
        return -1;
    }

    out->positions = calloc(vertex_total * 3, sizeof(float));
    out->normals = calloc(vertex_total * 3, sizeof(float));
    out->uvs = calloc(vertex_total * 2, sizeof(float));
    out->indices = calloc(index_total, sizeof(uint32_t));
    if (!out->positions || !out->normals || !out->uvs || !out->indices) {
        merged_mesh_free(out);
        return -1;
    }

    uint32_t vertex_base = 0;
    uint32_t index_base = 0;
    for (size_t i = 0; i < model->item_count; ++i) {
        const ModelDrawItem *item = &model->items[i];
        if (item->primitive >= model->primitive_count) {
            continue;
        }
        const ModelPrimitive *primitive = &model->primitives[item->primitive];
        if (!primitive->vertices || !primitive->indices) {
            continue;
        }

        mat4 transform;
        mat4 inverse;
        mat3 normal_matrix;
        memcpy(transform, item->transform, sizeof(transform));
        glm_mat4_inv(transform, inverse);
        glm_mat4_pick3(inverse, normal_matrix);
        glm_mat3_transpose(normal_matrix);

        for (uint32_t v = 0; v < primitive->vertex_count; ++v) {
            const uint32_t dst = vertex_base + v;
            vec3 position;
            vec3 normal;
            glm_vec3_copy(primitive->vertices[v].position, position);
            glm_vec3_copy(primitive->vertices[v].normal, normal);
            glm_mat4_mulv3(transform, position, 1.0f, &out->positions[dst * 3]);
            glm_mat3_mulv(normal_matrix, normal, &out->normals[dst * 3]);
            glm_normalize(&out->normals[dst * 3]);
            out->uvs[dst * 2 + 0] = primitive->vertices[v].uv[0];
            out->uvs[dst * 2 + 1] = primitive->vertices[v].uv[1];
        }

        for (uint32_t k = 0; k < primitive->index_count; ++k) {
            out->indices[index_base + k] = primitive->indices[k] + vertex_base;
        }
        vertex_base += primitive->vertex_count;
        index_base += primitive->index_count;
    }

    out->vertex_count = vertex_base;
    out->index_count = index_base;

    float min_y = out->positions[1];
    float max_y = out->positions[1];
    float min_z = out->positions[2];
    float max_z = out->positions[2];
    for (uint32_t i = 1; i < out->vertex_count; ++i) {
        const float y = out->positions[i * 3 + 1];
        const float z = out->positions[i * 3 + 2];
        if (y < min_y) {
            min_y = y;
        }
        if (y > max_y) {
            max_y = y;
        }
        if (z < min_z) {
            min_z = z;
        }
        if (z > max_z) {
            max_z = z;
        }
    }
    out->min_y = min_y;
    out->max_y = max_y;
    out->min_z = min_z;
    out->max_z = max_z;
    out->height = max_y - min_y;
    return 0;
}

/* ---------- 毛发遮罩：按 UV 光栅化出「发长 / 发根宽度」比例图 ---------- */

#define FUR_MASK_SIZE 512

static float smoothstep01(float edge0, float edge1, float x)
{
    float t = (x - edge0) / (edge1 - edge0);
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

/* 对象空间位置 →（R：长度比例，G：发根宽度比例）：头部转为贴体短绒，五官区无毛 */
static void fur_mask_factors(const MergedMesh *mesh, const float *position,
                             float *length_scale, float *width_scale)
{
    const float span_y = mesh->max_y - mesh->min_y;
    const float span_z = mesh->max_z - mesh->min_z;
    const float y = span_y > 0.0f ? (position[1] - mesh->min_y) / span_y : 0.0f;
    const float z = span_z > 0.0f ? (position[2] - mesh->min_z) / span_z : 0.0f;

    /* 头在 +Z 端：z 靠前、y 高于胸口的部分整体改成短绒 */
    const float head = smoothstep01(0.62f, 0.74f, z) * smoothstep01(0.48f, 0.60f, y);
    float len = 1.0f - 0.72f * head;
    float wid = 1.0f - 0.45f * head;

    /* 五官区（眼睛 / 鼻 / 嘴）：椭圆内完全不长毛，边缘平滑收回 */
    const float dy = (y - 0.80f) / 0.12f;
    const float dz = (z - 0.94f) / 0.07f;
    const float face = 1.0f - smoothstep01(0.70f, 1.20f, dy * dy + dz * dz);
    len *= 1.0f - face;
    wid *= 1.0f - face;

    *length_scale = len;
    *width_scale = wid;
}

static void fur_mask_write(uint8_t *mask, int size, int x, int y, float r, float g)
{
    if (x < 0 || y < 0 || x >= size || y >= size) {
        return;
    }
    uint8_t *texel = &mask[((size_t)y * size + x) * 4];
    texel[0] = (uint8_t)(r * 255.0f + 0.5f);
    texel[1] = (uint8_t)(g * 255.0f + 0.5f);
    texel[2] = 0;
    texel[3] = 255;
}

/* 逐三角形把 UV 覆盖的纹素填上遮罩值，返回 malloc 的 RGBA8 图 */
static uint8_t *fur_mask_build(const MergedMesh *mesh, int size)
{
    uint8_t *mask = calloc((size_t)size * size * 4, 1);
    uint8_t *scratch = malloc((size_t)size * size * 4);
    if (!mask || !scratch) {
        free(mask);
        free(scratch);
        return NULL;
    }

    const float scale = (float)(size - 1);
    for (uint32_t t = 0; t + 2 < mesh->index_count; t += 3) {
        const uint32_t i0 = mesh->indices[t + 0];
        const uint32_t i1 = mesh->indices[t + 1];
        const uint32_t i2 = mesh->indices[t + 2];
        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count ||
            i2 >= mesh->vertex_count) {
            continue;
        }

        const float ax = mesh->uvs[i0 * 2 + 0] * scale;
        const float ay = mesh->uvs[i0 * 2 + 1] * scale;
        const float bx = mesh->uvs[i1 * 2 + 0] * scale;
        const float by = mesh->uvs[i1 * 2 + 1] * scale;
        const float cx = mesh->uvs[i2 * 2 + 0] * scale;
        const float cy = mesh->uvs[i2 * 2 + 1] * scale;

        const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (fabsf(area) < 1e-6f) {
            continue;
        }

        int min_x = (int)floorf(fminf(ax, fminf(bx, cx)));
        int min_y = (int)floorf(fminf(ay, fminf(by, cy)));
        int max_x = (int)ceilf(fmaxf(ax, fmaxf(bx, cx)));
        int max_y = (int)ceilf(fmaxf(ay, fmaxf(by, cy)));
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x > size - 1) max_x = size - 1;
        if (max_y > size - 1) max_y = size - 1;

        for (int py = min_y; py <= max_y; ++py) {
            for (int px = min_x; px <= max_x; ++px) {
                const float fx = (float)px + 0.5f;
                const float fy = (float)py + 0.5f;
                const float w0 = ((bx - fx) * (cy - fy) - (cx - fx) * (by - fy)) / area;
                const float w1 = ((cx - fx) * (ay - fy) - (ax - fx) * (cy - fy)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -0.01f || w1 < -0.01f || w2 < -0.01f) {
                    continue;
                }

                float position[3];
                for (int k = 0; k < 3; ++k) {
                    position[k] = w0 * mesh->positions[i0 * 3 + k] +
                                  w1 * mesh->positions[i1 * 3 + k] +
                                  w2 * mesh->positions[i2 * 3 + k];
                }

                float length_scale = 1.0f;
                float width_scale = 1.0f;
                fur_mask_factors(mesh, position, &length_scale, &width_scale);
                fur_mask_write(mask, size, px, py, length_scale, width_scale);
            }
        }
    }

    /* 膨胀两圈：双线性采样在 UV 岛边缘不会读到未覆盖的背景 */
    for (int pass = 0; pass < 2; ++pass) {
        memcpy(scratch, mask, (size_t)size * size * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                uint8_t *dst = &mask[((size_t)y * size + x) * 4];
                if (dst[3] != 0) {
                    continue;
                }
                int sum_r = 0;
                int sum_g = 0;
                int count = 0;
                for (int oy = -1; oy <= 1; ++oy) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int nx = x + ox;
                        const int ny = y + oy;
                        if (nx < 0 || ny < 0 || nx >= size || ny >= size) {
                            continue;
                        }
                        const uint8_t *src = &scratch[((size_t)ny * size + nx) * 4];
                        if (src[3] == 0) {
                            continue;
                        }
                        sum_r += src[0];
                        sum_g += src[1];
                        ++count;
                    }
                }
                if (count > 0) {
                    dst[0] = (uint8_t)(sum_r / count);
                    dst[1] = (uint8_t)(sum_g / count);
                    dst[2] = 0;
                    dst[3] = 255;
                }
            }
        }
    }

    free(scratch);
    return mask;
}

/* 基因描述 → 稳定的毛发随机种子，同一只猫重新生成结果一致 */
static uint32_t text_hash(const char *text)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)text; p && *p; ++p) {
        hash = (hash ^ *p) * 16777619u;
    }
    return hash ? hash : 1u;
}

/* 表型 → 毛发参数：毛长与毛质决定长度、卷曲、成束 */
static void fur_params_from_phenotype(const GeneLibPhenotype *phenotype, uint32_t seed,
                                      float model_height, FurParams *params)
{
    fur_params_init(params);
    params->seed = seed;

    if (model_height <= 0.0f) {
        model_height = 1.0f;
    }

    /* 按模型高度标定：库默认值以「最长轴半长 = 1」为尺度，
     * 这只猫身高约 0.9，直接套默认值会把整只猫裹住 */
    params->length = model_height * 0.040f;
    params->width = model_height * 0.0011f;    /* 细但有分量，填满发丝间隙 */
    params->density = 1.0f;                    /* 用满密度，配合细发丝 */
    params->segment_count = 6;                 /* 短毛 6 段足够顺滑，换取更多根数 */
    params->segment_budget = 1200000;          /* 20 万根 × 6 段 */
    params->tip_scale = 0.12f;
    params->gravity = 0.30f;
    params->clumping = 0.25f;                  /* 少成束，覆盖更均匀 */
    params->frizz = 0.08f;                     /* 短毛顺滑，不炸毛 */
    params->lift = 0.15f;                      /* 贴面顺毛，避免全部直立 */
    params->flow_dir[0] = 0.0f;
    params->flow_dir[1] = 0.0f;
    params->flow_dir[2] = -1.0f;               /* 猫头朝 +Z：毛流从鼻尖梳向尾巴 */

    if (phenotype->hair_texture == GENE_LIB_HAIR_TEXTURE_HAIRLESS) {
        params->density = 0.0f;
        params->strand_count = 0;
        return;
    }

    if (phenotype->hair_length == GENE_LIB_HAIR_LONG) {
        params->length *= 1.8f;
        params->gravity = 0.50f;
        params->lift = 0.25f;
    }

    if (phenotype->hair_texture == GENE_LIB_HAIR_TEXTURE_REX) {
        params->length *= 0.5f;
        params->frizz = 0.85f;
        params->clumping = 0.2f;
        params->lift = 0.45f;
    }
}

/* 随机基因 → 表型 → 烘焙贴图 → 加载模型 → 上传 GPU → 重建毛发 */
static int rebuild_cat(Renderer *renderer, GpuModel *gpu, int *loaded, FurContext *fur,
                       vec3 center, float *radius,
                       char *description, size_t capacity)
{
    GeneLibGene gene;
    if (gene_lib_generate(&gene) != GENE_LIB_OK) {
        fprintf(stderr, "基因生成失败\n");
        return -1;
    }

    if (gene_lib_describe(&gene, description, capacity) <= 0) {
        snprintf(description, capacity, "cat");
    }
    printf("基因：%s\n", description);

    GeneLibPhenotype phenotype;
    if (gene_lib_phenotype(&gene, &phenotype) != GENE_LIB_OK) {
        fprintf(stderr, "表型表达失败\n");
        return -1;
    }

    TexbakePattern pattern;
    appearance_build_pattern(&phenotype, &pattern);

    /* 按表型挑模型变体，缺资产时由资产模块逐级回退 */
    char source_glb[256];
    cat_asset_select(&phenotype, SOURCE_GLB, source_glb, sizeof(source_glb));
    printf("模型资产：%s\n", source_glb);

    /* 表面烘焙：3D 图元按对象空间求值，跨 UV 岛连续 */
    TexbakeSurfaceOptions options;
    texbake_surface_options_init(&options);

    const int status = texbake_bake_glb_surface(source_glb, BAKED_GLB, NULL,
                                                &pattern, &options);
    if (status != TEXBAKE_OK) {
        fprintf(stderr, "贴图烘焙失败：%d\n", status);
        return -1;
    }

    Model model;
    if (model_load(&model, BAKED_GLB) != 0) {
        fprintf(stderr, "模型加载失败：%s\n", BAKED_GLB);
        return -1;
    }

    GpuModel next;
    if (renderer_upload_model(renderer, &model, &next) != 0) {
        fprintf(stderr, "模型上传 GPU 失败\n");
        model_free(&model);
        return -1;
    }

    printf("模型：%zu 图元 / %zu 绘制项 / %zu 贴图，半径 %.3f\n",
           next.primitive_count, next.item_count, next.texture_count,
           (double)next.radius);

    /* 毛发：复用同一份 CPU 网格与烘焙贴图，必须在 model_free 之前 */
    MergedMesh hair_mesh;
    if (fur && merged_mesh_build(&model, &hair_mesh) == 0) {
        FurMeshDesc fur_mesh;
        memset(&fur_mesh, 0, sizeof(fur_mesh));
        fur_mesh.positions = hair_mesh.positions;
        fur_mesh.normals = hair_mesh.normals;
        fur_mesh.uvs = hair_mesh.uvs;
        fur_mesh.indices = hair_mesh.indices;
        fur_mesh.vertex_count = hair_mesh.vertex_count;
        fur_mesh.index_count = hair_mesh.index_count;

        if (model.primitive_count > 0 && model.primitives[0].texture >= 0 &&
            (size_t)model.primitives[0].texture < model.texture_count) {
            const ModelTexture *texture = &model.textures[model.primitives[0].texture];
            fur_mesh.basecolor_rgba = texture->pixels;
            fur_mesh.basecolor_width = texture->width;
            fur_mesh.basecolor_height = texture->height;
        }

        /* 头脸遮罩：头顶短绒、五官区无毛，其余部位保持正常毛长 */
        uint8_t *fur_mask = fur_mask_build(&hair_mesh, FUR_MASK_SIZE);
        if (fur_mask) {
            fur_mesh.fur_mask_rgba = fur_mask;
            fur_mesh.fur_mask_width = FUR_MASK_SIZE;
            fur_mesh.fur_mask_height = FUR_MASK_SIZE;
        }

        FurParams fur_params;
        fur_params_from_phenotype(&phenotype, text_hash(description),
                                  hair_mesh.height, &fur_params);
        if (fur_set_mesh(fur, &fur_mesh, &fur_params) != FUR_OK) {
            fprintf(stderr, "毛发重建失败\n");
        } else {
            FurStats stats;
            fur_get_stats(fur, &stats);
            printf("毛发：%d 根 × %d 段（%d 段实例），生成 %.1f ms\n",
                   stats.strand_count, stats.segment_count, stats.instance_count,
                   (double)stats.generate_ms);
        }
        free(fur_mask);
        merged_mesh_free(&hair_mesh);
    }

    model_free(&model);

    if (*loaded) {
        renderer_destroy_model(renderer, gpu);
    }
    *gpu = next;
    *loaded = 1;

    memcpy(center, next.center, sizeof(vec3));
    *radius = next.radius > 0.001f ? next.radius : 1.0f;
    return 0;
}

int main(void)
{
    gene_lib_seed((uint64_t)time(NULL));

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init 失败：%s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    /* 毛发的 alpha-to-coverage 需要多采样缓冲 */
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    SDL_Window *window = SDL_CreateWindow("cat-house",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1280, 800,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "创建窗口失败：%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fprintf(stderr, "创建 OpenGL 上下文失败：%s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "加载 OpenGL 函数失败\n");
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("OpenGL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    Renderer renderer;
    if (renderer_init(&renderer) != 0) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    FurContext *fur = NULL;
    if (fur_create(&fur) != FUR_OK) {
        fprintf(stderr, "毛发渲染初始化失败\n");
        renderer_destroy(&renderer);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* 与 renderer 使用同一套光照参数，避免身体与毛发两张皮 */
    FurLighting fur_lighting = {
        .direction = {-0.45f, 0.85f, 0.55f},
        .color = {1.0f, 1.0f, 1.0f},
        .ambient = {0.35f, 0.35f, 0.35f},
    };
    fur_set_lighting(fur, &fur_lighting);

    Ui ui;
    if (ui_init(&ui) != 0) {
        fur_destroy(fur);
        renderer_destroy(&renderer);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);

    GpuModel gpu;
    memset(&gpu, 0, sizeof(gpu));
    int loaded = 0;

    vec3 scene_center = {0.0f, 0.0f, 0.0f};
    float scene_radius = 1.0f;

    char description[1024] = "cat";
    rebuild_cat(&renderer, &gpu, &loaded, fur, scene_center, &scene_radius,
                description, sizeof(description));
    SDL_SetWindowTitle(window, description);

    OrbitCamera camera = {
        .yaw = 0.35f,
        .pitch = 0.20f,
        .distance = scene_radius * 2.6f,
    };

    const float button_x = 20.0f;
    const float button_y = 20.0f;
    const float button_w = 150.0f;
    const float button_h = 40.0f;
    const float text_scale = 2.2f;
    const char *button_label = "REROLL";
    const float label_width = ui_text_width(button_label, text_scale);
    const float label_x = button_x + (button_w - label_width) * 0.5f;
    const float label_y = button_y + (button_h - 12.0f * text_scale) * 0.5f;

    const vec4 button_color = {0.20f, 0.50f, 0.80f, 1.0f};
    const vec4 button_hover = {0.28f, 0.62f, 0.95f, 1.0f};
    const vec4 text_color = {1.0f, 1.0f, 1.0f, 1.0f};

    int running = 1;
    int dragging = 0;
    int reroll = 0;
    int fur_enabled = 1;
    int click_pending = 0;
    float click_x = 0.0f;
    float click_y = 0.0f;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = 0;
                } else if (event.key.keysym.sym == SDLK_r) {
                    reroll = 1;
                } else if (event.key.keysym.sym == SDLK_f) {
                    fur_enabled = !fur_enabled;
                    FurStats stats;
                    fur_get_stats(fur, &stats);
                    printf("毛发：%s | 本帧 %d 段，生成 %.1f ms，GPU %.2f ms\n",
                           fur_enabled ? "开" : "关", stats.drawn_instances,
                           (double)stats.generate_ms, (double)stats.gpu_ms);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    dragging = 1;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    dragging = 0;
                    click_pending = 1;
                    click_x = (float)event.button.x;
                    click_y = (float)event.button.y;
                }
                break;
            case SDL_MOUSEMOTION:
                if (dragging) {
                    camera.yaw -= (float)event.motion.xrel * 0.01f;
                    camera.pitch -= (float)event.motion.yrel * 0.01f;
                    if (camera.pitch > 1.45f) {
                        camera.pitch = 1.45f;
                    }
                    if (camera.pitch < -1.45f) {
                        camera.pitch = -1.45f;
                    }
                }
                break;
            case SDL_MOUSEWHEEL:
                if (event.wheel.y > 0) {
                    camera.distance *= 0.9f;
                } else if (event.wheel.y < 0) {
                    camera.distance *= 1.1f;
                }
                break;
            default:
                break;
            }
        }

        int width = 0;
        int height = 0;
        SDL_GL_GetDrawableSize(window, &width, &height);
        if (width <= 0 || height <= 0) {
            continue;
        }

        int window_width = 1;
        int window_height = 1;
        SDL_GetWindowSize(window, &window_width, &window_height);

        int mouse_x = 0;
        int mouse_y = 0;
        SDL_GetMouseState(&mouse_x, &mouse_y);
        const float ui_mouse_x = (float)mouse_x * (float)width / (float)window_width;
        const float ui_mouse_y = (float)mouse_y * (float)height / (float)window_height;

        /* 鼠标左键在按钮内抬起 → 重新随机 */
        if (click_pending) {
            const float ui_click_x = click_x * (float)width / (float)window_width;
            const float ui_click_y = click_y * (float)height / (float)window_height;
            if (ui_hit(button_x, button_y, button_w, button_h, ui_click_x, ui_click_y)) {
                reroll = 1;
            }
            click_pending = 0;
        }

        if (reroll) {
            reroll = 0;
            if (rebuild_cat(&renderer, &gpu, &loaded, fur, scene_center, &scene_radius,
                            description, sizeof(description)) == 0) {
                SDL_SetWindowTitle(window, description);
                camera.distance = scene_radius * 2.6f;
                camera.yaw = 0.35f;
                camera.pitch = 0.20f;
            }
        }

        const float min_distance = scene_radius * 1.2f;
        const float max_distance = scene_radius * 12.0f;
        if (camera.distance < min_distance) {
            camera.distance = min_distance;
        }
        if (camera.distance > max_distance) {
            camera.distance = max_distance;
        }

        glViewport(0, 0, width, height);

        vec3 eye;
        camera_eye(&camera, scene_center, eye);

        mat4 view;
        mat4 projection;
        vec3 up = {0.0f, 1.0f, 0.0f};
        glm_lookat(eye, scene_center, up, view);

        const float near_plane = fmaxf(scene_radius * 0.01f, 0.001f);
        const float far_plane = scene_radius * 50.0f + camera.distance;
        glm_perspective(glm_rad(45.0f), (float)width / (float)height,
                        near_plane, far_plane, projection);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderer_begin(&renderer, view, projection, eye);
        if (loaded) {
            const vec3 tint = {1.0f, 1.0f, 1.0f};
            renderer_draw_model(&renderer, &gpu, tint);
        }

        /* 毛发在身体之后绘制：网格已合并到世界空间，模型矩阵为单位阵 */
        if (fur && fur_enabled) {
            mat4 identity;
            glm_mat4_identity(identity);
            fur_draw(fur, (const float *)identity, (const float *)view,
                     (const float *)projection, eye);
        }

        const int hovered =
            ui_hit(button_x, button_y, button_w, button_h, ui_mouse_x, ui_mouse_y);

        ui_begin(&ui, width, height);
        ui_rect(&ui, button_x, button_y, button_w, button_h,
                hovered ? button_hover : button_color);
        ui_text(&ui, label_x, label_y, button_label, text_color, text_scale);
        ui_text(&ui, button_x, button_y + button_h + 10.0f,
                "drag: rotate / wheel: zoom / R: reroll / F: fur", text_color, 1.6f);
        ui_end(&ui);

        SDL_GL_SwapWindow(window);
    }

    if (loaded) {
        renderer_destroy_model(&renderer, &gpu);
    }
    fur_destroy(fur);
    ui_destroy(&ui);
    renderer_destroy(&renderer);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
