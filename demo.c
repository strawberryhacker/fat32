// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "fat.h"

//------------------------------------------------------------------------------
static FILE* g_file;
static fat_t g_fat;
static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

//------------------------------------------------------------------------------
static bool disk_init(const char* path)
{
  g_file = fopen(path, "r+");
  return g_file != NULL;
}

//------------------------------------------------------------------------------
static bool disk_read(uint8_t* buf, uint32_t sect)
{
  if (fseek(g_file, sect * SECT_SIZE, SEEK_SET))
    return false;

  return 1 == fread(buf, SECT_SIZE, 1, g_file);
}

//------------------------------------------------------------------------------
static bool disk_write(const uint8_t* buf, uint32_t sect)
{
  if (fseek(g_file, sect * SECT_SIZE, SEEK_SET))
    return false;

  return 1 == fwrite(buf, SECT_SIZE, 1, g_file);
}

//------------------------------------------------------------------------------
static int ls(const char* path)
{
  dir_t dir;
  dir_info_t info;

  printf("\nListing items in %s:\n\n", path);

  int err = fat_opendir(&dir, path);
  if (err)
    return err;

  for (;;)
  {
    err = fat_readdir(&dir, &info);
    if (err == FAT_ERR_EOF)
      return FAT_ERR_NONE;
    if (err)
      return err;
        
    printf("%5d   %s %02d   %02d:%02d   %.*s%c\n",
      info.size, months[info.modified.month], info.modified.day,
      info.modified.hour, info.modified.min, 
      info.namelen, info.name, info.attr & FAT_ATTR_DIR ? '/' : ' ');
    
    err = fat_nextdir(&dir);
    if (err)
      return err;
  }
}

//------------------------------------------------------------------------------
static int cat(const char* path)
{
  file_t file;
  int err = fat_fopen(&file, path, "r");
  if (err)
    return err;

  uint8_t buf[512];
  for (;;)
  {
    int cnt = fat_fread(&file, buf, 512);
    if (cnt < 0)
      return cnt;

    printf("%.*s\n", cnt, buf);
    if (cnt < 512)
      break;
  }

  return fat_fclose(&file);
}

//------------------------------------------------------------------------------
int main(int argc, const char** argv)
{
  int err, cnt;

  // Create disk interface
  disk_ops_t ops =
  {
    .read  = disk_read,
    .write = disk_write,
  };

  if (argc != 2)
  {
    printf("Usage: ./demo disk.img\n");
    return 0;
  }

  // User is responsible for handling disk initialization and status polling
  if (!disk_init(argv[1]))
    return 0;

  // File system will be mounted at /mnt
  err = fat_mount(&ops, 0, &g_fat, "mnt");
  if (err)
    return err;
  
  err = cat("/mnt/source/fat.c");
  if (err)
    goto unmount;

  err = ls("/mnt");
  if (err)
    goto unmount;

  err = fat_mkdir("/mnt/numbers");
  if (err)
    goto unmount;

  err = ls("/mnt/numbers");
  if (err)
    goto unmount;

  file_t file;
  err = fat_fopen(&file, "/mnt/numbers/numbers.txt", "w");
  if (err)
    goto unmount;

  for (int i = 0; i < 10; i++)
  {
    cnt = fat_fprintf(&file, "This is test number %d\n", i);
    if (cnt < 0)
      goto unmount;
  }

  err = fat_fclose(&file);
  if (err)
    goto unmount;

  err = ls("/mnt/numbers");
  if (err)
    goto unmount;

unmount:
  if (err < 0)
    printf("error: %s\n", fat_get_error(err));
  fat_umount(&g_fat);
  fclose(g_file);
  return err;
}

//------------------------------------------------------------------------------
// Use can use an RTC module to obtain the current date and time. If this 
// function is not implemented, the library will default to 01/01/1980 00:00:00.

void fat_get_timestamp(timestamp_t* dt)
{
  time_t t = time(NULL);
  struct tm tm = *localtime(&t);

  dt->day   = tm.tm_mday;
  dt->month = tm.tm_mon + 1;
  dt->year  = tm.tm_year + 1900;
  dt->hour  = tm.tm_hour;
  dt->min   = tm.tm_min;
  dt->sec   = tm.tm_sec;
}
