// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#ifndef FAT_H
#define FAT_H

//------------------------------------------------------------------------------
#include <stdint.h>
#include <stdbool.h>

//------------------------------------------------------------------------------
enum
{
  FAT_ERR_NONE     =  0,
  FAT_ERR_NOFAT    = -1,
  FAT_ERR_BROKEN   = -2,
  FAT_ERR_IO     = -3,
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
  FAT_WRITE      = 0x01, // Open file for writing
  FAT_READ       = 0x02, // Open file for reading
  FAT_APPEND     = 0x04, // Set file offset to the end of the file
  FAT_TRUNC      = 0x08, // Truncate the file after opening
  FAT_CREATE     = 0x10, // Create the file if it do not exist

  FAT_ACCESSED   = 0x20, // do not use (internal)
  FAT_MODIFIED   = 0x40, // do not use (internal)
  FAT_FILE_DIRTY = 0x80, // do not use (internal)
};

enum
{
  FAT_SEEK_START,
  FAT_SEEK_CURR,
  FAT_SEEK_END,
};

//------------------------------------------------------------------------------
typedef struct
{
  bool (*read)(uint8_t* buf, uint32_t sect);
  bool (*write)(const uint8_t* buf, uint32_t sect);
} DiskOps;

typedef struct
{
  uint8_t hour;
  uint8_t min;
  uint8_t sec;
  uint8_t day;
  uint8_t month;
  uint16_t year;
} Timestamp;

typedef struct Fat
{
  struct Fat* next;
  DiskOps ops;
  uint32_t clust_msk;
  uint32_t clust_cnt;
  uint32_t info_sect;
  uint32_t fat_sect[2];
  uint32_t data_sect;  
  uint32_t root_clust;
  uint32_t last_used;
  uint32_t free_cnt;
  uint32_t sect;
  uint8_t buf[512];
  uint8_t flags;
  uint8_t clust_shift;
  uint8_t name_len;
  char name[32];
} Fat;

typedef struct
{
  Timestamp created;
  Timestamp modified;
  uint32_t size;
  uint8_t attr;
  char name[255];
  uint8_t name_len;
} DirInfo;

typedef struct
{
  Fat* fat;
  uint32_t sclust;
  uint32_t clust;
  uint32_t sect;
  uint16_t idx;
} Dir;

typedef struct
{
  Fat* fat;
  uint32_t dir_sect;
  uint32_t sclust;
  uint32_t clust;
  uint32_t sect;
  uint32_t size;
  uint32_t offset;
  uint16_t dir_idx;
  uint8_t attr;
  uint8_t flags;
  uint8_t buf[512];
} File;

//------------------------------------------------------------------------------
const char* fat_get_error(int err);

int fat_probe(DiskOps* ops, int partition);
int fat_mount(DiskOps* ops, int partition, Fat* fat, const char* path);
int fat_umount(Fat* fat);
int fat_sync(Fat* fat);

int fat_stat(const char* path, DirInfo* info);
int fat_unlink(const char* path);

int fat_file_open(File* file, const char* path, uint8_t flags);
int fat_file_close(File* file);
int fat_file_read(File* file, void* buf, int len, int* bytes);
int fat_file_write(File* file, const void* buf, int len, int* bytes);
int fat_file_seek(File* file, int offset, int seek);
int fat_file_sync(File* file);

int fat_dir_create(Dir* dir, const char* path);
int fat_dir_open(Dir* dir, const char* path);
int fat_dir_read(Dir* dir, DirInfo* info);
int fat_dir_rewind(Dir* dir);
int fat_dir_next(Dir* dir);

void fat_get_timestamp(Timestamp* ts);

#endif
