//------------------------------------------------------------------------------
// Copyright (c) 2020 Bj�rn Brodtkorb
//
// This software is provided without warranty of any kind. Permission is 
// granted, free of charge, to copy and modify this software, if this copyright
// notice is included in all copies of the software.
//------------------------------------------------------------------------------


#ifndef FAT32_H
#define FAT32_H

#include "fat_types.h"
#include "disk_interface.h"

typedef enum {
	FATRES_OK,
	FATRES_ERROR
} fstatus;

struct volume_s {
	// Volume info
	struct volume_s* next;
	
	// The first label is 11-bytes and located in the BPB
	// The sectondary label is introduced in the root directory
	char label[256];
	char letter;
	
	// FAT32 info
	u16 sector_size;
	u8 cluster_size;
	u32 total_size;
	
	// FAT32 offsets
	u32 fat_lba;
	u32 info_lba;
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

struct info_s {
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

struct partition_s {
	u32 lba;
	u32 size;
	u8 status;
	u8 type;
};

//------------------------------------------------------------------------------
// Microsoft FAT32 spesification
// Due to Microsoft releasing the licensing on the FAT LFN usage, this code will 
// use LFN instead of SFN. It will not have SFN support since it is not meant
// for smaller systems.
//------------------------------------------------------------------------------
#define MBR_BOOTSTRAP		0
#define MBR_BOOTSTRAP_SIZE	446
#define MBR_PARTITION		446
#define MBR_PARTITION_SIZE	16
#define MBR_BOOT_SIG		510
#define MBR_BOOT_SIG_VALUE	0xAA55

#define PAR_STATUS			0
#define PAR_TYPE			4
#define PAR_LBA				8
#define PAR_SIZE			12

// Old BPB and BS
#define BPB_JUMP_BOOT		0
#define BPB_OEM				3
#define BPB_SECTOR_SIZE		11
#define BPB_CLUSTER_SIZE	13
#define BPB_RSVD_CNT		14
#define BPB_NUM_FATS		16
#define BPB_ROOT_ENT_CNT	17
#define BPB_TOT_SECT_16		19
#define BPB_MEDIA			21
#define BPB_FAT_SIZE_16		22
#define BPB_SEC_PER_TRACK	24
#define BPB_NUM_HEADS		26
#define BPB_HIDD_SECT		28
#define BPB_TOT_SECT_32		32

// New BPB and BS applying for FAT12 and FAT16
#define BPB_16_DRV_NUM		36
#define BPB_16_RSVD1		37
#define BPB_16_BOOT_SIG		38
#define BPB_16_VOL_ID		39
#define BPB_16_VOL_LABEL	43
#define BPB_16_FSTYPE		54

// New BPB and BS applying for FAT32
#define BPB_32_FAT_SIZE		36
#define BPB_32_EXT_FLAGS	40
#define BPB_32_FSV			42
#define BPB_32_ROOT_CLUST	44
#define BPB_32_FSINFO		48
#define BPB_32_BOOT_SECT	50
#define BPB_32_RSVD			52
#define BPB_32_DRV_NUM		64
#define BPB_32_RSVD1		65
#define BPB_32_BOOT_SIG		66
#define BPB_32_VOL_ID		67
#define BPB_32_VOL_LABEL	71
#define BPB_32_FSTYPE		82


// File system thread
void fat32_thread(void* arg);

// Disk functions
u8 disk_mount(disk_e disk);
u8 disk_eject(disk_e disk);

// Volume functions
struct volume_s* volume_get_first(void);
struct volume_s* volume_get(char letter);
fstatus volume_set_label(struct volume_s* vol, const char* name, u8 length);
fstatus volume_get_label(struct volume_s* vol, char* name, u8 length);
fstatus volume_format(struct volume_s* vol);

// FAT32 functions
fstatus fat_dir_open(struct dir_s* dir, const char* path, u16 length);
fstatus fat_dir_close(struct dir_s* dir);
fstatus fat_dir_read(struct dir_s* dir, struct info_s* info);
fstatus fat_dir_make(const char* path);

fstatus fat_file_open(struct file_s* file, const char* path, u16 length);
fstatus fat_file_close(struct file_s* file);
fstatus fat_file_read(struct file_s* file, u8* buffer, u32 count, u32* status);
fstatus fat_file_write(struct file_s* file, const u8* buffer, u32 count);
fstatus fat_file_jump(struct file_s* file, u32 offset);


#endif
