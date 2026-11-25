#ifndef PLIKE_UML_H
#define PLIKE_UML_H

#include "plike/diag.h"
#include "plike/arena.h"

/* Diagrams written as text, in PlantUML or Mermaid: the kinds that can be
 * walked through. */

typedef enum { UML_NONE, UML_SEQUENCE, UML_STATE, UML_CLASS, UML_GRAPH } UmlKind;

typedef enum { UML_MESSAGE, UML_NOTE, UML_DIVIDER, UML_GROUP, UML_ELSE, UML_END, UML_ACTIVATE, UML_DEACTIVATE } UmlRowKind;

typedef struct {
    UmlRowKind kind;
    int from, to;       /* participants */
    const char *text;
    const char *group;  /* alt, loop, opt... */
    bool dashed;
    int line;
} UmlRow;

typedef enum { UML_PLAIN, UML_START, UML_STOP, UML_DECISION, UML_ACTOR, UML_USECASE, UML_COMPONENT, UML_DATA } UmlShape;

typedef struct {
    const char *id;
    const char *text;
    UmlShape shape;
    const char *inside; /* package or composite state */
    int line;
} UmlNode;

typedef struct {
    int from, to;
    const char *text;
    bool dashed;
    int line;
} UmlEdge;

typedef struct {
    const char *name;
    const char *kind;   /* class, interface, abstract, enum */
    const char *members[64];
    int member_count;
    int line;
} UmlClass;

typedef struct {
    int from, to;
    const char *kind;   /* extends, implements, has, owns, uses, knows */
    const char *text;
} UmlRelation;

typedef struct {
    UmlKind kind;
    const char *title;
    bool mermaid;
    struct { const char *id, *name; bool actor; } people[64];
    int people_count;
    UmlRow *rows;
    int row_count, row_capacity;
    UmlNode *nodes;
    int node_count, node_capacity;
    UmlEdge *edges;
    int edge_count, edge_capacity;
    UmlClass *classes;
    int class_count, class_capacity;
    UmlRelation *relations;
    int relation_count, relation_capacity;
} Uml;

bool is_uml_path(const char *path);
/* Problems go to diag; NULL when nothing could be made of the text. */
Uml *uml_parse(const Source *source, Arena *arena, Diag *diag);
void uml_dump(const Uml *uml, StrBuf *out);

#endif
