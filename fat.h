// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#ifndef FAT_H
#define FAT_H

//------------------------------------------------------------------------------
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

//------------------------------------------------------------------------------
#define SECT_SIZE 512

//------------------------------------------------------------------------------
enum
{
  FAT_ERR_NONE     =  0,
  FAT_ERR_NOFAT    = -1,
  FAT_ERR_BROKEN  = -2,
  FAT_ERR_DISK     = -3,
  FAT_ERR_PARAM    = -4,
  FAT_ERR_PATH     = -5,
  FAT_ERR_EOF      = -6,
  FAT_ERR_DENIED   = -7,
  FAT_ERR_FULL     = -8,
};

enum
{
  FAT_ATTR_NONE     = 0x00,
  FAT_ATTR_RO       = 0x01,
  FAT_ATTR_HIDDEN   = 0x02,
  FAT_ATTR_SYS      = 0x04,
  FAT_ATTR_LABEL    = 0x08,
  FAT_ATTR_DIR      = 0x10,
  FAT_ATTR_ARCHIVE  = 0x20,
  FAT_ATTR_LFN      = 0x0f,
};

enum
{
  FAT_SEEK_SET,
  FAT_SEEK_END,
  FAT_SEEK_CURR,
};

//------------------------------------------------------------------------------
typedef struct
{
  bool (*read)(uint8_t* buf, uint32_t sect);
  bool (*write)(const uint8_t* buf, uint32_t sect);
} disk_ops_t;

typedef struct
{
  uint8_t hour;
  uint8_t min;
  uint8_t sec;

  uint8_t day;
  uint8_t month;
  uint16_t year;
} timestamp_t;

typedef struct fat_t
{
  struct fat_t* next;
  disk_ops_t ops;
  char path[32];
  int pathlen;

  uint32_t sect_per_clust;
  uint32_t clust_cnt;
  uint32_t info_sect;
  uint32_t fat_sect[2];
  uint32_t data_sect;  
  uint32_t root_sect;

  uint32_t info_last;
  uint32_t info_cnt;
  bool info_dirty;

  uint8_t win[SECT_SIZE];
  uint32_t win_sect;
  bool win_dirty;
  
  uint8_t name[260];
  int namelen;
  uint8_t crc;
} fat_t;

typedef struct
{
  timestamp_t created;
  timestamp_t modified;

  uint32_t size;
  uint8_t attr;

  char name[256];
  int namelen;
} dir_info_t;

typedef struct
{
  fat_t* fat;
  uint8_t attr;
  uint32_t first_clust;
  uint32_t clust;
  uint32_t sect;
  int idx;
} dir_t;

typedef struct
{
  dir_t dir;
  uint32_t first_clust;
  uint32_t clust;
  uint32_t sect;
  uint32_t off;
  uint32_t size;

  uint8_t buf[SECT_SIZE];
  uint32_t buf_sect;
  bool buf_dirty;

  bool modified;
  bool accessed;
  bool read;
  bool write;
} file_t;

//------------------------------------------------------------------------------
int fat_mount(disk_ops_t* ops, int part_num, fat_t* fs, const char* path);
int fat_umount(fat_t* fs);

int fat_fopen(file_t* file, const char* path, const char* mode);
int fat_fclose(file_t* file);
int fat_fread(file_t* file, void* buf, int len);
int fat_fwrite(file_t* file, const void* buf, int len);
int fat_fprintf(file_t* file, const char* str, ...);
int fat_fseek(file_t* file, int offset, int seek);
int fat_ftell(file_t* file);
int fat_fsize(file_t* file);
int fat_fsync(file_t* file);
int fat_unlink(const char* path);

int fat_mkdir(const char* path);
int fat_opendir(dir_t* dir, const char* path);
int fat_readdir(dir_t* dir, dir_info_t* info);
int fat_nextdir(dir_t* dir);

const char* fat_get_error(int err);

void fat_get_timestamp(timestamp_t* dt);

#endif
