#include "renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>

static const char *VERTEX_SHADER =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "layout(location = 2) in vec2 aTexCoord;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform mat3 uNormalMatrix;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec2 vTexCoord;\n"
    "void main()\n"
    "{\n"
    "    vec4 world = uModel * vec4(aPosition, 1.0);\n"
    "    vWorldPos = world.xyz;\n"
    "    vNormal = normalize(uNormalMatrix * aNormal);\n"
    "    vTexCoord = aTexCoord;\n"
    "    gl_Position = uProj * uView * world;\n"
    "}\n";

static const char *FRAGMENT_SHADER =
    "#version 450 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec2 vTexCoord;\n"
    "uniform vec4 uBaseColor;\n"
    "uniform vec3 uTint;\n"
    "uniform int uHasTexture;\n"
    "uniform sampler2D uTexture;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uCameraPos;\n"
    "out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "    vec4 albedo = uBaseColor;\n"
    "    if (uHasTexture == 1) {\n"
    "        albedo *= texture(uTexture, vTexCoord);\n"
    "    }\n"
    "    albedo.rgb *= uTint;\n"
    "\n"
    "    vec3 normal = normalize(vNormal);\n"
    "    vec3 light = normalize(uLightDir);\n"
    "    float diffuse = max(dot(normal, light), 0.0);\n"
    "    vec3 viewDir = normalize(uCameraPos - vWorldPos);\n"
    "    vec3 halfDir = normalize(light + viewDir);\n"
    "    float specular = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.15;\n"
    "    vec3 color = albedo.rgb * (0.35 + 0.75 * diffuse) + vec3(specular);\n"
    "    fragColor = vec4(color, albedo.a);\n"
    "}\n";

static uint32_t compile_shader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "[renderer] 着色器编译失败：\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int renderer_init(Renderer *renderer)
{
    renderer->program = 0;

    uint32_t vertex = compile_shader(GL_VERTEX_SHADER, VERTEX_SHADER);
    uint32_t fragment = compile_shader(GL_FRAGMENT_SHADER, FRAGMENT_SHADER);
    if (!vertex || !fragment) {
        if (vertex) {
            glDeleteShader(vertex);
        }
        if (fragment) {
            glDeleteShader(fragment);
        }
        return -1;
    }

    renderer->program = glCreateProgram();
    glAttachShader(renderer->program, vertex);
    glAttachShader(renderer->program, fragment);
    glLinkProgram(renderer->program);

    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(renderer->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(renderer->program, sizeof(log), NULL, log);
        fprintf(stderr, "[renderer] 着色器链接失败：\n%s\n", log);
        renderer_destroy(renderer);
        return -1;
    }

    renderer->u_model = glGetUniformLocation(renderer->program, "uModel");
    renderer->u_view = glGetUniformLocation(renderer->program, "uView");
    renderer->u_proj = glGetUniformLocation(renderer->program, "uProj");
    renderer->u_normal_matrix = glGetUniformLocation(renderer->program, "uNormalMatrix");
    renderer->u_base_color = glGetUniformLocation(renderer->program, "uBaseColor");
    renderer->u_has_texture = glGetUniformLocation(renderer->program, "uHasTexture");
    renderer->u_texture = glGetUniformLocation(renderer->program, "uTexture");
    renderer->u_tint = glGetUniformLocation(renderer->program, "uTint");
    renderer->u_light_dir = glGetUniformLocation(renderer->program, "uLightDir");
    renderer->u_camera_pos = glGetUniformLocation(renderer->program, "uCameraPos");

    return 0;
}

void renderer_destroy(Renderer *renderer)
{
    if (renderer->program) {
        glDeleteProgram(renderer->program);
        renderer->program = 0;
    }
}

void renderer_begin(Renderer *renderer, const mat4 view, const mat4 proj,
                    const vec3 camera_pos)
{
    glUseProgram(renderer->program);

    glUniformMatrix4fv(renderer->u_view, 1, GL_FALSE, (const GLfloat *)view);
    glUniformMatrix4fv(renderer->u_proj, 1, GL_FALSE, (const GLfloat *)proj);
    glUniform3fv(renderer->u_camera_pos, 1, camera_pos);

    /* 固定方向光：从左上前方打过来 */
    const vec3 light_dir = {-0.45f, 0.85f, 0.55f};
    glUniform3fv(renderer->u_light_dir, 1, light_dir);

    glActiveTexture(GL_TEXTURE0);
    glUniform1i(renderer->u_texture, 0);
}

static uint32_t upload_texture(const ModelTexture *texture)
{
    if (!texture || !texture->pixels || texture->width <= 0 || texture->height <= 0) {
        return 0;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture->width, texture->height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, texture->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

int renderer_upload_model(Renderer *renderer, const Model *model, GpuModel *out)
{
    (void)renderer;

    if (!model || !out) {
        return -1;
    }
    memset(out, 0, sizeof(*out));

    out->center[0] = model->center[0];
    out->center[1] = model->center[1];
    out->center[2] = model->center[2];
    out->radius = model->radius;

    /* 贴图：每个 CPU 贴图上传一次，图元按下标引用 */
    if (model->texture_count > 0) {
        out->textures = calloc(model->texture_count, sizeof(uint32_t));
        if (!out->textures) {
            return -1;
        }
        for (size_t i = 0; i < model->texture_count; ++i) {
            out->textures[i] = upload_texture(&model->textures[i]);
            out->texture_count = i + 1;
        }
    }

    /* 图元：顶点 / 索引上传到 VBO / EBO，顶点布局与 ModelVertex 一致 */
    if (model->primitive_count > 0) {
        out->primitives = calloc(model->primitive_count, sizeof(GpuPrimitive));
        if (!out->primitives) {
            renderer_destroy_model(renderer, out);
            return -1;
        }

        for (size_t i = 0; i < model->primitive_count; ++i) {
            const ModelPrimitive *src = &model->primitives[i];
            GpuPrimitive *dst = &out->primitives[i];
            out->primitive_count = i + 1;

            dst->index_count = src->index_count;
            dst->texture = src->texture;
            memcpy(dst->base_color, src->base_color, sizeof(dst->base_color));

            if (src->vertex_count == 0 || src->index_count == 0 ||
                !src->vertices || !src->indices) {
                continue;
            }

            glGenVertexArrays(1, &dst->vao);
            glGenBuffers(1, &dst->vbo);
            glGenBuffers(1, &dst->ebo);

            glBindVertexArray(dst->vao);

            glBindBuffer(GL_ARRAY_BUFFER, dst->vbo);
            glBufferData(GL_ARRAY_BUFFER,
                         (GLsizeiptr)(src->vertex_count * sizeof(ModelVertex)),
                         src->vertices, GL_STATIC_DRAW);

            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, dst->ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         (GLsizeiptr)(src->index_count * sizeof(uint32_t)),
                         src->indices, GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                                  (const void *)offsetof(ModelVertex, position));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                                  (const void *)offsetof(ModelVertex, normal));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ModelVertex),
                                  (const void *)offsetof(ModelVertex, uv));

            glBindVertexArray(0);
        }
    }

    /* 绘制项：节点世界矩阵 + 图元下标 */
    if (model->item_count > 0) {
        out->items = calloc(model->item_count, sizeof(GpuDrawItem));
        if (!out->items) {
            renderer_destroy_model(renderer, out);
            return -1;
        }
        for (size_t i = 0; i < model->item_count; ++i) {
            memcpy(out->items[i].transform, model->items[i].transform,
                   sizeof(out->items[i].transform));
            out->items[i].primitive = model->items[i].primitive;
        }
        out->item_count = model->item_count;
    }

    return 0;
}

void renderer_destroy_model(Renderer *renderer, GpuModel *model)
{
    (void)renderer;

    if (!model) {
        return;
    }

    for (size_t i = 0; i < model->primitive_count; ++i) {
        GpuPrimitive *primitive = &model->primitives[i];
        if (primitive->ebo) {
            glDeleteBuffers(1, &primitive->ebo);
        }
        if (primitive->vbo) {
            glDeleteBuffers(1, &primitive->vbo);
        }
        if (primitive->vao) {
            glDeleteVertexArrays(1, &primitive->vao);
        }
    }
    for (size_t i = 0; i < model->texture_count; ++i) {
        if (model->textures[i]) {
            glDeleteTextures(1, &model->textures[i]);
        }
    }

    free(model->primitives);
    free(model->textures);
    free(model->items);
    memset(model, 0, sizeof(*model));
}

void renderer_draw_model(Renderer *renderer, const GpuModel *model, const vec3 tint)
{
    glUniform3fv(renderer->u_tint, 1, tint);

    for (size_t i = 0; i < model->item_count; ++i) {
        const GpuDrawItem *item = &model->items[i];
        if (item->primitive >= model->primitive_count) {
            continue;
        }

        const GpuPrimitive *primitive = &model->primitives[item->primitive];
        if (primitive->index_count == 0 || !primitive->vao) {
            continue;
        }

        mat4 transform;
        memcpy(transform, item->transform, sizeof(transform));

        mat4 inverse;
        mat3 normal_matrix;
        glm_mat4_inv(transform, inverse);
        glm_mat4_pick3(inverse, normal_matrix);
        glm_mat3_transpose(normal_matrix);

        uint32_t texture = 0;
        if (primitive->texture >= 0 &&
            (size_t)primitive->texture < model->texture_count) {
            texture = model->textures[primitive->texture];
        }

        glUniformMatrix4fv(renderer->u_model, 1, GL_FALSE, (const GLfloat *)transform);
        glUniformMatrix3fv(renderer->u_normal_matrix, 1, GL_FALSE,
                           (const GLfloat *)normal_matrix);
        glUniform4fv(renderer->u_base_color, 1, primitive->base_color);
        glUniform1i(renderer->u_has_texture, texture ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, texture);

        glBindVertexArray(primitive->vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)primitive->index_count,
                       GL_UNSIGNED_INT, NULL);
        glBindVertexArray(0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
