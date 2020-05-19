//----------------------------------------------------------------------------//

// Copyright (c) 2020 Bjørn Brodtkorb
//
// This software is provided without warranty of any kind.
// Permission is granted, free of charge, to copy and modify this
// software, if this copyright notice is included in all copies of
// the software.

#ifndef FAT32_H
#define FAT32_H

#include "disk_interface.h"

typedef enum {
	FATRES_OK,
	FATRES_ERROR
} fatres;

struct volume_s {
	// Volume info
	struct volume_s* next;
	
	// The first label is 11-bytes and located in the BPB
	// The sectondary label is introduced in the root directory
	char label[256];
	char letter;
	
	// FAT32 info
	u32 sector_size;
	u32 cluster_size;
	u32 total_size;
	
	// FAT32 offsets
	u32 fat_lba;
	u32 root_lba;
	
	// Working buffers
	u8 buffer[512];
	u32 buffer_lba;
	u8 buffer_dirty;
	
	char lfn[256];
	u8 lfn_size;
};

struct dir_s {
	u32 sector;
	u32 cluster;
	u32 rw_offset;
};

struct file_s {
	u32 sector;
	u32 cluster;
	u32 rw_offset;
};

struct dirinfo_s {
	// Long file name support
	char name[256];
	
	u8	attribute;
	u8	c_time_tenth;
	u16 c_time;
	u16 cdate;
	u16 adate;
	u16 wtime;
	u16 wdate;
	
	u32 size;
};

// Disk functions
u8 disk_mount(disk_e disk);
u8 disk_eject(disk_e disk);

// Volume functions
struct volume_s* volume_get_first(void);
struct volume_s* volume_get(char letter);
fatres volume_set_label(struct volume_s* vol, const char* name, u8 length);
fatres volume_get_label(struct volume_s* vol, char* name, u8 length);
fatres volume_format(struct volume_s* vol);

// FAT32 functions
fatres fat_dir_open(struct dir_s* dir, const char* path, u16 length);
fatres fat_dir_close(struct dir_s* dir);
fatres fat_dir_read(struct dir_s* dir, struct dirinfo_s* info);
fatres fat_dir_make(const char* path);

fatres fat_file_open(struct file_s* file, const char* path, u16 length);
fatres fat_file_close(struct file_s* file);
fatres fat_file_read(struct file_s* file, u8* buffer, u32 count, u32* ret_count);
fatres fat_file_write(struct file_s* file, const u8* buffer, u32 count);
fatres fat_file_jump(struct file_s* file, u32 offset);


#endif