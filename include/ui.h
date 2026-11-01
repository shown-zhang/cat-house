#ifndef CAT_HOUSE_UI_H
#define CAT_HOUSE_UI_H

#include <stdint.h>
#include <cglm/cglm.h>

/* 极简 2D 叠加层：纯色矩形 + ASCII 文字（stb_easy_font） */

typedef struct {
    uint32_t program;
    uint32_t vao;
    uint32_t vbo;
    int32_t u_viewport;
    int32_t u_scale;
    int width;
    int height;
} Ui;

int ui_init(Ui *ui);
void ui_destroy(Ui *ui);

/* 开始绘制：宽度/高度使用 drawable 像素（与 glViewport 一致） */
void ui_begin(Ui *ui, int width, int height);
void ui_end(Ui *ui);

void ui_rect(Ui *ui, float x, float y, float width, float height, const vec4 color);
void ui_text(Ui *ui, float x, float y, const char *text, const vec4 color, float scale);
float ui_text_width(const char *text, float scale);
int ui_hit(float x, float y, float width, float height, float mouse_x, float mouse_y);

#endif
