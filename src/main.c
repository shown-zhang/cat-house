#define SDL_MAIN_HANDLED

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <glad/glad.h>
#include <SDL.h>
#include <cglm/cglm.h>

#include "gene_lib.h"
#include "model.h"
#include "phenotype.h"
#include "renderer.h"

typedef struct {
    float yaw;
    float pitch;
    float distance;
} OrbitCamera;

static const char *find_asset(const char *relative)
{
    static char candidates[2][512];

    snprintf(candidates[0], sizeof(candidates[0]), "%s", relative);
    snprintf(candidates[1], sizeof(candidates[1]), "../%s", relative);

    for (int i = 0; i < 2; ++i) {
        FILE *file = fopen(candidates[i], "rb");
        if (file) {
            fclose(file);
            return candidates[i];
        }
    }
    return NULL;
}

static void camera_eye(const OrbitCamera *camera, const vec3 center, vec3 eye)
{
    const float cos_pitch = cosf(camera->pitch);
    eye[0] = center[0] + camera->distance * cos_pitch * sinf(camera->yaw);
    eye[1] = center[1] + camera->distance * sinf(camera->pitch);
    eye[2] = center[2] + camera->distance * cos_pitch * cosf(camera->yaw);
}

int main(void)
{
    /* ---------- M1：基因 → 表型 ---------- */
    gene_lib_seed((uint64_t)time(NULL));

    GeneLibGene gene;
    GeneLibPhenotype phenotype;
    PhenotypeAppearance appearance;

    if (gene_lib_generate(&gene) != GENE_LIB_OK) {
        fprintf(stderr, "基因生成失败\n");
        return 1;
    }
    if (gene_lib_phenotype(&gene, &phenotype) != GENE_LIB_OK) {
        fprintf(stderr, "表型表达失败\n");
        return 1;
    }
    phenotype_appearance(&phenotype, &appearance);
    phenotype_print(&gene, &phenotype, &appearance);

    /* ---------- M2：SDL2 窗口 + OpenGL 上下文 ---------- */
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

    /* ---------- 加载静态模型 ---------- */
    const char *asset = find_asset("assets/cat.glb");
    if (!asset) {
        fprintf(stderr, "找不到 assets/cat.glb\n");
        renderer_destroy(&renderer);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Model model;
    if (model_load(&model, asset) != 0) {
        fprintf(stderr, "模型加载失败：%s\n", asset);
        renderer_destroy(&renderer);
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    printf("模型：%s（%zu 图元 / %zu 绘制项，包围球半径 %.3f）\n",
           asset, model.primitive_count, model.item_count, model.radius);
    printf("左键拖拽旋转，滚轮缩放，ESC 退出\n\n");

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE); /* 模型材质是 doubleSided */
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);

    OrbitCamera camera = {
        .yaw = 0.6f,
        .pitch = 0.35f,
        .distance = model.radius * 2.6f,
    };
    const float min_distance = model.radius * 1.2f;
    const float max_distance = model.radius * 12.0f;

    int running = 1;
    int dragging = 0;
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
                if (camera.distance < min_distance) {
                    camera.distance = min_distance;
                }
                if (camera.distance > max_distance) {
                    camera.distance = max_distance;
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
        glViewport(0, 0, width, height);

        vec3 eye;
        camera_eye(&camera, model.center, eye);

        mat4 view;
        mat4 projection;
        vec3 up = {0.0f, 1.0f, 0.0f};
        glm_lookat(eye, model.center, up, view);

        const float near_plane = fmaxf(model.radius * 0.01f, 0.001f);
        const float far_plane = model.radius * 50.0f + camera.distance;
        glm_perspective(glm_rad(45.0f), (float)width / (float)height,
                        near_plane, far_plane, projection);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer_begin(&renderer, view, projection, eye);
        renderer_draw_model(&renderer, &model, appearance.base_color);

        SDL_GL_SwapWindow(window);
    }

    model_destroy(&model);
    renderer_destroy(&renderer);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
