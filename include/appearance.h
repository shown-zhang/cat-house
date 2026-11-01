#ifndef CAT_HOUSE_APPEARANCE_H
#define CAT_HOUSE_APPEARANCE_H

#include "gene_lib.h"
#include "texbake.h"

/*
 * 把基因库的表型翻译成贴图烘焙库的绘制指令。
 * 产出的图层统一为 3D 图元（BLOB3D / STRIPE3D / GRADIENT3D / NOISE3D），
 * 供 texbake_bake_glb_surface / texbake_render_surface 使用。
 */
void appearance_build_pattern(const GeneLibPhenotype *phenotype, TexbakePattern *pattern);

#endif
