#ifndef VOXELPILOT_JSON_UTILS_H
#define VOXELPILOT_JSON_UTILS_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void voxelpilot_json_write_escaped(FILE *fp, const char *text)
{
    const unsigned char *cursor;

    if (!fp) {
        return;
    }

    cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        unsigned char ch = *cursor++;
        if (ch == '"' || ch == '\\') {
            fputc('\\', fp);
            fputc((int)ch, fp);
        } else if (ch == '\n') {
            fputs("\\n", fp);
        } else if (ch == '\r') {
            fputs("\\r", fp);
        } else if (ch == '\t') {
            fputs("\\t", fp);
        } else if (ch < 32) {
            fprintf(fp, "\\u%04x", (unsigned int)ch);
        } else {
            fputc((int)ch, fp);
        }
    }
}

static inline const char *voxelpilot_json_field_value(const char *line, const char *field)
{
    static char pattern[80];

    if (!line || !field || !field[0]) {
        return NULL;
    }

    snprintf(pattern, sizeof(pattern), "\"%s\":", field);
    return strstr(line, pattern);
}

static inline int voxelpilot_json_extract_string_field(
    const char *line,
    const char *field,
    char       *out,
    size_t      out_size)
{
    const char *cursor;
    const char *end;
    size_t length;

    cursor = voxelpilot_json_field_value(line, field);
    if (!cursor || !out || out_size == 0) return 0;
    cursor = strchr(cursor, ':');
    if (!cursor) return 0;
    cursor = strchr(cursor, '"');
    if (!cursor) return 0;
    cursor++;
    end = cursor;
    while (*end && !(*end == '"' && end[-1] != '\\')) end++;
    if (*end != '"') return 0;
    length = (size_t)(end - cursor);
    if (length >= out_size) length = out_size - 1;
    memcpy(out, cursor, length);
    out[length] = '\0';
    return 1;
}

static inline int voxelpilot_json_extract_int_field(
    const char *line,
    const char *field,
    int        *out)
{
    const char *cursor;

    cursor = voxelpilot_json_field_value(line, field);
    if (!cursor || !out) return 0;
    cursor = strchr(cursor, ':');
    if (!cursor) return 0;
    return sscanf(cursor + 1, "%d", out) == 1;
}

static inline int voxelpilot_json_extract_float_field(
    const char *line,
    const char *field,
    float      *out)
{
    const char *cursor;

    cursor = voxelpilot_json_field_value(line, field);
    if (!cursor || !out) return 0;
    cursor = strchr(cursor, ':');
    if (!cursor) return 0;
    return sscanf(cursor + 1, "%f", out) == 1;
}

#ifdef __cplusplus
}
#endif

#endif /* VOXELPILOT_JSON_UTILS_H */