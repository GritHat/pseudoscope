#include "unit.h"

int unit_checks;
int unit_failures;

int unit_argc;
char **unit_argv;

int main(int argc, char **argv) {
    unit_argc = argc;
    unit_argv = argv;
    test_util();
    test_lexer();
    test_parser();
    test_vm();
    test_uml();
    printf("unit: %d checks, %d failures\n", unit_checks, unit_failures);
    return unit_failures != 0;
}
