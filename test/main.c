#include <stdio.h>

#include "CuTest.h"

int main(void)
{
    CuSuite *suite = CuSuiteNew();
    CuString *summary = CuStringNew();

    /* 新增用例：SUITE_ADD_TEST(suite, 测试函数名); */

    CuSuiteRun(suite);
    CuSuiteSummary(suite, summary);
    printf("%s", summary->buffer);

    int failed = suite->failCount;

    CuStringDelete(summary);
    CuSuiteDelete(suite);

    return failed > 0 ? 1 : 0;
}
