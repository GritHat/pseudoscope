#include "plike/blocks.h"
#include "plike/uml.h"
#include "unit.h"

typedef struct {
    Arena arena;
    Source source;
    Diag diag;
    Uml *uml;
} Drawn;

static void draw(Drawn *d, const char *path, const char *text) {
    memset(d, 0, sizeof *d);
    d->source = (Source){path, text, strlen(text)};
    d->diag = (Diag){.source = &d->source, .out = fopen("/dev/null", "w")};
    d->uml = uml_parse(&d->source, &d->arena, &d->diag);
}

static void erase(Drawn *d) {
    arena_free(&d->arena);
    fclose(d->diag.out);
}

static void test_sequence(void) {
    Drawn d;
    draw(&d, "s.puml", "@startuml\nactor Ann\nparticipant \"The server\" as S\nAnn -> S : hello\n"
                       "alt good\nS --> Ann: yes\nelse bad\nS --> Ann: no\nend\n@enduml\n");
    CHECK(d.uml && d.uml->kind == UML_SEQUENCE);
    if (d.uml) {
        CHECK(d.uml->people_count == 2 && d.uml->people[0].actor);
        CHECK_STR(d.uml->people[1].name, "The server");
        CHECK(d.uml->row_count == 6);
        CHECK(d.uml->rows[0].from == 0 && d.uml->rows[0].to == 1 && !d.uml->rows[0].dashed);
        CHECK(d.uml->rows[1].kind == UML_GROUP && d.uml->rows[2].dashed && d.uml->rows[5].kind == UML_END);
    }
    erase(&d);
    draw(&d, "s.mmd", "sequenceDiagram\n  A->>B: ping\n  B-->>A: pong\n");
    CHECK(d.uml && d.uml->kind == UML_SEQUENCE && d.uml->mermaid && d.uml->row_count == 2 && d.uml->rows[1].dashed);
    erase(&d);
}

static void test_state(void) {
    Drawn d;
    draw(&d, "s.puml", "@startuml\n[*] --> Open\nOpen --> Closed : close\nClosed --> Open : open\nClosed --> [*]\n@enduml\n");
    CHECK(d.uml && d.uml->kind == UML_STATE && d.uml->node_count == 4 && d.uml->edge_count == 4);
    if (d.uml) CHECK_STR(d.uml->edges[1].text, "close");
    erase(&d);
    draw(&d, "s.mmd", "stateDiagram-v2\n  [*] --> Off\n  Off --> On : press\n");
    CHECK(d.uml && d.uml->kind == UML_STATE && d.uml->edge_count == 2);
    erase(&d);
}

static void test_class(void) {
    Drawn d;
    draw(&d, "c.puml", "@startuml\nclass Animal {\n +name: String\n +speak()\n}\nclass Dog\nAnimal <|-- Dog\n@enduml\n");
    CHECK(d.uml && d.uml->kind == UML_CLASS && d.uml->class_count == 2 && d.uml->relation_count == 1);
    if (d.uml) {
        CHECK(d.uml->classes[0].member_count == 2);
        CHECK_STR(d.uml->relations[0].kind, "extends");
        CHECK(d.uml->relations[0].from == 1 && d.uml->relations[0].to == 0); /* a Dog is an Animal */
    }
    erase(&d);
}

static void test_graph(void) {
    Drawn d;
    draw(&d, "g.puml", "@startuml\nstart\nif (hungry?) then (yes)\n:eat;\nelse (no)\n:sleep;\nendif\nstop\n@enduml\n");
    CHECK(d.uml && d.uml->kind == UML_GRAPH);
    int decisions = 0;
    for (int i = 0; d.uml && i < d.uml->node_count; i++) decisions += d.uml->nodes[i].shape == UML_DECISION;
    CHECK(decisions == 1);
    erase(&d);
    draw(&d, "g.mmd", "flowchart TD\n A[Start] --> B{ok?}\n B -->|yes| C[Done]\n B -->|no| A\n");
    CHECK(d.uml && d.uml->kind == UML_GRAPH && d.uml->node_count == 3 && d.uml->edge_count == 3);
    erase(&d);
}

static void test_nothing(void) {
    static const char *const BAD[] = {"", "@startuml\n@enduml\n", "hello", "@startuml\nalt\nend\nend\n"};
    for (size_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        Drawn d;
        draw(&d, "x.puml", BAD[i]);
        CHECK(!d.uml || d.diag.errors || d.uml->row_count || d.uml->node_count || d.uml->kind == UML_NONE);
        erase(&d);
    }
    CHECK(is_uml_path("a/b.puml") && is_uml_path("x.mmd") && !is_uml_path("x.plike") && !is_uml_path("puml"));
}

static void test_plan(void) {
    Drawn d;
    draw(&d, "p.mmd", "gantt\n dateFormat YYYY-MM-DD\n section One\n A :a, 2026-01-05, 3d\n B :after a, 2d\n C :crit, 4d\n M :milestone, after a, 0d\n");
    CHECK(d.uml && d.uml->kind == UML_GANTT && d.uml->task_count == 4);
    if (d.uml && d.uml->task_count == 4) {
        int y, m, day;
        uml_date(d.uml->first_day, &y, &m, &day);
        CHECK(y == 2026 && m == 1 && day == 5);
        CHECK(d.uml->tasks[0].start == 0 && d.uml->tasks[0].end == 3);
        CHECK(d.uml->tasks[1].start == 3 && d.uml->tasks[1].days == 2); /* after a */
        CHECK(d.uml->tasks[2].start == 5 && d.uml->tasks[2].critical);  /* after the one before it */
        CHECK(d.uml->tasks[3].milestone && d.uml->tasks[3].start == 3);
        CHECK_STR(d.uml->tasks[0].section, "One");
    }
    erase(&d);
    draw(&d, "p.puml", "@startgantt\n[A] lasts 5 days\n[B] lasts 2 days\n[A] -> [B]\nthen [C] lasts 1 week\n@endgantt\n");
    CHECK(d.uml && d.uml->kind == UML_GANTT && d.uml->task_count == 3 && d.uml->first_day == -1);
    if (d.uml && d.uml->task_count == 3) {
        CHECK(d.uml->tasks[1].start == 5 && d.uml->tasks[2].start == 7 && d.uml->tasks[2].days == 7);
    }
    erase(&d);
    /* days and dates */
    int y, m, day;
    uml_date(0, &y, &m, &day);
    CHECK(y == 1970 && m == 1 && day == 1);
    uml_date(19782, &y, &m, &day);
    CHECK(y == 2024 && m == 2 && day == 29);
}

static void test_entities(void) {
    Drawn d;
    draw(&d, "e.mmd", "erDiagram\n CUSTOMER ||--o{ ORDER : places\n ORDER }|..|{ LINE-ITEM : has\n CUSTOMER {\n string name PK\n }\n");
    CHECK(d.uml && d.uml->kind == UML_ER && d.uml->class_count == 3 && d.uml->relation_count == 2);
    if (d.uml && d.uml->relation_count == 2) {
        CHECK_STR(d.uml->relations[0].from_many, "one");
        CHECK_STR(d.uml->relations[0].to_many, "zero or more");
        CHECK_STR(d.uml->relations[1].from_many, "one or more");
        CHECK_STR(d.uml->classes[2].name, "LINE-ITEM");
        CHECK(d.uml->classes[0].member_count == 1);
    }
    erase(&d);
}

static void test_trees(void) {
    Drawn d;
    draw(&d, "m.mmd", "mindmap\n  root((Top))\n    A\n      A1\n    B\n");
    CHECK(d.uml && d.uml->kind == UML_TREE && d.uml->node_count == 4);
    if (d.uml && d.uml->node_count == 4) {
        CHECK_STR(d.uml->nodes[0].text, "Top");
        CHECK(d.uml->nodes[2].parent == 1 && d.uml->nodes[2].depth == 2 && d.uml->nodes[3].parent == 0);
    }
    erase(&d);
    draw(&d, "w.puml", "@startwbs\n* All\n** Half\n*** Quarter\n** Other\n@endwbs\n");
    CHECK(d.uml && d.uml->kind == UML_TREE && d.uml->node_count == 4 && d.uml->nodes[3].parent == 0);
    erase(&d);
    draw(&d, "t.mmd", "timeline\n title Years\n 2001 : one : two\n 2002 : three\n");
    CHECK(d.uml && d.uml->kind == UML_TREE && d.uml->node_count == 6);
    erase(&d);
    draw(&d, "p.mmd", "pie title Parts\n \"A\" : 30\n \"B\" : 70\n");
    CHECK(d.uml && d.uml->kind == UML_PIE && d.uml->node_count == 2 && d.uml->nodes[1].value == 70);
    erase(&d);
    draw(&d, "g.dot", "digraph g {\n a -> b -> c [label=\"x\"];\n b [label=\"B\", shape=diamond];\n}\n");
    CHECK(d.uml && d.uml->kind == UML_GRAPH && d.uml->node_count == 3 && d.uml->edge_count == 2);
    if (d.uml && d.uml->edge_count == 2) {
        CHECK_STR(d.uml->edges[1].text, "x");
        CHECK_STR(d.uml->nodes[1].text, "B");
        CHECK(d.uml->nodes[1].shape == UML_DECISION);
    }
    erase(&d);
    CHECK(looks_like_uml("  sequenceDiagram\n A->>B: x\n") && looks_like_uml("@startuml\n@enduml") && !looks_like_uml("procedure main()\n"));
}

static void test_documents(void) {
    Arena arena = {0};
    static const char MARKDOWN[] = "# Title\n\ntext\n\n## Part\n\n```mermaid\npie\n \"A\" : 1\n```\n\n```python\nprint(1)\n```\n\n"
                                   "~~~pseint\nAlgoritmo X\nEscribir 1\nFinAlgoritmo\n~~~\n";
    Source md = {"notes.md", MARKDOWN, sizeof MARKDOWN - 1};
    Blocks *b = blocks_read(&md, &arena);
    CHECK(b->count == 2 && b->outline && b->outline->node_count == 3);
    if (b->count == 2) {
        CHECK(b->items[0].diagram && b->items[0].line == 8 && b->items[0].lines == 2);
        CHECK(!b->items[1].diagram && b->items[1].dialect == DIALECT_PSEINT);
        CHECK_STR(b->items[0].title, "Part");
        /* the lines of a block are where they were in the document */
        int lines = 0;
        for (const char *c = b->items[1].text; *c; c++) lines += *c == '\n';
        CHECK(lines == 20 && strstr(b->items[1].text, "Escribir 1") && !strstr(b->items[1].text, "print"));
    }
    static const char LATEX[] = "\\section{One}\n\\begin{algorithmic}\n\\Function{F}{n}\n\\State $r \\gets n \\cdot 2$\n"
                                "\\State \\Return $r$\n\\EndFunction\n\\end{algorithmic}\n";
    Source tex = {"paper.tex", LATEX, sizeof LATEX - 1};
    b = blocks_read(&tex, &arena);
    CHECK(b->count == 1 && b->latex);
    if (b->count == 1) {
        CHECK(strstr(b->items[0].text, "Funcion res_ <- F(n)") != NULL);
        CHECK(strstr(b->items[0].text, "r <- n * 2") != NULL);
        CHECK_STR(b->items[0].title, "One");
    }
    arena_free(&arena);
    char file[64];
    CHECK(block_number("a/notes.md#3", file, sizeof file) == 3 && strcmp(file, "a/notes.md") == 0);
    CHECK(block_number("plain.md", file, sizeof file) == 0 && strcmp(file, "plain.md") == 0);
    CHECK(guess_dialect("Algoritmo x\nFinAlgoritmo") == DIALECT_PSEINT && guess_dialect("algoritmo \"x\"\n") == DIALECT_VISUALG);
    CHECK(guess_dialect("programa\n{\n}") == DIALECT_PORTUGOL && guess_dialect("DECLARE X : INTEGER\n") == DIALECT_CAMBRIDGE);
    CHECK(guess_dialect("procedure main()\nbegin\nend main\n") == DIALECT_PLIKE);
    CHECK(file_kind("x.pscope", NULL) == FILE_PROGRAM && file_kind("x.md", NULL) == FILE_DOCUMENT && file_kind("x.dot", NULL) == FILE_DIAGRAM);
    CHECK(file_kind("LICENSE", "Permission is hereby granted") == FILE_OTHER);
    CHECK(file_kind("notes", "procedure main()\nbegin\n    print(1)\nend main\n") == FILE_PROGRAM);
}

void test_uml(void) {
    test_plan();
    test_entities();
    test_trees();
    test_documents();
    test_sequence();
    test_state();
    test_class();
    test_graph();
    test_nothing();
}
