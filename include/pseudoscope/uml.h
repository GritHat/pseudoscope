#ifndef PSEUDOSCOPE_UML_H
#define PSEUDOSCOPE_UML_H

#include "pseudoscope/diag.h"
#include "pseudoscope/arena.h"

/* Diagrams written as text, in PlantUML or Mermaid: the kinds that can be
 * walked through. */

typedef enum {
    UML_NONE, UML_SEQUENCE, UML_STATE, UML_CLASS, UML_GRAPH,
    UML_GANTT,   /* a plan: tasks along time */
    UML_ER,      /* entities and their relationships */
    UML_TREE,    /* mind maps, work breakdowns, outlines, timelines, journeys */
    UML_PIE      /* parts of a whole */
} UmlKind;

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
    int parent, depth;  /* trees; the root has parent -1 */
    double value;       /* pies */
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
    const char *kind;   /* extends, implements, has, owns, uses, knows; "relates" between entities */
    const char *text;
    const char *from_many, *to_many; /* entities: "one", "zero or one", "one or more", "zero or more" */
} UmlRelation;

/* A task of a plan. Days are counted from the first day of the plan. */
typedef struct {
    const char *id, *name, *section;
    long start, end;    /* end: the day after the last one; -1 until known */
    int days;           /* 0: a milestone */
    int after;          /* the task it waits for, or -1 */
    int previous;       /* the one written before it: it follows it when nothing else is said */
    bool done, active, critical, milestone, follows;
    int percent;        /* done so far, or -1 */
    int line;
} UmlTask;

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
    UmlTask *tasks;
    int task_count, task_capacity;
    long first_day;         /* of a plan with dates: days from 1970-01-01; else -1 */
    const char *tree_kind;  /* "mind map", "work breakdown", "timeline", "journey", "outline" */
} Uml;

/* A day counted from 1970-01-01, as year, month and day. */
void uml_date(long day, int *year, int *month, int *day_of_month);

bool is_uml_path(const char *path);
/* Whether a text without a telling name looks like a diagram. */
bool looks_like_uml(const char *text);
/* Problems go to diag; NULL when nothing could be made of the text. */
Uml *uml_parse(const Source *source, Arena *arena, Diag *diag);
void uml_dump(const Uml *uml, StrBuf *out);

#endif
