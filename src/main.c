#include <stdio.h>
#include "type.h"

int add(int a, int b)
{
    return a + b;
}



int main(void)
{
    int result = add(1, 2);
    printf("result = %d\n", result);
    printf("Hello, World!\n");
    return 0;
}
