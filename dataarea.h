#ifndef DATAAREA_H_
#define DATAAREA_H_

#define MAX_LINE_LENGTH 80
#define MAX_WINDOW_SIZE 15

typedef enum ErrorType {
    ERROR_NONE,
    ERROR_FILE_NOT_FOUND,
    ERROR_OUT_OF_MEMORY,
    ERROR_INVALID_RULE,
    ERROR_INVALID_EXPRESSION,
    ERROR_INVALID_BINDING,
    ERROR_EXPECTED_PATTERN,
    ERROR_EXPECTED_REPLACEMENT_OR_CONSTRAINT,
    ERROR_TOO_MANY_LINES,
    ERROR_MULTILINE_CONSTRAINT,
} ErrorType;

extern char line[];
extern char tmp_line1[];
extern char tmp_line2[];
extern char output_filename[];
extern char window[][MAX_LINE_LENGTH];

char* trim(char* s);
char* hash(const char* s);
void free_strtbl(void);

void error(ErrorType e, int lineno);

#endif //DATAAREA_H_