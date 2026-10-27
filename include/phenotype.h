#ifndef CAT_HOUSE_PHENOTYPE_H
#define CAT_HOUSE_PHENOTYPE_H

#include <cglm/cglm.h>

#include "gene_lib.h"

/* 表型在渲染层的外观解释结果 */
typedef struct {
    vec3 base_color;    /* 基础毛色（调色板查表） */
    vec3 accent_color;  /* 花纹颜色：一期只打印，二期烘焙贴图 */
    vec3 eye_color;
    int has_pattern;
} PhenotypeAppearance;

/* 把机器可读表型翻译成渲染参数 */
void phenotype_appearance(const GeneLibPhenotype *phenotype, PhenotypeAppearance *out);

/* 控制台输出：文本描述 + 关键表型字段 */
void phenotype_print(const GeneLibGene *gene, const GeneLibPhenotype *phenotype,
                     const PhenotypeAppearance *appearance);

#endif
