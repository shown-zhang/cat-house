#include "appearance.h"

#include <string.h>

/* 基因库图层 → 烘焙库图层（两者顺序一致，但仍显式映射，避免依赖枚举值） */
static TexbakeLayerKind map_kind(uint8_t kind)
{
    switch (kind) {
    case GENE_LIB_PATTERN_REGION:
        return TEXBAKE_LAYER_REGION;
    case GENE_LIB_PATTERN_STRIPE:
        return TEXBAKE_LAYER_STRIPE;
    case GENE_LIB_PATTERN_GRADIENT:
        return TEXBAKE_LAYER_GRADIENT;
    case GENE_LIB_PATTERN_NOISE:
        return TEXBAKE_LAYER_NOISE;
    default:
        return TEXBAKE_LAYER_REGION;
    }
}

void appearance_build_pattern(const GeneLibPhenotype *phenotype, TexbakePattern *pattern)
{
    if (!phenotype || !pattern) {
        return;
    }

    TexbakePalette palette;
    texbake_default_palette(&palette);

    texbake_pattern_init(pattern);
    pattern->base_color = texbake_palette_get(&palette, (int)phenotype->base_color);
    pattern->bake_size = 1024;
    pattern->detail_strength = 1.0f;

    const GeneLibPatternLayout *layout = &phenotype->pattern_layout;
    for (int i = 0; i < layout->count && i < TEXBAKE_MAX_LAYERS; ++i) {
        const GeneLibPatternLayer *src = &layout->layers[i];
        if (i >= GENE_LIB_MAX_PATTERN_LAYERS) {
            break;
        }

        TexbakeLayer *dst = &pattern->layers[pattern->layer_count];
        memset(dst, 0, sizeof(*dst));
        dst->kind = map_kind(src->kind);
        dst->color = texbake_palette_get(&palette, (int)src->color);

        switch (dst->kind) {
        case TEXBAKE_LAYER_REGION: {
            int count = src->count;
            if (count > TEXBAKE_MAX_BLOBS) {
                count = TEXBAKE_MAX_BLOBS;
            }
            dst->blob_count = count;
            for (int k = 0; k < count; ++k) {
                dst->blobs[k].x = (float)src->u.circles[k].x;
                dst->blobs[k].y = (float)src->u.circles[k].y;
                dst->blobs[k].radius = (float)src->u.circles[k].r;
            }
            dst->threshold = (float)src->param / 255.0f;
            break;
        }
        case TEXBAKE_LAYER_STRIPE: {
            int count = src->count;
            if (count > TEXBAKE_MAX_CURVES) {
                count = TEXBAKE_MAX_CURVES;
            }
            dst->curve_count = count;
            for (int k = 0; k < count; ++k) {
                const GeneLibStripeCurve *curve = &src->u.stripes[k];
                dst->curves[k].x0 = (float)curve->pts[0];
                dst->curves[k].y0 = (float)curve->pts[1];
                dst->curves[k].x1 = (float)curve->pts[2];
                dst->curves[k].y1 = (float)curve->pts[3];
                dst->curves[k].x2 = (float)curve->pts[4];
                dst->curves[k].y2 = (float)curve->pts[5];
                /* 基因库线宽 0..255，烘焙坐标 0..1024 */
                dst->curves[k].w0 = (float)curve->w0 * 4.0f;
                dst->curves[k].w1 = (float)curve->w1 * 4.0f;
            }
            break;
        }
        case TEXBAKE_LAYER_GRADIENT:
            dst->strength = (float)src->param / 255.0f;
            /* 基因库没有给渐变轴，这里用自上而下 */
            dst->gx0 = 0.0f;
            dst->gy0 = 0.0f;
            dst->gx1 = 0.0f;
            dst->gy1 = 1024.0f;
            break;
        case TEXBAKE_LAYER_NOISE:
            dst->frequency = (float)src->count;
            dst->strength = (float)src->param / 255.0f;
            dst->seed = 12345u;
            break;
        default:
            continue;
        }

        pattern->layer_count++;
    }
}
