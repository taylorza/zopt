#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "dataarea.h"

#define STR_TBL_SIZE 97

const char* errmsg[] = {
    "OK",
    "File not found",
    "Out of memory",
    "Invalid rule",
    "Invalid expression",
    "Invalid binding",
    "Expected pattern",
    "Expected replacement/constraint",
    "Too many lines",
    "Multi-line constraint not supported",
};

char line[MAX_LINE_LENGTH];
char tmp_line1[MAX_LINE_LENGTH * 2];
char tmp_line2[MAX_LINE_LENGTH];
char output_filename[MAX_LINE_LENGTH];
char window[MAX_WINDOW_SIZE][MAX_LINE_LENGTH];

typedef struct HNode {
    char* str;
    struct HNode* next;
} HNode;

HNode* strtbl[STR_TBL_SIZE] = { 0 };

char* trim(char* s) {
    char* end = s + strlen(s) - 1;

    while (s < end && *s == ' ')
        ++s;

    while (end >= s && *end == ' ')
        --end;
    end[1] = '\0';
    return s;
}

char* hash(const char* s) {
    int h = 0;

    const char* p = s;
    while (*p) {
        h += *p++;
    }
    h %= STR_TBL_SIZE;

    HNode* entry = strtbl[h];
    while (entry) {
        if (strcmp(s, entry->str) == 0) {
            return entry->str;
        }
        entry = entry->next;
    }

    entry = malloc(sizeof(HNode));
    if (!entry) exit(1);

    entry->str = malloc(strlen(s) + 1);
    if (!entry->str) exit(1);

    entry->next = strtbl[h];
    strtbl[h] = entry;

    strcpy(entry->str, s);
    return entry->str;
}

void free_strtbl(void) {
    uint16_t size = 0;
    for (int i = 0; i < STR_TBL_SIZE; ++i) {
        HNode* p = strtbl[i];
        while (p) {
            size += strlen(p->str)+1;
            HNode* n = p->next;
            free(p);
            p = n;
        }
        strtbl[i] = NULL;
    }
}

void error(ErrorType e, int lineno) {
    if (lineno) {
        printf("Error: line %d: %s\n", lineno, errmsg[e]);
    }
    else {
        printf("Error: %s\n", errmsg[e]);
    }
    exit(1);
}
