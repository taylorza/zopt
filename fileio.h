#ifndef FILEIO_H_
#define FILEIO_H_

#include <stdint.h>

#ifndef _strdup
#define _strdup strdup
#endif

void init_file_io(void) MYCC;
int8_t open_file(const char *filename) MYCC;
int8_t create_file(const char *filename) MYCC;
int16_t read_line(int8_t f, char *buf, int16_t size) MYCC;
int16_t write_line(int8_t f, char *buf, int16_t size) MYCC;
void close_file(int8_t f) MYCC;

void delete_file(const char* filename) MYCC;
void rename_file(const char* origname, const char* newname) MYCC;
#endif //FILEIO_H_