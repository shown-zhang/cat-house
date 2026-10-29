#include <stdio.h>
#include <string.h>

#include "CuTest.h"
#include "gene_lib.h"

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

int main(void)
{
    CuSuite *suite = CuSuiteNew();
    CuString *summary = CuStringNew();

    SUITE_ADD_TEST(suite, test_generate);
    SUITE_ADD_TEST(suite, test_describe);

    CuSuiteRun(suite);
    CuSuiteSummary(suite, summary);
    printf("%s", summary->buffer);

    int failed = suite->failCount;

    CuStringDelete(summary);
    CuSuiteDelete(suite);

    return failed > 0 ? 1 : 0;
}
