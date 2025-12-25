#include <stdint.h>
#include <stdio.h>

#include <z80.h>
#include <arch/zxn.h>
#include <arch/zxn/esxdos.h>
#include <errno.h>

#include "platform.h"
#include "fileio.h"

#define MAX_BUFFER_SIZE 128
#define MAX_FILES 3

typedef struct FileInfo {
    char readbuf[MAX_BUFFER_SIZE];
    char writebuf[MAX_BUFFER_SIZE];
    uint8_t handle;
    uint16_t r_offset;
    uint16_t w_offset;
    uint16_t r_bytes;
} FileInfo;

FileInfo files[MAX_FILES];

void init_file_io(void) MYCC {
    for(int8_t i=0; i<MAX_FILES; ++i) {
        files[i].handle = 255;
        files[i].r_offset = MAX_BUFFER_SIZE;
        files[i].w_offset = 0;
        files[i].r_bytes = 0;
    }
}

int8_t find_free_slot(void) MYCC {
    for(int8_t i=0; i<MAX_FILES; ++i) {
        if (files[i].handle == 255) return i;
    }
    return -1;
}

int16_t read_buffer(FileInfo *fi) MYCC {
    errno = 0;
    fi->r_bytes = esxdos_f_read(fi->handle, fi->readbuf, MAX_BUFFER_SIZE);
    if (errno) return -1;
    fi->r_offset = 0;
    return fi->r_bytes;
}

int8_t internal_open_file(const char *filename, unsigned char mode) MYCC {
    errno = 0; 
    int8_t fh = find_free_slot();
    if (fh < 0) return -1;
    FileInfo *fi = &files[fh];
    fi->r_offset = MAX_BUFFER_SIZE;
    fi->r_bytes = 0;
    fi->handle = esx_f_open(filename, mode);
    if (fi->handle == 255 && errno) return -1;    
    return fh;
}

int8_t open_file(const char *filename) MYCC {
    return internal_open_file(filename, ESXDOS_MODE_R | ESXDOS_MODE_OE);
}

int8_t create_file(const char *filename) MYCC { 
    return internal_open_file(filename, ESXDOS_MODE_W | ESXDOS_MODE_CT);    
}

int16_t peek_char(FileInfo* fi) MYCC {
    if (fi->r_offset >= fi->r_bytes) {
        int16_t bytesread = read_buffer(fi);
        if (bytesread <= 0) return -1;
    }
    return fi->readbuf[fi->r_offset];
}

int16_t read_char(FileInfo* fi) MYCC {
    if (fi->r_offset >= fi->r_bytes) {
        int16_t bytesread = read_buffer(fi);
        if (bytesread <= 0) return -1;
    }
    return fi->readbuf[fi->r_offset++];
}

int16_t read_line(int8_t f, char* buf, int16_t size) MYCC {
    zx_border(1);
    FileInfo* fi = &files[f];
    int16_t count = 0;
    while (count < size) {
        int16_t ch = read_char(fi);
        if (ch < 0) {
            if (!count) count = -1;
            break;
        }
        if (ch == '\r' || ch == '\n') {
            int16_t ch2 = peek_char(fi);
            if (ch == '\r' && ch2 == '\n') read_char(fi);
            break;
        }
        *buf++ = (char)ch;
        ++count;
    }
    *buf = '\0';
    return count;
}

int16_t flush_write_buffer(FileInfo *fi) MYCC {
    errno = 0;
    int16_t byteswritten = esxdos_f_write(fi->handle, fi->writebuf, fi->w_offset);
    if (errno != 0) return -1;
    fi->w_offset = 0;
    return byteswritten;
}

uint16_t write_byte(FileInfo *fi, uint8_t b) MYCC {
    if (fi->w_offset == MAX_BUFFER_SIZE) {
        if (flush_write_buffer(fi) == -1) return -1;
    }
    fi->writebuf[fi->w_offset++] = b;
    return 1;
}

int16_t write_line(int8_t f, char *buf, int16_t size) MYCC {
    zx_border(0);
    FileInfo *fi = &files[f]; 
    int16_t byteswritten = 0;
    int8_t write_count;
    for(int16_t i=0; i<size; ++i) {
        write_count = write_byte(fi, buf[i]);
        if (write_count == -1) return -1;
        byteswritten += write_count;
    }
    write_count = write_byte(fi, '\n');
    if (write_count == -1) return -1;
        
    return byteswritten;
}

void close_file(int8_t f) MYCC {
    FileInfo *fi = &files[f]; 
    flush_write_buffer(fi);
    esxdos_f_close(fi->handle);
    fi->handle = 255;
    fi->r_offset = MAX_BUFFER_SIZE;
    fi->w_offset = 0;
    fi->r_bytes = 0;
}

void delete_file(const char* filename) MYCC {
    esx_f_unlink(filename);
}

void rename_file(const char* origname, const char* newname) MYCC {
    esx_f_rename(origname, newname);
}

