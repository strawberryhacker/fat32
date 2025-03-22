// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Bjørn Brodtkorb. All rights reserved.

#include <string.h>
#include "fat.h"

//------------------------------------------------------------------------------
#define LIMIT(a, b) ((a) < (b) ? (a) : (b))

#define FSINFO_HEAD_SIG 0x41615252
#define FSINFO_STRUCT_SIG 0x61417272
#define FSINFO_TAIL_SIG 0xaa550000

#define LFN_FIRST 0x40
#define LFN_SEQ_MASK 0x1f

#define SFN_FREE 0xe5
#define SFN_LAST 0x00

//------------------------------------------------------------------------------
enum
{
  CLUST_FREE = 0x01,
  CLUST_USED = 0x02,
  CLUST_LAST = 0x04,
  CLUST_BAD  = 0x08,
};

enum
{
  FMT_ZERO        = 0x1,
  FMT_NO_SIGN     = 0x2,
  FMT_SIGN        = 0x4,
  FMT_LEFT        = 0x8,
  FMT_UPPER       = 0x10,
  FMT_HEX         = 0x20,
  FMT_BIN         = 0x40,
  FMT_CHAR        = 0x80,
  FMT_STR         = 0x100,
  FMT_SHORT       = 0x200,
  FMT_SHORT_SHORT = 0x400,
  FMT_LONG        = 0x800,
  FMT_LONG_LONG   = 0x1000,
  FMT_NUM         = 0x2000,
  FMT_UNSIGNED    = 0x4000,
  FMT_FLOAT       = 0x8000,
};

//------------------------------------------------------------------------------
typedef struct
{
  uint8_t status;
  uint8_t not_used0[3];
  uint8_t type;
  uint8_t not_used1[3];
  uint32_t lba;
  uint32_t size;
} mbr_part_t;

typedef struct __attribute__((packed))
{
  uint8_t jump[3];
  char name[8];
  uint16_t bytes_per_sect;
  uint8_t sect_per_clust;
  uint16_t res_sect_cnt;
  uint8_t fat_cnt;
  uint16_t root_ent_cnt;
  uint16_t sect_cnt_16;
  uint8_t media;
  uint16_t sect_per_fat_16;
  uint16_t sect_per_track;
  uint16_t head_cnt;
  uint32_t hidden_sect_cnt;
  uint32_t sect_cnt_32;
  uint32_t sect_per_fat_32;
  uint16_t flags;
  uint8_t minor;
  uint8_t major;
  uint32_t root_cluster;
  uint16_t info_sect;
  uint16_t copy_bpb_sector;
  uint8_t reserved0[12];
  uint8_t drive_num;
  uint8_t reserved1;
  uint8_t boot_sig;
  uint32_t volume_id;
  char volume_label[11];
  char fs_type[8];
  uint8_t reserved2[420];
  uint8_t sign[2];
} bpb_t;

typedef struct __attribute__((packed))
{
  uint32_t head_sig;
  uint8_t reserved0[480];
  uint32_t struct_sig;
  uint32_t free_cnt;
  uint32_t next_free;
  uint8_t reserved1[12];
  uint32_t tail_sig;
} fsinfo_t;

typedef struct __attribute__((packed))
{
  uint8_t name[11];
  uint8_t attr;
  uint8_t reserved;
  uint8_t tenth;
  uint16_t create_time;
  uint16_t create_date;
  uint16_t access_date;
  uint16_t clust_hi;
  uint16_t modify_time;
  uint16_t modify_date;
  uint16_t clust_lo;
  uint32_t size;
} dir_ent_t;

typedef struct __attribute__((packed))
{
  uint8_t seq;
  uint8_t name0[10];
  uint8_t attr;
  uint8_t type;
  uint8_t crc;
  uint8_t name1[12];
  uint16_t clust;
  uint8_t name2[4];
} lfn_ent_t;

//------------------------------------------------------------------------------
static fat_t* g_fat_list;
static uint8_t g_lfn_pos[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

//------------------------------------------------------------------------------
static bool fmt_printable(char c)
{
  return ' ' <= c && c <= '~';
}

//------------------------------------------------------------------------------
static void fmt_put(char** buf, char* end, char c)
{
  if (*buf != end)
    *(*buf)++ = c; 
}

//------------------------------------------------------------------------------
static void fmt_put_num(long long val, int flags, int width, char** buf, char* end)
{
  unsigned long long uval = (unsigned long long)val;
  int size = 0;
  char tmp[65]; // Binary digits in uint64_t plus sign
  
  char* hex = flags & FMT_UPPER ? "0123456789ABCDEF" : "0123456789abcdef";
  char pad = flags & FMT_ZERO ? '0' : ' ';
  int base = flags & FMT_HEX ? 16 : flags & FMT_BIN ? 2 : 10;
  char sign = 0;

  if (!(flags & FMT_UNSIGNED) && val < 0)
  {
    uval = (unsigned long long)-val;
    if (!(flags & FMT_NO_SIGN))
      sign = '-';
  }
  else if (flags & FMT_SIGN)
  {
    sign = '+';
  }

  do
  {
    tmp[size++] = hex[uval % base];
    uval /= base;
  } while (uval);

  if (sign)
    tmp[size++] = sign;

  int pad_size = width - size;
  if (!(flags & FMT_LEFT))
  {
    while (--pad_size >= 0)
      fmt_put(buf, end, pad);
  }
  
  while (size--)
    fmt_put(buf, end, tmp[size]);

  while (--pad_size >= 0)
    fmt_put(buf, end, pad);
}

//------------------------------------------------------------------------------
static void fmt_put_str(char* str, int flags, int width, char** buf, char* end)
{
  char pad = flags & FMT_ZERO ? '0' : ' ';
  if (!str)
    str = "NULL";

  int size = 0;
  while (str[size])
    size++;

  int pad_size = width - size;
  if (!(flags & FMT_LEFT))
  {
    while (--pad_size >= 0)
      fmt_put(buf, end, pad);
  }

  while (size--)
  {
    char c = *str++;
    // Some terminals require CR to reset line offset
    if (c == '\n')
      fmt_put(buf, end, '\r');
    fmt_put(buf, end, fmt_printable(c) ? c : '?');
  }

  while (--pad_size >= 0)
    fmt_put(buf, end, pad);
}

//------------------------------------------------------------------------------
int fmt_va(char* buf, int size, const char* fmt, va_list va)
{
  char* start = buf;
  char* end = buf + size;

  while (*fmt)
  {
    char c = *fmt++;
    if (c != '%')
    {
      if (c == '\n')
        fmt_put(&buf, end, '\r');
      fmt_put(&buf, end, c);
      continue;
    }

    int flags = 0;
    int width = 0;
    int prec = 0;

    for (c = *fmt++;; c = *fmt++)
    {
      if (c == '0')
        flags |= FMT_ZERO;
      else if (c == '-')
        flags |= FMT_LEFT;
      else if (c == ' ')
        flags |= FMT_NO_SIGN;
      else if (c == '+')
        flags |= FMT_SIGN;
      else if (c == '*')
      {
        width = va_arg(va, int);
        if (width < 0)
        {
          width = -width;
          flags |= FMT_LEFT;
        }
      }
      else
        break;
    }

    for (; '0' <= c && c <= '9'; c = *fmt++)
      width = 10 * width + (c - '0');
    
    if (c == '.')
    {
      c = *fmt++;
      for (; '0' <= c && c <= '9'; c = *fmt++)
        prec = 10 * prec + (c - '0');
    }

    for (;; c = *fmt++)
    {
      if (c == 'h')
        flags |= (flags & FMT_SHORT) ? FMT_SHORT_SHORT : FMT_SHORT;
      else if (c == 'l')
        flags |= (flags & FMT_LONG) ? FMT_LONG_LONG : FMT_LONG;
      else
        break;
    }

    switch (c)
    {
    case 'u':
      flags |= FMT_UNSIGNED;
    case 'd':
    case 'i': 
      flags |= FMT_NUM;
      break;
    case 'X':
      flags |= FMT_UPPER;
    case 'x':
      flags |= FMT_NUM | FMT_UNSIGNED | FMT_HEX;
      break;
    case 'f':
      flags |= FMT_FLOAT;
      break;
    case 'b':
    case 'B':
      flags |= FMT_UNSIGNED | FMT_NUM | FMT_BIN;
      break;
    case 'c':
      flags |= FMT_CHAR;
      break;
    case 's':
      flags |= FMT_STR;
      break;
    default:
      fmt_put(&buf, end, c);
      break;
    }

    if (flags & FMT_NUM)
    {
      long long val;

      if (flags & FMT_LONG_LONG)
        val = va_arg(va, long long);
      else if (flags & FMT_LONG)
        val = va_arg(va, long);
      else
        val = va_arg(va, int);
      
      if (flags & FMT_UNSIGNED)
      {
        if (flags & FMT_LONG_LONG)
          val = (unsigned long long)val;
        else if (flags & FMT_LONG)
          val = (unsigned long)val;
        else
          val = (unsigned int)val;
      }

      fmt_put_num(val, flags, width, &buf, end);
    }
    else if (flags & FMT_FLOAT)
    {
      int mult = 1;
      for (int i = 0; i < prec; i++)
        mult *= 10;

      double val = va_arg(va, double);
      int i = (int)val;
      int f = (int)((val - i) * mult);

      fmt_put_num(i, flags, width, &buf, end);
      if (prec)
      {
        fmt_put(&buf, end, '.');
        fmt_put_num(f, flags | FMT_ZERO, prec, &buf, end);
      }
    }
    else if (flags & FMT_CHAR)
    {
      char str[2];
      str[0] = (char)va_arg(va, int);
      str[1] = 0;
      fmt_put_str(str, flags, width, &buf, end);
    }
    else if (flags & FMT_STR)
    {
      char* str = va_arg(va, char*);
      fmt_put_str(str, flags, width, &buf, end);
    }
  }

  return buf - start;
}

//------------------------------------------------------------------------------
static char to_upper(char c)
{
  return (c >= 'a' && c <= 'z') ? c & ~0x20 : c;
}

//------------------------------------------------------------------------------
static int memcmp_upper(const char* a, const char* b, int len)
{
  for (int i = 0; i < len; i++)
  {
    if (to_upper(a[i]) != to_upper(b[i]))
      return 1;
  }

  return 0;
}

//------------------------------------------------------------------------------
static int subpath_len(const char* path)
{
  int i;
  for (i = 0; path[i] && path[i] != '/'; i++);
  return i;
}

//------------------------------------------------------------------------------
static uint8_t calc_crc(uint8_t* name)
{
  uint8_t sum = 0;
  for (int i = 0; i < 11; i++)
    sum = ((sum & 1) << 7) + (sum >> 1) + name[i];
  return sum;
}

//------------------------------------------------------------------------------
static void get_timestamp(uint16_t date, uint16_t time, timestamp_t* ts)
{
  ts->day = date & 0x1f;
  ts->month = (date >> 5) & 0xf;
  ts->year = ((date >> 9) & 0x3f) + 1980;
  ts->hour = (time >> 11) & 0x1f;
  ts->min = (time >> 5) & 0x3f;
  ts->sec = 2 * (time & 0x1f);
}

//------------------------------------------------------------------------------
static void put_timestamp(uint16_t* date, uint16_t* time)
{
  timestamp_t ts;
  fat_get_timestamp(&ts);
  
  *date = ((ts.year - 1980) & 0x3f) << 9 | (ts.month & 0xf) << 5 | (ts.day & 0x1f);
  *time = ((ts.sec / 2) & 0x1f) | (ts.min & 0x3f) << 5 | (ts.hour & 0x1f) << 11;
}

//------------------------------------------------------------------------------
static fat_t* find_fat_volume(const char* path, int len)
{
  for (fat_t* it = g_fat_list; it; it = it->next)
  {
    if (len == it->pathlen && 0 == memcmp(path, it->path, len))
      return it;
  }

  return NULL;
}

//------------------------------------------------------------------------------
static int sync_win(fat_t* fat)
{
  if (fat->win_dirty)
  {
    if (!fat->ops.write(fat->win, fat->win_sect))
      return FAT_ERR_DISK;

    fat->win_dirty = false;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int move_win(fat_t* fat, uint32_t sect)
{
  if (sect != fat->win_sect)
  {
    int err = sync_win(fat);
    if (err)
      return err;
    
    if (!fat->ops.read(fat->win, sect))
      return FAT_ERR_DISK;

    fat->win_sect = sect;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int sync_buf(fat_t* fat, file_t* file)
{
  if (file->buf_dirty)
  {
    if (!fat->ops.write(file->buf, file->buf_sect))
      return FAT_ERR_DISK;

    file->buf_dirty = false;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int move_buf(fat_t* fat, file_t* file, uint32_t sect)
{
  if (sect != file->buf_sect)
  {
    int err = sync_win(fat);
    if (err)
      return err;
    
    if (!fat->ops.read(file->buf, sect))
      return FAT_ERR_DISK;

    file->buf_sect = sect;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int sync_fat(fat_t* fat)
{
  int err = sync_win(fat);
  if (err)
    return err;

  if (fat->info_dirty)
  {
    err = move_win(fat, fat->info_sect);
    if (err)
      return err;
    
    fsinfo_t* info = (fsinfo_t*)fat->win;
    fat->win_dirty = true;

    memset(info, 0, sizeof(fsinfo_t));
    info->head_sig = FSINFO_HEAD_SIG;
    info->tail_sig = FSINFO_TAIL_SIG;
    info->struct_sig = FSINFO_STRUCT_SIG;
    info->next_free = fat->info_last;
    info->free_cnt = fat->info_cnt;

    err = sync_win(fat);
    if (err)
      return err;

    fat->info_dirty = false;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static uint32_t sect_to_clust(fat_t* fat, uint32_t sect)
{
  return ((sect - fat->data_sect) / fat->sect_per_clust) + 2;
}

//------------------------------------------------------------------------------
static uint32_t clust_to_sect(fat_t* fat, uint32_t clust)
{
  return ((clust - 2) * fat->sect_per_clust) + fat->data_sect;
}

//------------------------------------------------------------------------------
static int get_fat(fat_t* fat, uint32_t clust, uint32_t* res)
{
  uint32_t sect = fat->fat_sect + clust / 128;
  uint32_t off = (clust % 128) << 2;

  int err = move_win(fat, sect);
  if (err)
    return err;

  uint32_t* ent = (uint32_t*)(fat->win + off);
  *res = *ent & 0x0fffffff;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int put_fat(fat_t* fat, uint32_t clust, uint32_t val)
{
  uint32_t sect = fat->fat_sect + clust / 128;
  uint32_t off = 4 * (clust % 128);

  int err = move_win(fat, sect);
  if (err)
    return err;

  // Upper nibble must be preserved
  uint32_t* ent = (uint32_t*)(fat->win + off);
  *ent = (*ent & 0xf0000000) | (val & 0x0fffffff);
  fat->win_dirty = true;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int get_clust_status(fat_t* fat, uint32_t clust)
{
  if (clust == 0)
    return CLUST_FREE;

  if (clust == 0x0fffffff)
    return CLUST_USED | CLUST_LAST;

  if (clust >= 0x0ffffff6 || clust >= fat->clust_cnt)
    return CLUST_BAD;

  return CLUST_USED;
}

//------------------------------------------------------------------------------
static int clust_clear(fat_t* fat, uint32_t clust)
{
  uint32_t sect = clust_to_sect(fat, clust);

  for (int i = 0; i < fat->sect_per_clust; i++)
  {
    int err = sync_win(fat);
    if (err)
      return err;

    memset(fat->win, 0, SECT_SIZE);
    fat->win_sect = sect++;
    fat->win_dirty = true;
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int clust_chain_remove(fat_t* fat, uint32_t clust)
{
  for (;;)
  {
    uint32_t next;
    int err = get_fat(fat, clust, &next);
    if (err)
      return err;
    
    int status = get_clust_status(fat, next);

    if (status & (CLUST_BAD | CLUST_FREE))
      return FAT_ERR_BROKEN;
    
    err = put_fat(fat, clust, 0);
    if (err)
      return err;
    
    fat->info_cnt--;
    clust = next;

    if (status & CLUST_LAST)
      break;
  }

  fat->info_dirty = true;
  return sync_fat(fat);
}

//------------------------------------------------------------------------------
static int clust_chain_stretch(fat_t* fat, uint32_t clust, uint32_t* new)
{
  int err, status;
  uint32_t it, next;
  bool scan = true;

  if (clust)
  {
    // Stretch cluster chain. Check next cluster; if not free, scan from last used.
    it = clust + 1;
    if (it >= fat->clust_cnt)
      it = 2;
    
    err = get_fat(fat, it, &next);
    if (err)
      return err;
    
    status = get_clust_status(fat, next);

    if (status & CLUST_FREE)
      scan = false;
  }

  if (scan)
  {
    uint32_t mark = fat->info_last;
    it = fat->info_last;

    for (;;)
    {
      if (++it == mark)
        return FAT_ERR_FULL;

      if (it >= fat->clust_cnt)
        it = 2;

      err = get_fat(fat, it, &next);
      if (err)
        return err;
      
      status = get_clust_status(fat, next);

      if (status & CLUST_FREE)
        break;
    }
  }

  // Mark end of cluster chain
  err = put_fat(fat, it, 0x0fffffff);
  if (err)
    return err;
  
  if (clust)
  {
    // Stretching. Add link.
    err = put_fat(fat, clust, it);
    if (err)
      return err;
  }

  fat->info_last = it;
  fat->info_cnt--;
  fat->info_dirty = true;

  *new = it;
  return sync_fat(fat);
}

//------------------------------------------------------------------------------
static int clust_chain_create(fat_t* fat, uint32_t* new)
{
  return clust_chain_stretch(fat, 0, new);
}

//------------------------------------------------------------------------------
static void dir_rewind(fat_t* fat, dir_t* dir)
{
  dir->clust = dir->first_clust;
  dir->sect = clust_to_sect(fat, dir->first_clust);
  dir->idx = 0;
}

//------------------------------------------------------------------------------
static int dir_next(fat_t* fat, dir_t* dir)
{
  dir->idx += sizeof(dir_ent_t);

  if (dir->idx < SECT_SIZE)
    return FAT_ERR_NONE;

  dir->idx = 0;
  dir->sect++;

  if (0 == (dir->sect % fat->sect_per_clust))
  {
    uint32_t next;
    int err = get_fat(fat, dir->clust, &next);
    if (err)
      return err;
    
    int status = get_clust_status(fat, next);
    
    if (status & (CLUST_BAD | CLUST_FREE))
      return FAT_ERR_BROKEN;

    if (status & CLUST_LAST)
      return FAT_ERR_EOF;
    
    dir->clust = next;
    dir->sect = clust_to_sect(fat, next);
  }

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int dir_next_stretch(fat_t* fat, dir_t* dir)
{
  int err = dir_next(fat, dir);
  if (err != FAT_ERR_EOF)
    return err;

  uint32_t next;
  err = clust_chain_stretch(fat, dir->clust, &next);
  if (err)
    return err;

  err = clust_clear(fat, next);
  if (err)
    return err;
  
  dir->clust = next;
  dir->sect = clust_to_sect(fat, next);
  dir->idx = 0;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static bool dir_ent_is_last(dir_ent_t* ent)
{
  return ent->name[0] == SFN_LAST;
}

//------------------------------------------------------------------------------
static bool dir_ent_is_free(dir_ent_t* ent)
{
  return ent->name[0] == SFN_LAST || ent->name[0] == SFN_FREE;
}

//------------------------------------------------------------------------------
static bool dir_ent_is_lfn(dir_ent_t* ent)
{
  return ent->attr == FAT_ATTR_LFN;
}

//------------------------------------------------------------------------------
static char sfn_char(char c)
{
  c = to_upper(c);
  if (c >= 'A' && c <= 'Z')
    return c;
  
  const char specials[] = "!#$%&'()-@^_`{}~";
  for (int i = 0; i < sizeof(specials) - 1; i++)
  {
    if (c == specials[i])
      return c;
  }

  return '~';
}

//------------------------------------------------------------------------------
static void put_sfn_name(uint8_t* sfn_name, const char* name, int len)
{
  int i, j;

  for (i = 0; i < LIMIT(len, 8) && name[i] != '.'; i++)
    sfn_name[i] = sfn_char(name[i]);

  for (j = i; j < 8; j++)
    sfn_name[j] = ' ';

  while (i < len && name[i++] != '.');

  for (j = 0; j < 3 && i < len; j++, i++)
    sfn_name[8 + j] = sfn_char(name[i]);

  for (; j < 3; j++)
    sfn_name[8 + j] = ' ';
}

//------------------------------------------------------------------------------
static void load_sfn_name(fat_t* fat, dir_ent_t* ent)
{
  uint8_t* ptr = fat->name;

  for (int i = 0; i < 8 && ent->name[i] != ' '; i++)
    *ptr++ = ent->name[i];

  if (ent->name[8] != ' ')
    *ptr++ = '.';

  for (int i = 8; i < 11 && ent->name[i] != ' '; i++)
    *ptr++ = ent->name[i];

  fat->namelen = ptr - fat->name;
}

//------------------------------------------------------------------------------
static void put_lfn_name(lfn_ent_t* ent, const char* name, int len)
{
  uint8_t* ptr = (uint8_t*)ent;

  int i;
  for (i = 0; i < len; i++)
  {
    ptr[g_lfn_pos[i] + 0] = name[i];
    ptr[g_lfn_pos[i] + 1] = 0x00;
  }
  
  if (i < 13)
  {
    ptr[g_lfn_pos[i] + 0] = 0x00;
    ptr[g_lfn_pos[i] + 1] = 0x00;

    while (++i < 13)
    {
      ptr[g_lfn_pos[i] + 0] = 0xff;
      ptr[g_lfn_pos[i] + 1] = 0xff;
    }
  }
}

//------------------------------------------------------------------------------
// The LFN entry spans multiple entries, and possible clusters. This moves the 
// directory pointer, and loads one LFN name into the FAT name buffer.

static int load_lfn_name(fat_t* fat, dir_t* dir)
{
  int err = move_win(fat, dir->sect);
  if (err)
    return err;

  lfn_ent_t* ent = (lfn_ent_t*)(fat->win + dir->idx);

  fat->crc = ent->crc;
  fat->namelen = 0;

  if (0 == (ent->seq & LFN_FIRST))
    return FAT_ERR_BROKEN;

  int cnt = ent->seq & LFN_SEQ_MASK;
  if (cnt > 20)
    return FAT_ERR_BROKEN;

  while (cnt--)
  {
    if (ent->attr != FAT_ATTR_LFN || ent->crc != fat->crc)
      return FAT_ERR_BROKEN;

    int i;
    for (i = 0; i < 13; i++)
    {
      char c = fat->win[dir->idx + g_lfn_pos[i]];

      if (c == 0xff)
        return FAT_ERR_BROKEN; // Must proceed termination

      if (c == 0x00)
        break;

      fat->name[13 * cnt + i] = c;
    }

    fat->namelen += i;

    err = dir_next(fat, dir);
    if (err)
      return err;
  }

  return fat->namelen <= 255 ? FAT_ERR_NONE : FAT_ERR_BROKEN;
}

//------------------------------------------------------------------------------
static int dir_search(fat_t* fat, dir_t* dir, const char* name, int len)
{
  dir_rewind(fat, dir);

  for (;;)
  {
    int err = move_win(fat, dir->sect);
    if (err)
      return err;
    
    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);
    
    if (dir_ent_is_last(ent))
      return FAT_ERR_EOF;

    if (!dir_ent_is_free(ent))
    {
      if (dir_ent_is_lfn(ent))
      {
        err = load_lfn_name(fat, dir);
        if (err)
          return err;

        // The following entry must be SFN
        err = move_win(fat, dir->sect);
        if (err)
          return err;
        
        ent = (dir_ent_t*)(fat->win + dir->idx);

        if (dir_ent_is_free(ent) || dir_ent_is_lfn(ent))
          return FAT_ERR_BROKEN;

        if (fat->crc != calc_crc(ent->name))
          return FAT_ERR_BROKEN;
        
        if ((fat->namelen == len) && (0 == memcmp(fat->name, name, len)))
          return FAT_ERR_NONE;
      }
      else
      {
        load_sfn_name(fat, ent);
        if ((fat->namelen == len) && (0 == memcmp_upper((char*)fat->name, name, len)))
          return FAT_ERR_NONE;
      }
    }

    err = dir_next(fat, dir);
    if (err)
      return err;
  }
}

//------------------------------------------------------------------------------
static int follow_path(dir_t* dir, const char** path)
{
  int err, len;
  const char* str = *path;

  if (*str == '/')
  {
    str++;
    len = subpath_len(str);
    if (len == 0)
      return FAT_ERR_PATH;
    
    fat_t* fat = find_fat_volume(str, len);
    if (!fat)
      return FAT_ERR_PATH;
    
    str += len;

    dir->fat = fat;
    dir->first_clust = sect_to_clust(fat, fat->root_sect);
    dir->clust = dir->first_clust;
    dir->sect = fat->root_sect;
    dir->idx = 0;
  }

  fat_t* fat = dir->fat;
  if (!fat)
    return FAT_ERR_PARAM;

  dir_rewind(fat, dir);

  for (;;)
  {
    if (*str == '/')
    {
      str++;
      *path = str;
    }

    len = subpath_len(str);
    if (len == 0)
      return FAT_ERR_NONE;

    err = dir_search(fat, dir, str, len);
    if (err)
      return err;
      
    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);

    if (0 == (ent->attr & FAT_ATTR_DIR))
      return FAT_ERR_NONE;
    
    str += len;
    *path = str;

    uint32_t clust = ent->clust_hi << 16 | ent->clust_lo;
    if (clust == 0)
      clust = 2;

    dir->first_clust = clust;
    dir->clust = clust;
    dir->sect = clust_to_sect(fat, clust);
    dir->idx = 0;
  }
}

//------------------------------------------------------------------------------
static int dir_register(fat_t* fat, dir_t* dir, const char* name, int len, uint8_t attr, uint32_t clust)
{
  if (len <= 0 || len > 255)
    return FAT_ERR_PARAM;

  int err, idx;
  uint32_t sect;
  bool eod = false;
  int lfns = (len + 12) / 13;

  dir_rewind(fat, dir);

  // Try to find lfn_cnt + 1 consecutive free entries. Stretch cluster chain
  // if necessary. Store the first free entry in the sequence.
  for (int cnt = 0; cnt < lfns + 1;)
  {
    err = move_win(fat, dir->sect);
    if (err)
      return err;

    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);

    if (eod || dir_ent_is_free(ent))
    {
      if (cnt++ == 0)
      {
        sect = dir->sect;
        idx = dir->idx;
      }
    }
    else
      cnt = 0;

    if (dir_ent_is_last(ent))
      eod = true;

    err = dir_next_stretch(fat, dir);
    if (err)
      return err;
  }

  if (eod)
  {
    // We are currently at the entry after the future SFN.
    // Create new EOD marker
    err = move_win(fat, dir->sect);
    if (err)
      return err;
    
    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);

    if (!dir_ent_is_free(ent))
      return FAT_ERR_BROKEN;

    ent->name[0] = 0x00;
    fat->win_dirty = true;
  }

  // Rewind to the first free entry
  dir->clust = sect_to_clust(fat, sect);
  dir->sect = sect;
  dir->idx = idx;

  uint8_t sfn_name[11];
  put_sfn_name(sfn_name, name, len);

  uint8_t crc = calc_crc(sfn_name);
  uint8_t mask = LFN_FIRST;

  for (int i = lfns; i > 0; i--, mask = 0)
  {
    err = move_win(fat, dir->sect);
    if (err)
      return err;
    
    lfn_ent_t* lfn = (lfn_ent_t*)(fat->win + dir->idx);
    fat->win_dirty = true;

    int pos = 13 * (i - 1);
    put_lfn_name(lfn, name + pos, LIMIT(len - pos, 13));
    lfn->attr = FAT_ATTR_LFN;
    lfn->seq = mask | i;
    lfn->crc = crc;
    lfn->type = 0;
    lfn->clust = 0;
    
    err = dir_next(fat, dir);
    if (err && err != FAT_ERR_EOF) // We have not updated yet
      return err;
  }

  uint16_t time, date;
  put_timestamp(&date, &time);

  err = move_win(fat, dir->sect);
  if (err)
    return err;

  dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);
  fat->win_dirty = true;

  memcpy(ent->name, sfn_name, sizeof(ent->name));
  ent->clust_hi = clust >> 16;
  ent->clust_lo = clust & 0xffff;
  ent->attr = attr;
  ent->reserved = 0;
  ent->tenth = 0;
  ent->create_time = time;
  ent->modify_time = time;
  ent->create_date = date;
  ent->modify_date = date;
  ent->access_date = date;
  ent->size = 0;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
static int get_mbr_partition(fat_t* fat, mbr_part_t* part, int part_num)
{
  int err = move_win(fat, 0);
  if (err)
    return err;

  if (fat->win[510] != 0x55 || fat->win[511] != 0xaa)
    return FAT_ERR_NOFAT;

  *part = ((mbr_part_t*)(fat->win + 446))[part_num];
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_mount(disk_ops_t* ops, int part_num, fat_t* fat, const char* path)
{
  int err;
  fat->ops = *ops;
  fat->win_sect = 0xffffffff; // First call to move_win will succeed

  mbr_part_t part;
  err = get_mbr_partition(fat, &part, part_num);
  if (err)
    return err;

  if (part.type != 0xc)
    return FAT_ERR_NOFAT;

  err = move_win(fat, part.lba);
  if (err)
    return err;

  bpb_t* bpb = (bpb_t*)fat->win;
  
  if (bpb->jump[0] != 0xeb && bpb->jump[0] != 0xe9)
    return FAT_ERR_NOFAT;

  if (bpb->fat_cnt != 2)
    return FAT_ERR_NOFAT;
  
  if (bpb->root_ent_cnt || bpb->sect_cnt_16 || bpb->sect_per_fat_16)
    return FAT_ERR_NOFAT;
  
  if (bpb->info_sect != 1)
    return FAT_ERR_NOFAT;

  if (memcmp(bpb->fs_type, "FAT32   ", 8))
    return FAT_ERR_NOFAT;
    
  if (bpb->bytes_per_sect != SECT_SIZE)
    return FAT_ERR_NOFAT;
  
  uint32_t data_cnt = bpb->sect_cnt_32 - (bpb->res_sect_cnt + bpb->fat_cnt * bpb->sect_per_fat_32);
  uint32_t clust_cnt = data_cnt / bpb->sect_per_clust;

  if (clust_cnt < 65525)
    return FAT_ERR_NOFAT;

  fat->sect_per_clust = bpb->sect_per_clust;
  fat->clust_cnt = bpb->sect_per_fat_32 * 128;
  fat->info_sect = part.lba + bpb->info_sect;
  fat->fat_sect  = part.lba + bpb->res_sect_cnt;
  fat->data_sect = fat->fat_sect + bpb->fat_cnt * bpb->sect_per_fat_32;
  fat->root_sect = clust_to_sect(fat, bpb->root_cluster);

  err = move_win(fat, fat->info_sect);
  if (err)
    return err;

  fsinfo_t* info = (fsinfo_t*)fat->win;

  // Require a valid fat info
  if (info->tail_sig != FSINFO_TAIL_SIG || 
      info->head_sig != FSINFO_HEAD_SIG ||
      info->struct_sig != FSINFO_STRUCT_SIG ||
      info->next_free == 0xffffffff || 
      info->free_cnt == 0xffffffff)
    return FAT_ERR_NOFAT;
  
  fat->info_last = info->next_free;
  fat->info_cnt  = info->free_cnt;
  
  int path_len = strlen(path);
  if (sizeof(fat->path) < path_len)
    return FAT_ERR_PARAM;
  memcpy(fat->path, path, path_len);
  fat->pathlen = path_len;

  fat->next = g_fat_list;
  g_fat_list = fat;

  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_umount(fat_t* fat)
{
  fat_t** it = &g_fat_list;
  while (*it && *it != fat)
    it = &(*it)->next;

  if (*it == NULL)
    return FAT_ERR_PARAM;

  *it = fat->next;

  return sync_fat(fat);
}

//------------------------------------------------------------------------------
int fat_fopen(file_t* file, const char* path, const char* mode)
{
  enum
  {
    FLAG_W = 0x01,
    FLAG_R = 0x02,
    FLAG_A = 0x04,
    FLAG_X = 0x08,
    FLAG_P = 0x10,
  };

  uint8_t flags = 0;
  while (*mode)
  {
    switch (*mode++)
    {
      case 'r':
        flags |= FLAG_R;
        break;
      case 'w':
        flags |= FLAG_W;
        break;
      case 'a':
        flags |= FLAG_A;
        break;
      case '+':
        flags |= FLAG_P;
        break;
      case 'x':
        flags |= FLAG_X;
        break;
    }
  }

  memset(file, 0, sizeof(file_t));

  if (flags & (FLAG_R | FLAG_P))
    file->read = true;

  if (flags & (FLAG_W | FLAG_A | FLAG_P))
    file->write = true;
  
  bool append = (flags & FLAG_A) != 0;
  bool create = (flags & (FLAG_W | FLAG_X)) == FLAG_W;
  bool trunc = (flags & (FLAG_W | FLAG_A)) == FLAG_W;

  int err = follow_path(&file->dir, &path);
  if (err && err != FAT_ERR_EOF)
    return err;
  
  dir_t* dir = &file->dir;
  fat_t* fat = dir->fat;
  uint32_t file_clust;
  
  int len = subpath_len(path);
  if (len == 0 || path[len])
    return FAT_ERR_PATH;

  err = dir_search(fat, dir, path, len);

  if (err == FAT_ERR_EOF)
  {
    if (!create)
      return FAT_ERR_DENIED;
    
    // Create a new file
    uint32_t new;
    err = clust_chain_create(fat, &new);
    if (err)
      return err;
    
    err = dir_register(fat, dir, path, len, FAT_ATTR_NONE, new);
    if (err)
      return err;
    
    file_clust = new;
  }
  else
  {
    if (err)
      return err;

    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);

    if (trunc)
    {
      file->size = 0;
      file->modified = true;
    }
    else
      file->size = ent->size;

    file_clust = ent->clust_hi << 16 | ent->clust_lo;
  }

  file->first_clust = file_clust;
  file->clust = file_clust;
  file->sect = clust_to_sect(fat, file_clust);
  file->off = 0;

  return append ? fat_fseek(file, 0, FAT_SEEK_END) : FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_fclose(file_t* file)
{
  if (file->dir.fat == NULL)
    return FAT_ERR_PARAM;

  int err = fat_fsync(file);
  if (err)
    return err;
  
  memset(file, 0, sizeof(file_t));
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_fread(file_t* file, void* buf, int len)
{
  int err;
  int bytes = 0;
  uint8_t* dst = buf;
  dir_t* dir = &file->dir;
  fat_t* fat = dir->fat;

  if (fat == NULL)
    return FAT_ERR_PARAM;

  if (!file->read)
    return FAT_ERR_DENIED;
  
  file->accessed = true;
  
  while (len && file->size > file->off)
  {
    err = move_buf(fat, file, file->sect);
    if (err)
      break;
    
    int idx = file->off % SECT_SIZE;
    int cnt = LIMIT(len, LIMIT(SECT_SIZE - idx, file->size - file->off));
    memcpy(dst, file->buf + idx, cnt);

    bytes += cnt;
    dst += cnt;
    len -= cnt;

    err = fat_fseek(file, cnt, FAT_SEEK_CURR);
    if (err)
      break;
  }

  return err < 0 ? err : bytes;
}

//------------------------------------------------------------------------------
int fat_fwrite(file_t* file, const void* buf, int len)
{
  int err;
  int bytes = 0;
  const uint8_t* src = buf;
  dir_t* dir = &file->dir;
  fat_t* fat = dir->fat;

  if (fat == NULL)
    return FAT_ERR_PARAM;

  if (!file->write)
    return FAT_ERR_DENIED;
  
  file->modified = true;
  file->accessed = true;

  while (len)
  {
    err = move_buf(fat, file, file->sect);
    if (err)
      break;
    
    int idx = file->off % SECT_SIZE;
    int cnt = LIMIT(len, SECT_SIZE - idx);
    memcpy(file->buf + idx, src, cnt);
    file->buf_dirty = true;

    bytes += cnt;
    src += cnt;
    len -= cnt;

    err = fat_fseek(file, cnt, FAT_SEEK_CURR);
    if (err)
      break;
  }

  if (file->off > file->size)
    file->size = file->off;

  return err < 0 ? err : bytes;
}

//------------------------------------------------------------------------------
int fat_fprintf(file_t* file, const char* fmt, ...)
{
  static char buf[4096];

  va_list va;
  va_start(va, fmt);
  int len = fmt_va(buf, sizeof(buf), fmt, va);
  va_end(va);

  return fat_fwrite(file, buf, len);
}

//------------------------------------------------------------------------------
int fat_fseek(file_t* file, int offset, int seek)
{
  int err;
  int64_t s_off = 0;
  dir_t* dir = &file->dir;
  fat_t* fat = dir->fat;

  if (fat == NULL)
    return FAT_ERR_PARAM;

  switch (seek)
  {
    case FAT_SEEK_SET:
      s_off = 0;
      break;
    case FAT_SEEK_CURR:
      s_off = file->off;
      break;
    case FAT_SEEK_END:
      s_off = file->size;
      break;
  }

  s_off += offset;
  if (s_off < 0 || s_off > 0xffffffff)
    return FAT_ERR_EOF;

  uint32_t off = (uint32_t)s_off;
  uint32_t dst_clust = off / (SECT_SIZE * fat->sect_per_clust);
  uint32_t src_clust = file->off / (SECT_SIZE * fat->sect_per_clust);

  if (dst_clust < src_clust)
  {
    // Backtracking not possible. Start scan from the beginning.
    file->clust = file->first_clust;
    file->sect = clust_to_sect(fat, file->first_clust);
    file->off = 0;
    src_clust = 0;
  }

  for (int i = 0; i < dst_clust - src_clust; i++)
  {
    uint32_t next;
    err = get_fat(fat, file->clust, &next);
    if (err)
      return err;

    int status = get_clust_status(fat, next);

    if (status & (CLUST_BAD | CLUST_FREE))
      return FAT_ERR_BROKEN;

    if (status & CLUST_LAST)
    {
      err = clust_chain_stretch(fat, file->clust, &next);
      if (err)
        return err;
    }
    
    file->clust = next;
  }

  file->sect = clust_to_sect(fat, file->clust) + ((off / SECT_SIZE) % fat->sect_per_clust);
  file->off = off;
  
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_ftell(file_t* file)
{
  return file->off;
}

//------------------------------------------------------------------------------
int fat_fsize(file_t* file)
{
  return file->size;
}

//------------------------------------------------------------------------------
int fat_fsync(file_t* file)
{
  int err;
  fat_t* fat = file->dir.fat;

  if (fat == NULL)
    return FAT_ERR_PARAM;

  if (file->accessed || file->modified)
  {
    err = move_win(fat, file->dir.sect);
    if (err)
      return err;
    
    dir_ent_t* ent = (dir_ent_t*)(fat->win + file->dir.idx);
    fat->win_dirty = true;

    uint16_t date, time;
    put_timestamp(&date, &time);
    
    if (file->accessed)
      ent->access_date = date;

    if (file->modified)
    {
      ent->size = file->size;
      ent->modify_date = date;
      ent->modify_time = time;
    }
  }

  err = sync_buf(fat, file);
  if (err)
    return err;
  
  err = sync_fat(fat);
  if (err)
    return err;

  file->accessed = false;
  file->modified = false;
  
  return FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_unlink(const char* path)
{
  dir_t dir;
  int err = follow_path(&dir, &path);
  if (err)
    return err;
  
  int len = subpath_len(path);
  if (len == 0 || path[len])
    return FAT_ERR_PATH;

  fat_t* fat = dir.fat;

  err = dir_search(fat, &dir, path, len);
  if (err)
    return err;
  
  dir_ent_t* ent = (dir_ent_t*)(fat->win + dir.idx);

  err = clust_chain_remove(fat, ent->clust_hi << 16 | ent->clust_lo);
  if (err)
    return err;

  ent->name[0] = SFN_FREE;
  fat->win_dirty = true;

  return sync_fat(fat);
}

//------------------------------------------------------------------------------
int fat_mkdir(const char* path)
{
  dir_t dir;
  int err = follow_path(&dir, &path);
  if (err != FAT_ERR_EOF)
    return err;
  
  fat_t* fat = dir.fat;
  
  int len = subpath_len(path);
  if (len == 0 || path[len])
    return FAT_ERR_PATH;

  // Create a new directory
  uint32_t new;
  err = clust_chain_create(fat, &new);
  if (err)
    return err;

  err = clust_clear(fat, new);
  if (err)
    return err;

  uint16_t date, time;
  put_timestamp(&date, &time);

  err = move_win(fat, clust_to_sect(fat, new));
  if (err)
    return err;
  
  dir_ent_t* ent = (dir_ent_t*)fat->win;
  fat->win_dirty = true;

  memset(ent->name, ' ', sizeof(ent->name));
  ent->name[0] = '.';
  ent->attr = FAT_ATTR_DIR;
  ent->clust_hi = new >> 16;
  ent->clust_lo = new & 0xffff;
  ent->create_date = date;
  ent->create_time = time;
  ent->modify_date = date;
  ent->modify_time = time;
  ent->access_date = date;

  memcpy(ent + 1, ent, sizeof(dir_ent_t));
  ent[1].name[1] = '.';
  ent[1].clust_hi = dir.first_clust >> 16;
  ent[1].clust_lo = dir.first_clust & 0xffff;

  err = dir_register(fat, &dir, path, len, FAT_ATTR_DIR, new);
  if (err)
    return err;
  
  return sync_fat(fat);
}

//------------------------------------------------------------------------------
int fat_opendir(dir_t* dir, const char* path)
{
  int err = follow_path(dir, &path);
  if (err)
    return err;

  return subpath_len(path) ? FAT_ERR_PATH : FAT_ERR_NONE;
}

//------------------------------------------------------------------------------
int fat_readdir(dir_t* dir, dir_info_t* info)
{
  fat_t* fat = dir->fat;
  if (!fat)
    return FAT_ERR_PARAM;
  
  for (;;)
  {
    int err = move_win(fat, dir->sect);
    if (err)
      return err;
    
    dir_ent_t* ent = (dir_ent_t*)(fat->win + dir->idx);

    if (dir_ent_is_last(ent))
      return FAT_ERR_EOF;

    if (!dir_ent_is_free(ent))
    {
      if (dir_ent_is_lfn(ent))
      {
        err = load_lfn_name(fat, dir);
        if (err)
          return err;

        err = move_win(fat, dir->sect);
        if (err)
          return err;
        
        // Following entry must be SFN
        ent = (dir_ent_t*)(fat->win + dir->idx);
        
        if (dir_ent_is_free(ent))
          return FAT_ERR_BROKEN;

        if (fat->crc != calc_crc(ent->name))
          return FAT_ERR_BROKEN;
      }
      else
        load_sfn_name(fat, ent);

      memcpy(info->name, fat->name, fat->namelen);
      info->namelen = fat->namelen;

      get_timestamp(ent->create_date, ent->create_time, &info->created);
      get_timestamp(ent->modify_date, ent->modify_time, &info->modified);

      info->size = ent->size;
      info->attr = ent->attr;

      return FAT_ERR_NONE;
    }

    err = dir_next(fat, dir);
    if (err)
      return err;
  }
}

//------------------------------------------------------------------------------
int fat_nextdir(dir_t* dir)
{
  return dir_next(dir->fat, dir);
}

//------------------------------------------------------------------------------
__attribute__((weak)) void fat_get_timestamp(timestamp_t* dt)
{
  dt->day   = 1;
  dt->month = 1;
  dt->year  = 1980;
  dt->hour  = 0;
  dt->min   = 0;
  dt->sec   = 0;
}

//------------------------------------------------------------------------------
const char* fat_get_error(int err)
{
  static const char* codes[] =
  {
    "FAT_ERR_NONE",
    "FAT_ERR_NOFAT",
    "FAT_ERR_BROKEN",
    "FAT_ERR_DISK",
    "FAT_ERR_PARAM",
    "FAT_ERR_PATH",
    "FAT_ERR_EOF",
    "FAT_ERR_DENIED",
    "FAT_ERR_FULL",
  };

  err = -err;
  return err >= 0 && err < sizeof(codes) / sizeof(char*) ? codes[err] : "FAT_ERR_UNKNOWN";
}
