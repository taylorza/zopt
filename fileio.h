#ifndef FILEIO_H_
#define FILEIO_H_

#include <stdint.h>

#ifndef _strdup
#define _strdup strdup
#endif

void init_file_io(void);
int8_t open_file(const char *filename);
int8_t create_file(const char *filename);
int16_t read_line(int8_t f, char *buf, int16_t size);
int16_t write_line(int8_t f, char *buf, int16_t size);
void close_file(int8_t f);

void delete_file(const char* filename);
void rename_file(const char* origname, const char* newname);
#endif //FILEIO_H_