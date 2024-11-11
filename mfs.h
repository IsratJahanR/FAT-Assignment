#ifndef MFS_H
#define MFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <unistd.h>

#define Mx_FILENAME_LENGTH 256
#define SECTOR_SIZE 512
#define Mx_COMMAND_LENGTH 1024

#define ATTRIBUTE_SYSTEM 0x04
#define ATTRIBUTE_VOLUME_ID 0x08
#define ATTRIBUTE_DIRECTORY 0x10
#define ATTRIBUTE_READ_ONLY 0x01
#define ATTRIBUTE_HIDDEN 0x02
#define ATTRIBUTE_ARCHIVE 0x20
#define EOC 0x0FFFFFF8

#define FORMAT_HEX 0
#define FORMAT_ASCII 1
#define FORMAT_DEC 2

int open_filesystem(const char *filename);
void close_filesystem(void);
int save_filesystem(const char *newname);
void print_info(void);
void process_command(char *cmd);

#endif 