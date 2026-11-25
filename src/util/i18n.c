#include "plike/i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int now;                       /* 0: English */
static const CatalogEntry **sorted;   /* the entries of the language chosen, by text */
static int sorted_count;

int locale_count(void) { return CATALOG_COUNT + 1; }

const char *locale_code(int locale) { return locale > 0 && locale <= CATALOG_COUNT ? CATALOGS[locale - 1].code : "en"; }

const char *locale_name(int locale) { return locale > 0 && locale <= CATALOG_COUNT ? CATALOGS[locale - 1].name : "English"; }

int locale_now(void) { return now; }

static int by_text(const void *a, const void *b) {
    return strcmp((*(const CatalogEntry *const *)a)->id, (*(const CatalogEntry *const *)b)->id);
}

void locale_set(int locale) {
    if (locale < 0 || locale > CATALOG_COUNT) locale = 0;
    now = locale;
    free(sorted);
    sorted = NULL;
    sorted_count = 0;
    if (!now) return;
    const Catalog *c = &CATALOGS[now - 1];
    sorted = malloc(sizeof *sorted * (size_t)(c->count ? c->count : 1));
    if (!sorted) abort();
    for (int i = 0; i < c->count; i++) sorted[i] = &c->entries[i];
    sorted_count = c->count;
    qsort(sorted, (size_t)sorted_count, sizeof *sorted, by_text);
}

static bool same_word(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return b[n] == '\0';
}

bool locale_choose(const char *name) {
    if (!name) return false;
    size_t whole = strlen(name), code = strcspn(name, "_.@-");
    for (int i = 0; i < locale_count(); i++) {
        if (same_word(name, locale_code(i), code) || same_word(name, locale_name(i), whole)) {
            locale_set(i);
            return true;
        }
    }
    if (same_word(name, "c", code) || same_word(name, "posix", code)) {
        locale_set(0);
        return true;
    }
    return false;
}

void locale_from_environment(void) {
    static const char *const NAMES[] = {"PSEUDOSCOPE_LANG", "LC_ALL", "LC_MESSAGES", "LANG"};
    for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        const char *value = getenv(NAMES[i]);
        if (!value || !*value) continue;
        if (!locale_choose(value)) locale_set(0); /* a language we do not speak: English */
        return;
    }
    locale_set(0);
}

static const CatalogEntry *find(const char *english) {
    if (!now || !english) return NULL;
    CatalogEntry key = {.id = english};
    const CatalogEntry *wanted = &key;
    const CatalogEntry **found = bsearch(&wanted, sorted, (size_t)sorted_count, sizeof *sorted, by_text);
    return found ? *found : NULL;
}

const char *tr(const char *english) {
    const CatalogEntry *e = find(english);
    return e ? e->text[0] : english;
}

const char *tr_count(const char *one, const char *many, long count) {
    /* English, Italian and Spanish all tell one from many in the same way */
    const CatalogEntry *e = find(one);
    if (e && e->plural && e->text[1]) return count == 1 ? e->text[0] : e->text[1];
    return count == 1 ? one : many;
}

const char *tr_in(const char *context, const char *english) {
    char key[512];
    if (!now || snprintf(key, sizeof key, "%s\004%s", context, english) >= (int)sizeof key) return english;
    const CatalogEntry *e = find(key);
    return e ? e->text[0] : english;
}
