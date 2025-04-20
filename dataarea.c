#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "dataarea.h"

char line[MAX_LINE_LENGTH];
char tmp_line[MAX_LINE_LENGTH*2];
char output_filename[MAX_LINE_LENGTH];

typedef struct HNode {
    char* str;
    struct HNode* next;
} HNode;

HNode* strtbl[STR_TBL_SIZE] = { 0 };

char* hash(const char* s) {
    int h = 0;

    const char* p = s;
    while (*p) {
        h += *p++ * 31;
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
    for (int i = 0; i < STR_TBL_SIZE; ++i) {
        HNode* p = strtbl[i];
        while (p) {
            HNode* n = p->next;
            free(p);
            p = n;
        }
        strtbl[i] = NULL;
    }
}