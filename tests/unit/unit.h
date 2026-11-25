#ifndef PLIKE_UNIT_H
#define PLIKE_UNIT_H

#include <stdio.h>
#include <string.h>

extern int unit_checks;
extern int unit_failures;
extern int unit_argc; /* programs to travel through */
extern char **unit_argv;

#define CHECK(condition)                                                       \
    do {                                                                       \
        unit_checks++;                                                         \
        if (!(condition)) {                                                    \
            unit_failures++;                                                   \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,   \
                    #condition);                                               \
        }                                                                      \
    } while (0)

#define CHECK_STR(actual, expected)                                            \
    do {                                                                       \
        const char *actual_ = (actual), *expected_ = (expected);               \
        unit_checks++;                                                         \
        if (!actual_ || strcmp(actual_, expected_) != 0) {                     \
            unit_failures++;                                                   \
            fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__,  \
                    __LINE__, expected_, actual_ ? actual_ : "(null)");        \
        }                                                                      \
    } while (0)

void test_util(void);
void test_lexer(void);
void test_parser(void);
void test_vm(void);
void test_uml(void);

#endif
