#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glad/glad.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#include <stb_easy_font.h>
#pragma GCC diagnostic pop

/* stb_easy_font 顶点：x, y, z, rgba，共 16 字节 */
typedef struct {
    float x, y, z;
    uint8_t r, g, b, a;
} UiVertex;

static const char *UI_VERTEX_SHADER =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "uniform vec2 uViewport;\n"
    "uniform float uScale;\n"
    "out vec4 vColor;\n"
    "void main()\n"
    "{\n"
    "    vec2 p = aPos.xy * uScale;\n"
    "    vec2 ndc = p / uViewport * 2.0 - 1.0;\n"
    "    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *UI_FRAGMENT_SHADER =
    "#version 450 core\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "    fragColor = vColor;\n"
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
        fprintf(stderr, "[ui] 着色器编译失败：\n%s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

int ui_init(Ui *ui)
{
    memset(ui, 0, sizeof(*ui));

    const uint32_t vertex = compile_shader(GL_VERTEX_SHADER, UI_VERTEX_SHADER);
    const uint32_t fragment = compile_shader(GL_FRAGMENT_SHADER, UI_FRAGMENT_SHADER);
    if (!vertex || !fragment) {
        if (vertex) {
            glDeleteShader(vertex);
        }
        if (fragment) {
            glDeleteShader(fragment);
        }
        return -1;
    }

    ui->program = glCreateProgram();
    glAttachShader(ui->program, vertex);
    glAttachShader(ui->program, fragment);
    glLinkProgram(ui->program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint ok = GL_FALSE;
    glGetProgramiv(ui->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(ui->program, sizeof(log), NULL, log);
        fprintf(stderr, "[ui] 着色器链接失败：\n%s\n", log);
        ui_destroy(ui);
        return -1;
    }

    ui->u_viewport = glGetUniformLocation(ui->program, "uViewport");
    ui->u_scale = glGetUniformLocation(ui->program, "uScale");

    glGenVertexArrays(1, &ui->vao);
    glGenBuffers(1, &ui->vbo);
    glBindVertexArray(ui->vao);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                          (const void *)offsetof(UiVertex, x));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UiVertex),
                          (const void *)offsetof(UiVertex, r));
    glBindVertexArray(0);

    return 0;
}

void ui_destroy(Ui *ui)
{
    if (!ui) {
        return;
    }
    if (ui->vbo) {
        glDeleteBuffers(1, &ui->vbo);
    }
    if (ui->vao) {
        glDeleteVertexArrays(1, &ui->vao);
    }
    if (ui->program) {
        glDeleteProgram(ui->program);
    }
    memset(ui, 0, sizeof(*ui));
}

void ui_begin(Ui *ui, int width, int height)
{
    ui->width = width;
    ui->height = height;

    glUseProgram(ui->program);
    glUniform2f(ui->u_viewport, (float)width, (float)height);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(ui->vao);
}

void ui_end(Ui *ui)
{
    (void)ui;
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

static void draw_vertices(Ui *ui, const void *data, size_t bytes, GLenum mode, float scale)
{
    if (bytes == 0) {
        return;
    }
    glUniform1f(ui->u_scale, scale);
    glBindBuffer(GL_ARRAY_BUFFER, ui->vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)(bytes / sizeof(UiVertex)));
}

static UiVertex color_vertex(float x, float y, const vec4 color)
{
    UiVertex vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = 0.0f;
    vertex.r = (uint8_t)(color[0] * 255.0f);
    vertex.g = (uint8_t)(color[1] * 255.0f);
    vertex.b = (uint8_t)(color[2] * 255.0f);
    vertex.a = (uint8_t)(color[3] * 255.0f);
    return vertex;
}

void ui_rect(Ui *ui, float x, float y, float width, float height, const vec4 color)
{
    UiVertex vertices[6];
    vertices[0] = color_vertex(x, y, color);
    vertices[1] = color_vertex(x + width, y, color);
    vertices[2] = color_vertex(x + width, y + height, color);
    vertices[3] = color_vertex(x, y, color);
    vertices[4] = color_vertex(x + width, y + height, color);
    vertices[5] = color_vertex(x, y + height, color);

    draw_vertices(ui, vertices, sizeof(vertices), GL_TRIANGLES, 1.0f);
}

float ui_text_width(const char *text, float scale)
{
    if (!text || !*text) {
        return 0.0f;
    }
    return (float)stb_easy_font_width((char *)text) * scale;
}

void ui_text(Ui *ui, float x, float y, const char *text, const vec4 color, float scale)
{
    if (!text || !*text) {
        return;
    }

    static UiVertex quads[2048];
    unsigned char rgba[4] = {
        (unsigned char)(color[0] * 255.0f),
        (unsigned char)(color[1] * 255.0f),
        (unsigned char)(color[2] * 255.0f),
        (unsigned char)(color[3] * 255.0f),
    };

    const int quad_count =
        stb_easy_font_print(x, y, (char *)text, rgba, quads, sizeof(quads));
    if (quad_count <= 0) {
        return;
    }

    UiVertex *triangles = malloc((size_t)quad_count * 6 * sizeof(UiVertex));
    if (!triangles) {
        return;
    }

    /* 以 (x, y) 为原点放大：位置与尺寸都按 scale 计算，坐标单位即屏幕像素 */
    for (int v = 0; v < quad_count * 4; ++v) {
        quads[v].x = x + (quads[v].x - x) * scale;
        quads[v].y = y + (quads[v].y - y) * scale;
    }

    for (int q = 0; q < quad_count; ++q) {
        const UiVertex *src = &quads[q * 4];
        UiVertex *dst = &triangles[q * 6];
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[0];
        dst[4] = src[2];
        dst[5] = src[3];
    }

    draw_vertices(ui, triangles, (size_t)quad_count * 6 * sizeof(UiVertex),
                  GL_TRIANGLES, 1.0f);
    free(triangles);
}

int ui_hit(float x, float y, float width, float height, float mouse_x, float mouse_y)
{
    return mouse_x >= x && mouse_x <= x + width && mouse_y >= y && mouse_y <= y + height;
}
