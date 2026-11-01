#include "appearance.h"

#include <string.h>

/*
 * 表型 → TexbakePattern。
 *
 * 当前烘焙库的正式用法是「表面烘焙」：图层必须使用 3D 图元（BLOB3D / STRIPE3D /
 * GRADIENT3D / NOISE3D），在归一化对象空间求值后再落到 UV 纹素，因此图案跨 UV 岛连续。
 * 基因库给的是 UV 空间指令，这里负责把它翻译成对象空间的语义位置。
 *
 * 归一化对象空间（cat.glb）：x 左右、y 向上、z 身体前后，坐标大致落在 [-1, 1]。
 */

#define APPEARANCE_BODY_HALF_LENGTH 0.85f
#define APPEARANCE_BLOB_RADIUS_MIN 0.03f
#define APPEARANCE_BLOB_RADIUS_MAX 0.32f

/* 颜色索引 → 调色板 RGB */
static TexbakeColor layer_color(const TexbakePalette *palette, uint8_t index)
{
    return texbake_palette_get(palette, (int)index);
}

/*
 * 由表型内容派生噪声种子：同一只猫每次烘焙结果一致，不同花色不同纹理。
 * 基因库不提供稳定标识，这里用表型字段做一次轻量哈希。
 */
static uint32_t appearance_seed(const GeneLibPhenotype *phenotype)
{
    uint32_t seed = 0x9E3779B9u;
    seed ^= (uint32_t)phenotype->base_color * 0x85EBu;
    seed ^= (uint32_t)phenotype->pattern_layout.count * 0xC2B2u;

    for (int i = 0; i < phenotype->pattern_layout.count; ++i) {
        const GeneLibPatternLayer *layer = &phenotype->pattern_layout.layers[i];
        const uint32_t packed = ((uint32_t)layer->kind << 16) |
                                ((uint32_t)layer->color << 8) |
                                (uint32_t)layer->param;
        seed = seed * 16777619u ^ packed;
        seed = seed * 16777619u ^ (uint32_t)layer->count;
    }

    return seed ? seed : 1u;
}

/*
 * REGION（元球斑块）→ BLOB3D。
 *
 * 基因库的圆心/半径是 UV 坐标，无法直接换算成对象空间，因此按文档建议改用
 * 「部位语义」：
 *   - 圆心 x → 沿身体前后的分布；
 *   - param → 斑块大小；
 *   - 椭球锚在身体表面（侧腹 / 背部 / 腹部）循环排布，否则球心落在体内时
 *     根本碰不到表面，图案就看不见；
 *   - 白色斑块只放在侧腹与腹部，并用 normal_y 过滤掉背部。
 */
static void region_anchor(int index, int is_white, float *x, float *y)
{
    const float side = 0.22f;

    if (is_white) {
        switch (index % 3) {
        case 0:
            *x = side;
            *y = -0.15f;
            break;
        case 1:
            *x = -side;
            *y = -0.15f;
            break;
        default:
            *x = 0.0f;
            *y = -0.32f;
            break;
        }
        return;
    }

    switch (index % 4) {
    case 0:
        *x = side;
        *y = 0.10f;
        break;
    case 1:
        *x = -side;
        *y = 0.10f;
        break;
    case 2:
        *x = 0.0f;
        *y = 0.40f; /* 背部 */
        break;
    default:
        *x = 0.0f;
        *y = -0.25f; /* 腹侧 */
        break;
    }
}

static void fill_region(const GeneLibPatternLayer *src, TexbakeLayer *dst)
{
    int count = src->count;
    if (count <= 0) {
        return;
    }
    if (count > TEXBAKE_MAX_BLOBS) {
        count = TEXBAKE_MAX_BLOBS;
    }

    const int is_white = src->color == GENE_LIB_COLOR_WHITE;
    const float param = (float)src->param / 255.0f;

    /* UV 半径在对象空间没有意义，只用来做一点大小扰动 */
    const float base_radius = 0.12f + 0.16f * param;

    if (is_white) {
        texbake_filter_init(&dst->filter);
        dst->filter.enabled = 1;
        dst->filter.normal_y_max = 0.45f; /* 只画侧面与腹部 */
    }

    dst->blob3d_count = count;
    for (int k = 0; k < count; ++k) {
        const GeneLibMetaCircle *circle = &src->u.circles[k];
        const float u = (float)circle->x / (float)GENE_LIB_PATTERN_UV_MAX;

        float radius = base_radius * (0.80f + 0.40f * ((float)circle->r / (float)GENE_LIB_PATTERN_UV_MAX));
        if (radius < APPEARANCE_BLOB_RADIUS_MIN) {
            radius = APPEARANCE_BLOB_RADIUS_MIN;
        }
        if (radius > APPEARANCE_BLOB_RADIUS_MAX) {
            radius = APPEARANCE_BLOB_RADIUS_MAX;
        }

        region_anchor(k, is_white, &dst->blobs3d[k].x, &dst->blobs3d[k].y);
        dst->blobs3d[k].z = -APPEARANCE_BODY_HALF_LENGTH + 2.0f * APPEARANCE_BODY_HALF_LENGTH * u;
        dst->blobs3d[k].rx = radius * 1.25f;
        dst->blobs3d[k].ry = radius;
        dst->blobs3d[k].rz = radius * 1.15f;
        dst->blobs3d[k].softness = 0.35f;
    }
}

/*
 * STRIPE（贝塞尔条纹）→ STRIPE3D。
 * 基因库用曲线数量表达条纹密度、用线宽表达粗细；对象空间里沿身体前后轴重复。
 */
static void fill_stripe(const GeneLibPatternLayer *src, TexbakeLayer *dst, uint32_t seed)
{
    int count = src->count;
    if (count < 1) {
        count = 1;
    }
    if (count > 4) {
        count = 4;
    }

    /* 条纹越少，间距越大；落在文档建议的 period 0.15~0.35 附近 */
    const float period = 0.6f / ((float)count + 1.0f);

    float weight = (float)src->param / 255.0f;
    if (src->count > 0) {
        const GeneLibStripeCurve *curve = &src->u.stripes[0];
        weight = 0.5f * ((float)curve->w0 + (float)curve->w1) / 255.0f;
    }

    float width = period * (0.28f + 0.45f * weight);
    if (width > period * 0.6f) {
        width = period * 0.6f;
    }

    dst->stripe3d.ax = 0.0f;
    dst->stripe3d.ay = 0.0f;
    dst->stripe3d.az = 1.0f; /* 沿身体前后轴重复 */
    dst->stripe3d.period = period;
    dst->stripe3d.width = width;
    dst->stripe3d.phase = (float)src->param / 255.0f;
    dst->stripe3d.warp = 0.35f;
    dst->stripe3d.warp_freq = 2.0f;
    dst->seed = seed;
}

/*
 * GRADIENT（重点色）→ GRADIENT3D。
 * 基因库只说「强度」和「范围」，渐变轴由这里补：沿身体前后轴，从一端过渡到中部。
 */
static void fill_gradient(const GeneLibPatternLayer *src, TexbakeLayer *dst)
{
    float span = 0.55f + 0.02f * (float)src->count;
    if (span > 1.10f) {
        span = 1.10f;
    }

    dst->strength = (float)src->param / 255.0f;
    dst->gradient3d.ax = 0.0f;
    dst->gradient3d.ay = 0.0f;
    dst->gradient3d.az = -1.0f;
    dst->gradient3d.start = -1.0f; /* 一端为底色 */
    dst->gradient3d.end = span;    /* 向另一端混入图层色 */
}

/*
 * NOISE（银化 / 烟色）→ NOISE3D。
 * 基因库的 count 与烘焙库的 frequency 都是「格点数」语义，直接传；夹到建议区间。
 */
static void fill_noise(const GeneLibPatternLayer *src, TexbakeLayer *dst, uint32_t seed)
{
    float frequency = (float)src->count;
    if (frequency < 4.0f) {
        frequency = 4.0f;
    }
    if (frequency > 40.0f) {
        frequency = 40.0f;
    }

    dst->frequency = frequency;
    dst->strength = (float)src->param / 255.0f;
    dst->seed = seed;
}

void appearance_build_pattern(const GeneLibPhenotype *phenotype, TexbakePattern *pattern)
{
    if (!phenotype || !pattern) {
        return;
    }

    TexbakePalette palette;
    texbake_default_palette(&palette);

    texbake_pattern_init(pattern);
    pattern->base_color = layer_color(&palette, (uint8_t)phenotype->base_color);
    pattern->bake_size = 1024;
    pattern->detail_strength = 1.0f;

    const uint32_t seed = appearance_seed(phenotype);
    const GeneLibPatternLayout *layout = &phenotype->pattern_layout;

    for (int i = 0; i < layout->count && i < GENE_LIB_MAX_PATTERN_LAYERS; ++i) {
        if (pattern->layer_count >= TEXBAKE_MAX_LAYERS) {
            break;
        }

        const GeneLibPatternLayer *src = &layout->layers[i];
        TexbakeLayer *dst = &pattern->layers[pattern->layer_count];
        memset(dst, 0, sizeof(*dst));
        dst->color = layer_color(&palette, src->color);

        /* 表面烘焙只接受 3D 图元 */
        switch (src->kind) {
        case GENE_LIB_PATTERN_REGION:
            dst->kind = TEXBAKE_LAYER_BLOB3D;
            fill_region(src, dst);
            break;
        case GENE_LIB_PATTERN_STRIPE:
            dst->kind = TEXBAKE_LAYER_STRIPE3D;
            fill_stripe(src, dst, seed);
            break;
        case GENE_LIB_PATTERN_GRADIENT:
            dst->kind = TEXBAKE_LAYER_GRADIENT3D;
            fill_gradient(src, dst);
            break;
        case GENE_LIB_PATTERN_NOISE:
            dst->kind = TEXBAKE_LAYER_NOISE3D;
            fill_noise(src, dst, seed);
            break;
        default:
            continue;
        }

        pattern->layer_count++;
    }
}
