#ifndef CAT_HOUSE_APPEARANCE_H
#define CAT_HOUSE_APPEARANCE_H

#include "gene_lib.h"
#include "texbake.h"

/* 把基因库的表型翻译成贴图烘焙库的绘制指令 */
void appearance_build_pattern(const GeneLibPhenotype *phenotype, TexbakePattern *pattern);

#endif
