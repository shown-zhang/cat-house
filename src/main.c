#define SDL_MAIN_HANDLED

#include <math.h>
#include <stdio.h>
#include <time.h>

#include <glad/glad.h>
#include <SDL.h>
#include <cglm/cglm.h>

#include "gene_lib.h"
#include "renderer.h"

/* 环绕相机：绕场景中心旋转 */
typedef struct {
    float yaw;
    float pitch;
    float distance;
} OrbitCamera;

/* 场景中心与包围球半径：模型库接入后改用模型自身的包围球 */
static vec3 SCENE_CENTER = {0.0f, 0.0f, 0.0f};
static const float SCENE_RADIUS = 1.0f;

static void camera_eye(const OrbitCamera *camera, const vec3 center, vec3 eye)
{
    const float cos_pitch = cosf(camera->pitch);
    eye[0] = center[0] + camera->distance * cos_pitch * sinf(camera->yaw);
    eye[1] = center[1] + camera->distance * sinf(camera->pitch);
    eye[2] = center[2] + camera->distance * cos_pitch * cosf(camera->yaw);
}

int main(void)
{
    /* ---------- 逻辑库接入验证：生成一份基因并打印 ---------- */
    gene_lib_seed((uint64_t)time(NULL));

    GeneLibGene gene;
    if (gene_lib_generate(&gene) != GENE_LIB_OK) {
        fprintf(stderr, "基因生成失败\n");
        return 1;
    }

    char description[1024];
    if (gene_lib_describe(&gene, description, sizeof(description)) > 0) {
        printf("基因：%s\n\n", description);
    }

    /* ---------- 窗口 + OpenGL 上下文 ---------- */
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
    printf("空场景：模型加载与贴图烘焙由外部模型库负责，库接入后在此绘制\n\n");

    Renderer renderer;
    if (renderer_init(&renderer) != 0) {
        SDL_GL_DeleteContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glClearColor(0.12f, 0.13f, 0.15f, 1.0f);

    OrbitCamera camera = {
        .yaw = 0.6f,
        .pitch = 0.35f,
        .distance = SCENE_RADIUS * 2.6f,
    };
    const float min_distance = SCENE_RADIUS * 1.2f;
    const float max_distance = SCENE_RADIUS * 12.0f;

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
        camera_eye(&camera, SCENE_CENTER, eye);

        mat4 view;
        mat4 projection;
        vec3 up = {0.0f, 1.0f, 0.0f};
        glm_lookat(eye, SCENE_CENTER, up, view);

        const float near_plane = fmaxf(SCENE_RADIUS * 0.01f, 0.001f);
        const float far_plane = SCENE_RADIUS * 50.0f + camera.distance;
        glm_perspective(glm_rad(45.0f), (float)width / (float)height,
                        near_plane, far_plane, projection);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer_begin(&renderer, view, projection, eye);

        SDL_GL_SwapWindow(window);
    }

    renderer_destroy(&renderer);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
