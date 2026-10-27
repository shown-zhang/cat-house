#include "phenotype.h"

#include <stdio.h>

/* 毛色调色板：索引与 GeneLibColor 一一对应 */
static const float PALETTE[][3] = {
    {0.10f, 0.10f, 0.11f}, /* BLACK */
    {0.42f, 0.47f, 0.55f}, /* BLUE */
    {0.35f, 0.22f, 0.16f}, /* CHOCOLATE */
    {0.63f, 0.57f, 0.62f}, /* LILAC */
    {0.55f, 0.33f, 0.20f}, /* CINNAMON */
    {0.72f, 0.62f, 0.55f}, /* FAWN */
    {0.85f, 0.45f, 0.16f}, /* ORANGE */
    {0.94f, 0.78f, 0.55f}, /* CREAM */
    {0.78f, 0.78f, 0.80f}, /* SILVER */
    {0.96f, 0.95f, 0.93f}, /* WHITE */
};

static const char *COLOR_NAMES[] = {
    "黑色", "蓝色", "巧克力色", "淡紫", "肉桂色",
    "浅黄褐", "橘色", "奶油色", "银色", "白色",
};

static const char *EYE_NAMES[] = {
    "绿色", "黄色", "琥珀色", "蓝色",
};

static const char *EAR_NAMES[] = {
    "直耳", "折耳", "卷耳",
};

static const char *HAIR_LENGTH_NAMES[] = {
    "短毛", "长毛",
};

static const char *HAIR_TEXTURE_NAMES[] = {
    "正常毛", "卷毛", "无毛",
};

static const char *LEG_NAMES[] = {
    "正常腿", "矮脚", "严重矮脚",
};

static const char *TAIL_NAMES[] = {
    "长尾", "短尾", "断尾",
};

static const char *PATTERN_NAMES[] = {
    "色块", "条纹", "渐变", "噪声",
};

static void copy_color(vec3 out, const float rgb[3])
{
    out[0] = rgb[0];
    out[1] = rgb[1];
    out[2] = rgb[2];
}

void phenotype_appearance(const GeneLibPhenotype *phenotype, PhenotypeAppearance *out)
{
    out->base_color[0] = 0.5f;
    out->base_color[1] = 0.5f;
    out->base_color[2] = 0.5f;

    if (phenotype->base_color >= 0 && phenotype->base_color <= GENE_LIB_COLOR_WHITE) {
        copy_color(out->base_color, PALETTE[phenotype->base_color]);
    }
    copy_color(out->accent_color, out->base_color);

    if (phenotype->pattern_layout.count > 0) {
        const uint8_t color = phenotype->pattern_layout.layers[0].color;
        if (color <= GENE_LIB_COLOR_WHITE) {
            copy_color(out->accent_color, PALETTE[color]);
        }
    }

    out->eye_color[0] = 0.5f;
    out->eye_color[1] = 0.5f;
    out->eye_color[2] = 0.5f;
    switch (phenotype->eye_color) {
    case GENE_LIB_EYE_GREEN:
        copy_color(out->eye_color, (const float[3]){0.29f, 0.62f, 0.30f});
        break;
    case GENE_LIB_EYE_YELLOW:
        copy_color(out->eye_color, (const float[3]){0.88f, 0.82f, 0.31f});
        break;
    case GENE_LIB_EYE_AMBER:
        copy_color(out->eye_color, (const float[3]){0.85f, 0.55f, 0.15f});
        break;
    case GENE_LIB_EYE_BLUE:
        copy_color(out->eye_color, (const float[3]){0.35f, 0.60f, 0.92f});
        break;
    }

    out->has_pattern = phenotype->pattern_layout.count > 0;
}

void phenotype_print(const GeneLibGene *gene, const GeneLibPhenotype *phenotype,
                     const PhenotypeAppearance *appearance)
{
    char description[1024];
    printf("=== 基因表达 ===\n");

    const int length = gene_lib_describe(gene, description, sizeof(description));
    if (length > 0) {
        printf("%s\n", description);
    } else {
        printf("文本描述生成失败（%d）\n", length);
    }

    const char *sex = phenotype->sex == GENE_LIB_FEMALE ? "母猫" : "公猫";
    const char *base_color = phenotype->base_color <= GENE_LIB_COLOR_WHITE
                                 ? COLOR_NAMES[phenotype->base_color]
                                 : "未知";
    const char *eye_color = phenotype->eye_color <= GENE_LIB_EYE_BLUE
                                ? EYE_NAMES[phenotype->eye_color]
                                : "未知";

    printf("表型：%s / %s，眼睛 %s，%s%s，%s（程度 %u），%s，%s\n",
           sex, base_color, eye_color,
           HAIR_LENGTH_NAMES[phenotype->hair_length],
           HAIR_TEXTURE_NAMES[phenotype->hair_texture],
           EAR_NAMES[phenotype->ear_shape], phenotype->ear_severity,
           LEG_NAMES[phenotype->leg_length], TAIL_NAMES[phenotype->tail_form]);

    if (appearance->has_pattern) {
        const GeneLibPatternLayout *layout = &phenotype->pattern_layout;
        printf("花纹：%u 层，第一层为 %s，颜色 %s\n",
               layout->count,
               layout->layers[0].kind <= GENE_LIB_PATTERN_NOISE
                   ? PATTERN_NAMES[layout->layers[0].kind]
                   : "未知",
               layout->layers[0].color <= GENE_LIB_COLOR_WHITE
                   ? COLOR_NAMES[layout->layers[0].color]
                   : "未知");
    } else {
        printf("花纹：无\n");
    }

    printf("渲染：base_color = (%.2f, %.2f, %.2f)%s\n\n",
           appearance->base_color[0], appearance->base_color[1],
           appearance->base_color[2],
           phenotype->alive ? "" : "，该基因组合致死，不生成实例");
}
