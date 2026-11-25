#include "plike/parser.h"
#include "unit.h"

static int parse_errors(const char *text, Config config) {
    Arena arena = {0};
    Source source = {"t", text, strlen(text)};
    FILE *sink = fopen("/dev/null", "w");
    Diag diag = {.source = &source, .out = sink};
    parse(lex(&source, &config, &arena, &diag), &config, &arena, &diag);
    fclose(sink);
    arena_free(&arena);
    return diag.errors;
}

#define ACCEPT(text) CHECK(parse_errors(text, CONFIG_DEFAULT) == 0)
#define REJECT(text) CHECK(parse_errors(text, CONFIG_DEFAULT) > 0)
#define BODY(statements) "procedure p()\nvar a: integer\nbegin\n" statements "\nend\nendprocedure"
#define PARAMS(list) "procedure p(" list ")\nbegin\nend\nendprocedure"

static void test_parameter_modes(void) {
    Arena arena = {0};
    Config config = CONFIG_DEFAULT;
    const char *text = PARAMS("a, b, out: c, d; e, in/out f, g, out h, i : real");
    Source source = {"t", text, strlen(text)};
    Diag diag = {.source = &source, .out = stderr};
    Program *program = parse(lex(&source, &config, &arena, &diag), &config, &arena, &diag);
    const ParamMode want[] = {MODE_IN, MODE_IN, MODE_OUT, MODE_OUT, MODE_IN,
                              MODE_INOUT, MODE_IN, MODE_OUT, MODE_OUT};
    int i = 0;
    for (VarDecl *d = program->functions->params; d; d = d->next, i++)
        CHECK(i < 9 && d->mode == want[i]);
    CHECK(i == 9 && diag.errors == 0);
    arena_free(&arena);
}

void test_parser(void) {
    test_parameter_modes();

    /* every way to close a body */
    ACCEPT("procedure p()\nbegin\nend\nend p");
    ACCEPT("procedure p()\nbegin\nend\nendprocedure");
    ACCEPT("procedure p()\nbegin\nend p");
    ACCEPT("procedure p()\nbegin\nendprocedure");
    ACCEPT("procedure p()\nbegin\nend\nend");
    REJECT("procedure p()\nbegin\nend");
    REJECT("procedure p()\nbegin\nend q");
    REJECT("procedure p()\nbegin\nend\nendfunction");
    ACCEPT("integer function f()\nbegin\nend\nendfunction\n"
           "point function g()\nbegin\nend\nendfunction");

    /* parameters */
    ACCEPT(PARAMS("in: n; in/out: A"));
    ACCEPT(PARAMS("in n integer, out: x,y,z"));
    ACCEPT(PARAMS("in A[n][n] : array of integer; B[n,m] : 2d array of real"));
    ACCEPT(PARAMS("in/out arr[]: array of integer"));
    ACCEPT(PARAMS("in A : array [1..n, 0..m] of real, C(1..n): array of real"));
    ACCEPT(PARAMS("in *p: integer, in q: integer**"));
    REJECT(PARAMS("in A[n][] : array of integer"));
    REJECT(PARAMS("in A: 2d array[n] of integer"));
    REJECT(PARAMS("in"));

    /* declarations */
    ACCEPT("var: a : integer **\nvar b, c[3], *d : real; e : logical");
    ACCEPT("type t: record x, y: real z: array[3] of integer next: t* end");
    ACCEPT("type t = record\nin_: record a: real end\nend");
    ACCEPT("var m: 2d array[2,2] of record v: record r: real end end");
    ACCEPT("function f(x, y)\n x: integer;\n y: real;\n f: real\nbegin\nend f");

    /* statements */
    ACCEPT(BODY("a := 1 ; a := 2"));
    ACCEPT(BODY("a := a\n*a := 2"));
    ACCEPT(BODY("a := (a\n* 2)"));
    ACCEPT(BODY("a := a +\n 1 - \n 2"));
    ACCEPT(BODY("if a = 1 then\nelseif a == 2 then\nelse if a < 3 then endif endif"));
    ACCEPT(BODY("for a := 10 to 1 step -2 do endfor"));
    ACCEPT(BODY("repeat a := 1 until a > 3\nreturn\nfoo"));
    ACCEPT(BODY("if a == 1 then return endif"));
    ACCEPT(BODY("print a\nprint(a, \"x\")\nprint (a+1)*2\nread a\nread(a)"));
    ACCEPT(BODY("@a := 3\nbegin var b: real\n b := 1 end"));
    REJECT(BODY("a = 1"));
    REJECT(BODY("a[] := 1"));
    REJECT(BODY("if a then"));
    REJECT(BODY("a + 1"));

    Config equals = CONFIG_DEFAULT;
    equals.assign = ASSIGN_EQUALS;
    CHECK(parse_errors(BODY("a = 1\nif a = 1 then a = 2 endif\nfor a = 1 to 3 do endfor"), equals) == 0);

    /* errors are recovered from: both are reported */
    CHECK(parse_errors(BODY("a := )\na := 1\na := ]"), CONFIG_DEFAULT) == 2);

    /* runaway nesting is an error, not a crash */
    Arena arena = {0};
    StrBuf deep = {0};
    strbuf_puts(&deep, "procedure p()\nbegin\na := ");
    for (int i = 0; i < 100000; i++) strbuf_puts(&deep, "(");
    REJECT(deep.data);
    strbuf_free(&deep);
    strbuf_puts(&deep, "procedure p()\nbegin\na := 1");
    for (int i = 0; i < 100000; i++) strbuf_puts(&deep, "+1");
    REJECT(deep.data);
    strbuf_free(&deep);
    strbuf_puts(&deep, "procedure p()\nbegin\n");
    for (int i = 0; i < 100000; i++) strbuf_puts(&deep, "if a then ");
    REJECT(deep.data);
    strbuf_free(&deep);
    arena_free(&arena);
}
