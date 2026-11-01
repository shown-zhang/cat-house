#include <stdio.h>
#include <string.h>

#include "CuTest.h"
#include "appearance.h"
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

int main(void)
{
    CuSuite *suite = CuSuiteNew();
    CuString *summary = CuStringNew();

    SUITE_ADD_TEST(suite, test_generate);
    SUITE_ADD_TEST(suite, test_describe);
    SUITE_ADD_TEST(suite, test_appearance);

    CuSuiteRun(suite);
    CuSuiteSummary(suite, summary);
    printf("%s", summary->buffer);

    int failed = suite->failCount;

    CuStringDelete(summary);
    CuSuiteDelete(suite);

    return failed > 0 ? 1 : 0;
}
