#include <stdio.h>
#include <string.h>

#include "CuTest.h"
#include "gene_lib.h"
#include "phenotype.h"

/* 基因生成 → 表型 → 渲染参数，全流程不应出现非法值 */
static void test_generate_and_phenotype(CuTest *tc)
{
    gene_lib_seed(20261027u);

    for (int i = 0; i < 64; ++i) {
        GeneLibGene gene;
        CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_generate(&gene));

        GeneLibPhenotype phenotype;
        CuAssertIntEquals(tc, GENE_LIB_OK, gene_lib_phenotype(&gene, &phenotype));

        CuAssertTrue(tc, phenotype.sex == GENE_LIB_MALE || phenotype.sex == GENE_LIB_FEMALE);
        CuAssertTrue(tc, phenotype.base_color >= GENE_LIB_COLOR_BLACK &&
                             phenotype.base_color <= GENE_LIB_COLOR_WHITE);
        CuAssertTrue(tc, phenotype.pattern_layout.count <= GENE_LIB_MAX_PATTERN_LAYERS);

        PhenotypeAppearance appearance;
        phenotype_appearance(&phenotype, &appearance);
        for (int channel = 0; channel < 3; ++channel) {
            CuAssertTrue(tc, appearance.base_color[channel] >= 0.0f &&
                                 appearance.base_color[channel] <= 1.0f);
            CuAssertTrue(tc, appearance.eye_color[channel] >= 0.0f &&
                                 appearance.eye_color[channel] <= 1.0f);
        }
        CuAssertIntEquals(tc, phenotype.pattern_layout.count > 0 ? 1 : 0,
                          appearance.has_pattern);
    }
}

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

int main(void)
{
    CuSuite *suite = CuSuiteNew();
    CuString *summary = CuStringNew();

    SUITE_ADD_TEST(suite, test_generate_and_phenotype);
    SUITE_ADD_TEST(suite, test_describe);

    CuSuiteRun(suite);
    CuSuiteSummary(suite, summary);
    printf("%s", summary->buffer);

    int failed = suite->failCount;

    CuStringDelete(summary);
    CuSuiteDelete(suite);

    return failed > 0 ? 1 : 0;
}
