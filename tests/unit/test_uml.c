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

void test_uml(void) {
    test_sequence();
    test_state();
    test_class();
    test_graph();
    test_nothing();
}
