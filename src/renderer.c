#include "renderer.h"

#include <stdio.h>

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

void renderer_draw_model(Renderer *renderer, const Model *model, const vec3 tint)
{
    glUniform3fv(renderer->u_tint, 1, tint);

    for (size_t i = 0; i < model->item_count; ++i) {
        const ModelDrawItem *item = &model->items[i];
        const ModelPrimitive *primitive = &model->primitives[item->primitive];
        if (primitive->index_count == 0) {
            continue;
        }

        mat4 inverse;
        mat3 normal_matrix;
        glm_mat4_inv(item->transform, inverse);
        glm_mat4_pick3(inverse, normal_matrix);
        glm_mat3_transpose(normal_matrix);

        glUniformMatrix4fv(renderer->u_model, 1, GL_FALSE,
                           (const GLfloat *)item->transform);
        glUniformMatrix3fv(renderer->u_normal_matrix, 1, GL_FALSE,
                           (const GLfloat *)normal_matrix);
        glUniform4fv(renderer->u_base_color, 1, primitive->base_color);
        glUniform1i(renderer->u_has_texture, primitive->texture ? 1 : 0);
        glBindTexture(GL_TEXTURE_2D, primitive->texture);

        glBindVertexArray(primitive->vao);
        glDrawElements(GL_TRIANGLES, (GLsizei)primitive->index_count,
                       GL_UNSIGNED_INT, NULL);
        glBindVertexArray(0);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}
