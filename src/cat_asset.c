#include "cat_asset.h"

#include <stdio.h>
#include <string.h>

/* 每个性状的取值函数：返回文件名片段，返回 NULL 表示处于默认值、不参与命名 */
typedef const char *(*TraitTag)(const GeneLibPhenotype *phenotype);

typedef struct {
    TraitTag tag;
} CatAssetRule;

static const char *ear_tag(const GeneLibPhenotype *phenotype)
{
    switch (phenotype->ear_shape) {
    case GENE_LIB_EAR_FOLD:
        return "ear-fold";
    case GENE_LIB_EAR_CURL:
        return "ear-curl";
    default:
        return NULL;
    }
}

static const char *hair_texture_tag(const GeneLibPhenotype *phenotype)
{
    switch (phenotype->hair_texture) {
    case GENE_LIB_HAIR_TEXTURE_REX:
        return "fur-rex";
    case GENE_LIB_HAIR_TEXTURE_HAIRLESS:
        return "fur-hairless";
    default:
        return NULL;
    }
}

static const char *hair_length_tag(const GeneLibPhenotype *phenotype)
{
    return phenotype->hair_length == GENE_LIB_HAIR_LONG ? "fur-long" : NULL;
}

static const char *leg_tag(const GeneLibPhenotype *phenotype)
{
    switch (phenotype->leg_length) {
    case GENE_LIB_LEG_SHORT:
        return "leg-short";
    case GENE_LIB_LEG_SEVERE:
        return "leg-severe";
    default:
        return NULL;
    }
}

static const char *tail_tag(const GeneLibPhenotype *phenotype)
{
    switch (phenotype->tail_form) {
    case GENE_LIB_TAIL_SHORT:
        return "tail-short";
    case GENE_LIB_TAIL_BOB:
        return "tail-bob";
    default:
        return NULL;
    }
}

/*
 * 性状表：数组顺序就是优先级。
 * 耳形对轮廓影响最大排在前面，尾部影响最小排在后面；
 * 缺资产回退时从尾部开始裁剪。
 */
static const CatAssetRule RULES[] = {
    {ear_tag},
    {hair_texture_tag},
    {hair_length_tag},
    {leg_tag},
    {tail_tag},
};

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    fclose(file);
    return 1;
}

int cat_asset_traits(const GeneLibPhenotype *phenotype,
                     CatAssetTrait *traits, int capacity)
{
    if (!phenotype || !traits || capacity <= 0) {
        return 0;
    }

    int count = 0;
    const int rule_count = (int)(sizeof(RULES) / sizeof(RULES[0]));
    for (int i = 0; i < rule_count && count < capacity; ++i) {
        const char *tag = RULES[i].tag(phenotype);
        if (!tag) {
            continue;
        }
        traits[count].tag = tag;
        count++;
    }
    return count;
}

/* 用前 count 个片段拼出候选路径；缓冲区不足时返回 0 */
static int build_candidate(const CatAssetTrait *traits, int count,
                           char *out, size_t capacity)
{
    char suffix[160] = "";
    size_t used = 0;

    for (int i = 0; i < count; ++i) {
        const size_t remain = sizeof(suffix) - used;
        if (remain <= 1) {
            return 0;
        }
        const int written = snprintf(suffix + used, remain, "_%s", traits[i].tag);
        if (written < 0 || (size_t)written >= remain) {
            return 0;
        }
        used += (size_t)written;
    }

    snprintf(out, capacity, "assets/cat%s.glb", suffix);
    return 1;
}

void cat_asset_select(const GeneLibPhenotype *phenotype,
                      const char *default_path,
                      char *out, size_t capacity)
{
    if (!phenotype || !out || capacity == 0 || !default_path) {
        return;
    }

    CatAssetTrait traits[CAT_ASSET_MAX_TRAITS];
    const int count = cat_asset_traits(phenotype, traits, CAT_ASSET_MAX_TRAITS);

    /* 从完整组合开始，每次裁掉优先级最低的性状再试 */
    for (int keep = count; keep > 0; --keep) {
        char path[256];
        if (!build_candidate(traits, keep, path, sizeof(path))) {
            continue;
        }
        if (file_exists(path)) {
            snprintf(out, capacity, "%s", path);
            return;
        }
    }

    snprintf(out, capacity, "%s", default_path);
}
