/*
 * Mini JSON reader for the morph target test reference files.
 * Only supports the subset emitted by gen_morph_assets.py:
 * objects, arrays, strings, numbers, true/false. No allocations beyond the file copy.
 */
#ifndef MINI_JSON_H
#define MINI_JSON_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
    const char *cur;
} JsonReader;

static void jr_init(JsonReader *r, const char *text)
{
    r->cur = text;
}

static void jr_skip_ws(JsonReader *r)
{
    while (*r->cur == ' ' || *r->cur == '\t' || *r->cur == '\n' || *r->cur == '\r') r->cur++;
}

static bool jr_expect(JsonReader *r, char c)
{
    jr_skip_ws(r);
    if (*r->cur != c) return false;
    r->cur++;
    return true;
}

static bool jr_object_open(JsonReader *r)  { return jr_expect(r, '{'); }
static bool jr_array_open(JsonReader *r)   { return jr_expect(r, '['); }
static bool jr_object_close(JsonReader *r) { return jr_expect(r, '}'); }
static bool jr_array_close(JsonReader *r)  { return jr_expect(r, ']'); }

static bool jr_object_more(JsonReader *r)
{
    jr_skip_ws(r);
    return *r->cur != '}';
}

static bool jr_array_more(JsonReader *r)
{
    jr_skip_ws(r);
    return *r->cur != ']';
}

static void jr_comma(JsonReader *r)
{
    jr_skip_ws(r);
    if (*r->cur == ',') r->cur++;
}

static bool jr_key(JsonReader *r, char *out, size_t outSize)
{
    jr_skip_ws(r);
    if (*r->cur != '"') return false;
    r->cur++;
    size_t i = 0;
    while (*r->cur && *r->cur != '"' && (i + 1) < outSize) out[i++] = *r->cur++;
    out[i] = '\0';
    if (*r->cur != '"') return false;
    r->cur++;
    jr_skip_ws(r);
    if (*r->cur != ':') return false;
    r->cur++;
    return true;
}

static bool jr_number(JsonReader *r, double *out)
{
    jr_skip_ws(r);
    char *end = NULL;
    *out = strtod(r->cur, &end);
    if (end == r->cur) return false;
    r->cur = end;
    return true;
}

static char *load_text_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)size, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}

#endif // MINI_JSON_H
