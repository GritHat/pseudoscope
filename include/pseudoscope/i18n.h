#ifndef PSEUDOSCOPE_I18N_H
#define PSEUDOSCOPE_I18N_H

#include <stdbool.h>

/* The languages the program speaks.
 *
 * It is written in English: T("text") gives the same text in the language
 * chosen, from the catalogs in lang/ (one .po file each, compiled in by
 * tools/po2c). A text without a translation stays in English. Language 0 is
 * English; the others follow in the order of their files. */

typedef struct {
    const char *id;      /* the English text, as in the source */
    const char *plural;  /* its form for many, or NULL */
    const char *text[2]; /* in this language: for one, and for many */
} CatalogEntry;

typedef struct {
    const char *code, *name; /* "it", "Italiano" */
    const CatalogEntry *entries;
    int count;
} Catalog;

extern const Catalog CATALOGS[];
extern const int CATALOG_COUNT;

int locale_count(void);
const char *locale_code(int locale);
const char *locale_name(int locale);
int locale_now(void);
void locale_set(int locale);

/* "it", "IT", "it_IT.UTF-8", "Italiano": false when it is none of ours. */
bool locale_choose(const char *name);

/* PSEUDOSCOPE_LANG, or what the system says (LC_ALL, LC_MESSAGES, LANG). */
void locale_from_environment(void);

#if defined(__GNUC__) || defined(__clang__)
#define PSEUDOSCOPE_FORMAT_ARG(n) __attribute__((format_arg(n)))
#else
#define PSEUDOSCOPE_FORMAT_ARG(n)
#endif

const char *tr(const char *english) PSEUDOSCOPE_FORMAT_ARG(1);
const char *tr_count(const char *one, const char *many, long count) PSEUDOSCOPE_FORMAT_ARG(1) PSEUDOSCOPE_FORMAT_ARG(2);
const char *tr_in(const char *context, const char *english) PSEUDOSCOPE_FORMAT_ARG(2);

#define T(text) tr(text)
#define TN(one, many, count) tr_count(one, many, count)
#define TC(context, text) tr_in(context, text) /* the same English, said differently in two places */
#define N_(text) text                          /* translated where it is shown, with tr() */

#endif
