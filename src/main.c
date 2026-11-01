#define SDL_MAIN_HANDLED

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <glad/glad.h>
#include <SDL.h>
#include <cglm/cglm.h>

#include "appearance.h"
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

/* 随机基因 → 表型 → 烘焙贴图 → 加载模型 → 上传 GPU */
static int rebuild_cat(Renderer *renderer, GpuModel *gpu, int *loaded,
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

    const int status = texbake_bake_glb(SOURCE_GLB, BAKED_GLB, NULL, &pattern);
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

    Ui ui;
    if (ui_init(&ui) != 0) {
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
    rebuild_cat(&renderer, &gpu, &loaded, scene_center, &scene_radius,
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
            if (rebuild_cat(&renderer, &gpu, &loaded, scene_center, &scene_radius,
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

        const int hovered =
            ui_hit(button_x, button_y, button_w, button_h, ui_mouse_x, ui_mouse_y);

        ui_begin(&ui, width, height);
        ui_rect(&ui, button_x, button_y, button_w, button_h,
                hovered ? button_hover : button_color);
        ui_text(&ui, label_x, label_y, button_label, text_color, text_scale);
        ui_text(&ui, button_x, button_y + button_h + 10.0f,
                "drag: rotate / wheel: zoom / R: reroll", text_color, 1.6f);
        ui_end(&ui);

        SDL_GL_SwapWindow(window);
    }

    if (loaded) {
        renderer_destroy_model(&renderer, &gpu);
    }
    ui_destroy(&ui);
    renderer_destroy(&renderer);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
