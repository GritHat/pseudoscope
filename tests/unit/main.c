#include "unit.h"

int unit_checks;
int unit_failures;

int main(void) {
    test_util();
    test_lexer();
    printf("unit: %d checks, %d failures\n", unit_checks, unit_failures);
    return unit_failures != 0;
}
