// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#include "fat.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <time.h>

//------------------------------------------------------------------------------
#define CHECK_ERROR(err) \
  if (err) { \
    printf("\033[31merror:\033[0m %s(%d) (line %d)\n", fat_get_error(err), err, __LINE__); \
    fat_umount(&g_fat); \
    fclose(g_file); \
    return 0; \
  }

//------------------------------------------------------------------------------
static bool disk_read(uint8_t* buf, uint32_t sect);
static bool disk_write(const uint8_t* buf, uint32_t sect);

//------------------------------------------------------------------------------
static FILE* g_file;
static Fat g_fat;
static const char* g_months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static DiskOps g_ops =
{
  .read  = disk_read,
  .write = disk_write,
};

//------------------------------------------------------------------------------
static bool disk_init(const char* path)
{
  g_file = fopen(path, "r+");
  return g_file != NULL;
}

//------------------------------------------------------------------------------
static bool disk_read(uint8_t* buf, uint32_t sect)
{
  if (fseek(g_file, sect * 512, SEEK_SET))
    return false;

  return 1 == fread(buf, 512, 1, g_file);
}

//------------------------------------------------------------------------------
static bool disk_write(const uint8_t* buf, uint32_t sect)
{
  if (fseek(g_file, sect * 512, SEEK_SET))
    return false;

  return 1 == fwrite(buf, 512, 1, g_file);
}

//------------------------------------------------------------------------------
void print_info(DirInfo* info)
{
  printf("%5d   %s %02d   %02d:%02d   %.*s%c\n",
      info->size, g_months[info->modified.month - 1], info->modified.day,
      info->modified.hour, info->modified.min, 
      info->name_len, info->name, info->attr & FAT_ATTR_DIR ? '/' : ' ');
}

//------------------------------------------------------------------------------
int main(int argc, const char** argv)
{
  int err, cnt;
  File file;
  Dir dir;
  DirInfo info;
  char buf[1024];

  if (argc != 2)
  {
    printf("Usage: ./demo disk.img\n");
    return 0;
  }

  if (!disk_init(argv[1]))
    return 0;

  // You can scan the drive for FAT32 partitions before mounting to avoid 
  // allocating excess fat structures.
  fat_probe(&g_ops, 0);

  // Mount the partition under /mnt
  fat_mount(&g_ops, 0, &g_fat, "mnt");

  // Here are some examples:
  printf("-------------------------------\n");
  printf("Example 0: read large file in chunks\n");
  {
    err = fat_file_open(&file, "/mnt/source/fat.c", FAT_READ);
    CHECK_ERROR(err);

    for (;;)
    {
      err = fat_file_read(&file, buf, 512, &cnt);
      CHECK_ERROR(err);

      printf("%.*s", cnt, buf);
      if (cnt != 512)
        break;
    }

    err = fat_file_close(&file);
    CHECK_ERROR(err);
  }


  printf("-------------------------------\n");
  printf("Example 1: Overwrite file\n");
  {
    err = fat_file_open(&file, "/mnt/test.txt", FAT_WRITE | FAT_CREATE | FAT_TRUNC);
    CHECK_ERROR(err);

    err = fat_file_write(&file, "Hello\n", 6, &cnt);
    CHECK_ERROR(err);

    printf("Written %d bytes\n", cnt);

    err = fat_file_close(&file); // IMPORTANT
    CHECK_ERROR(err);
  }

  
  printf("-------------------------------\n");
  printf("Example 2: ls\n");
  {
    fat_dir_open(&dir, "/mnt");
    for (;;)
    {
      err = fat_dir_read(&dir, &info);
      if (err == FAT_ERR_EOF)
        break;
      CHECK_ERROR(err);

      print_info(&info);

      err = fat_dir_next(&dir);
      CHECK_ERROR(err);
    }
  }

  printf("-------------------------------\n");
  printf("Example 3: create directories\n");
  {
    err = fat_dir_create(&dir, "/mnt/dummy");
    CHECK_ERROR(err);
    err = fat_dir_create(&dir, "/mnt/dummy2");
    CHECK_ERROR(err);
  }

  printf("-------------------------------\n");
  printf("Example 4: unlink directory\n");
  {
    err = fat_unlink("/mnt/dummy");
    CHECK_ERROR(err);
  }

  printf("-------------------------------\n");
  printf("Example 5: read from file\n");
  {
    err = fat_file_open(&file, "/mnt/numbers/numbers.txt", FAT_READ);
    CHECK_ERROR(err);

    err = fat_file_read(&file, buf, 1024, &cnt);
    CHECK_ERROR(err);

    printf("File size: %d File offset: %d\n", file.size, file.offset);
    printf("%.*s\n", cnt, buf);

    err = fat_file_close(&file); // IMPORTANT
    CHECK_ERROR(err);

    if (cnt < 0)
      CHECK_ERROR(cnt);
  }

  printf("-------------------------------\n");
  printf("Example 6: get file info\n");
  {
    err = fat_stat("/mnt/numbers/numbers.txt", &info);
    CHECK_ERROR(err);

    print_info(&info);
  }

  printf("-------------------------------\n");
  printf("Example 7: get directory info\n");
  {
    err = fat_stat("/mnt/numbers", &info);
    CHECK_ERROR(err);

    print_info(&info);
  }

  fat_umount(&g_fat); // IMPORTANT
  fclose(g_file);
  return 0;
}

//------------------------------------------------------------------------------
// It is possible use an RTC module to obtain the current date and time. If this 
// function is not implemented, the library will default to 01/01/1980 00:00:00.

void fat_get_timestamp(Timestamp* ts)
{
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  ts->day   = tm.tm_mday;
  ts->month = tm.tm_mon + 1;
  ts->year  = tm.tm_year + 1900;
  ts->hour  = tm.tm_hour;
  ts->min   = tm.tm_min;
  ts->sec   = tm.tm_sec;
}