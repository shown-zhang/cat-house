#include <stdio.h>
#include <string.h>

#include "CuTest.h"
#include "appearance.h"
#include "cat_asset.h"
#include "gene_lib.h"
#include "texbake.h"

/* 基因生成：连续生成不应失败 */
static void test_generate(CuTest *tc)
{
    gene_lib_seed(20261027u);

    for (int i = 0; i < 64; ++i) {
        GeneLibGene gene;
        CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_generate(&gene));
    }
}

/* 基因描述：生成的文本不应为空 */
static void test_describe(CuTest *tc)
{
    gene_lib_seed(7u);

    GeneLibGene gene;
    CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_generate(&gene));

    char buffer[1024];
    const int length = gene_lib_describe(&gene, buffer, sizeof(buffer));
    CuAssertTrue(tc, length > 0);
    CuAssertTrue(tc, strlen(buffer) > 0);
}

/* 表型 → 烘焙指令：图层数量与尺寸必须落在烘焙库允许范围内 */
static void test_appearance(CuTest *tc)
{
    gene_lib_seed(99u);

    for (int i = 0; i < 32; ++i) {
        GeneLibGene gene;
        CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_generate(&gene));

        GeneLibPhenotype phenotype;
        CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_phenotype(&gene, &phenotype));

        TexbakePattern pattern;
        appearance_build_pattern(&phenotype, &pattern);

        CuAssertTrue(tc, pattern.layer_count >= 0);
        CuAssertTrue(tc, pattern.layer_count <= TEXBAKE_MAX_LAYERS);
        CuAssertTrue(tc, pattern.bake_size > 0);

        /* 表面烘焙只接受 3D 图元 */
        for (int l = 0; l < pattern.layer_count; ++l) {
            CuAssertIntEquals(tc, 1, pattern.layers[l].kind >= TEXBAKE_LAYER_BLOB3D);
        }
    }
}

/* 资产选择：只有偏离默认值的性状参与命名，且耳形优先保留 */
static void test_asset(CuTest *tc)
{
    GeneLibPhenotype phenotype;
    memset(&phenotype, 0, sizeof(phenotype));

    CatAssetTrait traits[CAT_ASSET_MAX_TRAITS];

    /* 全默认的表型：没有档案片段，直接用默认资产 */
    CuAssertIntEquals(tc, 0, cat_asset_traits(&phenotype, traits, CAT_ASSET_MAX_TRAITS));

    char path[256];
    cat_asset_select(&phenotype, "assets/cat.glb", path, sizeof(path));
    CuAssertStrEquals(tc, "assets/cat.glb", path);

    phenotype.ear_shape = GENE_LIB_EAR_FOLD;
    phenotype.leg_length = GENE_LIB_LEG_SHORT;
    phenotype.tail_form = GENE_LIB_TAIL_BOB;

    const int count = cat_asset_traits(&phenotype, traits, CAT_ASSET_MAX_TRAITS);
    CuAssertIntEquals(tc, 3, count);
    CuAssertStrEquals(tc, "ear-fold", traits[0].tag);
    CuAssertStrEquals(tc, "leg-short", traits[1].tag);
    CuAssertStrEquals(tc, "tail-bob", traits[2].tag);
}

int main(void)
{
    CuSuite *suite = CuSuiteNew();
    CuString *summary = CuStringNew();

    SUITE_ADD_TEST(suite, test_generate);
    SUITE_ADD_TEST(suite, test_describe);
    SUITE_ADD_TEST(suite, test_appearance);
    SUITE_ADD_TEST(suite, test_asset);

    CuSuiteRun(suite);
    CuSuiteSummary(suite, summary);
    printf("%s", summary->buffer);

    int failed = suite->failCount;

    CuStringDelete(summary);
    CuSuiteDelete(suite);

    return failed > 0 ? 1 : 0;
}
