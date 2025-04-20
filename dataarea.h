#ifndef DATAAREA_H_
#define DATAAREA_H_

#define MAX_LINE_LENGTH 80
#define STR_TBL_SIZE 101

extern char line[MAX_LINE_LENGTH];
extern char tmp_line[MAX_LINE_LENGTH*2];
extern char output_filename[MAX_LINE_LENGTH];

char* hash(const char* s);
void free_strtbl(void);

#endif //DATAAREA_H_