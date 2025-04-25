#ifndef DATAAREA_H_
#define DATAAREA_H_

#define MAX_LINE_LENGTH 80

typedef enum ErrorType {
    ERROR_NONE,
    ERROR_FILE_NOT_FOUND,
    ERROR_OUT_OF_MEMORY,
    ERROR_INVALID_RULE,
    ERROR_INVALID_EXPRESSION,
    ERROR_INVALID_BINDING,
} ErrorType;

extern char line[MAX_LINE_LENGTH];
extern char tmp_line1[MAX_LINE_LENGTH*2];
extern char tmp_line2[MAX_LINE_LENGTH];
extern char output_filename[MAX_LINE_LENGTH];

char* trim(char* s);
char* hash(const char* s);
void free_strtbl(void);

void error(ErrorType e);

#endif //DATAAREA_H_