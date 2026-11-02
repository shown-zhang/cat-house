#ifndef CAT_HOUSE_CAT_ASSET_H
#define CAT_HOUSE_CAT_ASSET_H

#include <stddef.h>

#include "gene_lib.h"

/*
 * 模型资产选择：把表型里影响「几何轮廓」的性状翻译成资产变体名，
 * 拼出 assets/cat_<tag>_<tag>....glb，缺资产时逐级回退。
 *
 * 命名只包含「偏离默认值」的性状，因此普通猫仍然是 assets/cat.glb：
 *   ear   straight / fold / curl        -> "", ear-fold, ear-curl
 *   fur   long / rex / hairless         -> "", fur-long, fur-rex, fur-hairless
 *   leg   normal / short / severe       -> "", leg-short, leg-severe
 *   tail  long / short / bob            -> "", tail-short, tail-bob
 *
 * 扩展一个新性状：在 cat_asset.c 的 RULES 表里加一条（取值函数 + 位置即优先级），
 * 路径拼接与回退逻辑不用改。位置越靠前，缺资产时越优先保留。
 */

/* 单次选择最多考虑的性状数量 */
#define CAT_ASSET_MAX_TRAITS 8

typedef struct {
    /* 文件名片段，如 "ear-fold" */
    const char *tag;
} CatAssetTrait;

/*
 * 收集当前表型偏离默认值的性状片段，按优先级从高到低排列。
 * 返回片段数量（不超过 capacity）。
 */
int cat_asset_traits(const GeneLibPhenotype *phenotype,
                     CatAssetTrait *traits, int capacity);

/*
 * 选择模型资产：从最完整的组合开始，每次裁掉优先级最低的性状再试，
 * 全部未命中时使用 default_path。out 返回最终路径。
 */
void cat_asset_select(const GeneLibPhenotype *phenotype,
                      const char *default_path,
                      char *out, size_t capacity);

#endif
