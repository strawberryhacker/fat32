// DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
//                    Version 2, December 2004
//  
// Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>
// 
// Everyone is permitted to copy and distribute verbatim or modified
// copies of this license document, and changing it is allowed as long
// as the name is changed.
//  
//            DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
//   TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION
// 
//  0. You just DO WHAT THE FUCK YOU WANT TO.

#ifndef FAT32_H
#define FAT32_H

#include "fat_types.h"
#include "disk_interface.h"

/// Most of the FAT32 file system functions returns one of these status codes
typedef enum {
	FSTATUS_OK,
	FSTATUS_ERROR,
	FSTATUS_NO_VOLUME,
	FSTATUS_PATH_ERR,
	FSTATUS_EOF
} fstatus;

struct volume_s {
	struct volume_s* next;
	
	// The first label is 11-bytes and located in the BPB, while the sectondary
	// label is introduced in the root directory. The BPB label contains 13 
	// characters while the root label can contain 13 characters.
	char label[13];
	char letter;
	
	// FAT32 info
	u16 sector_size;
	u8 cluster_size;
	u32 total_size;
	u32 fat_lba;
	u32 fsinfo_lba;
	u32 data_lba;
	u32 root_lba;
	
	// All file system operations require a 512-byte buffer for storing the
	// current sector.
	u8 buffer[512];
	u32 buffer_lba;
	disk_e disk;
	u32 buffer_dirty;
	
	char lfn[256];
	u8 lfn_size;
	
};

struct dir_s {
	u32 sector;
	u32 cluster;
	u32 rw_offset;
	
	u32 start_sect;
	u32 size;
	struct volume_s* vol;
};

struct file_s {
	u32 sector;
	u32 cluster;
	u32 rw_offset;
	u32 size;
	u32 start_sect;
	u32 glob_offset;
	struct volume_s* vol;
};

/// This structure will contain all information needed for a file or a folder. 
/// It is mainly used to read directory entries from a path
struct info_s {
	
	// By default this code supports long file name entries (LFN) up to 
	// 256 characters. The same buffer will be used for LFN and SFN entries.
	char name[256];
	u8 name_length;
	
	// The attribute field apply to a file or a folder
	// 
	// Bit 0 - Read-only
	// Bit 1 - Hidden
	// Bit 2 - System (do not mess with these directories)
	// Bit 3 - Volume label
	// Bit 4 - Subdirectory
	// Bit 5 - Archeive
	// Bit 6 - Device
	u8	attribute;
	
	// Time and date properties
	u8	c_time_tenth;
	u16 c_time;
	u16 c_date;
	u16 a_date;
	u16 w_time;
	u16 w_date;
	
	// Contains the total size of a file or a folder
	// A folder has 
	u32 size;
}

/// The classical generic MBR located at sector zero at a MSD contains four 
/// partition fields. This structure describe one partition. 
struct partition_s {
	u32 lba;
	u32 size;
	u8 status;
	u8 type;
};

/// Format structure
struct fat_fmt_s {
	u32 allocation_size;
	u32 allignment;
	u32 quick_format;
};

//------------------------------------------------------------------------------
// Microsoft FAT32 spesification
// Due to Microsoft releasing the licensing on the FAT LFN usage, this code will 
// use LFN instead of SFN. It will not have SFN support since it is not meant
// for smaller systems.
//------------------------------------------------------------------------------

/// MBR and boot sector
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

/// Old BPB and BS
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

/// New BPB and BS applying for FAT12 and FAT16
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

/// Directory entry defines
#define SFN_NAME			0
#define SFN_ATTR			11
#define SFN_NTR				12
#define SFN_CTIME_TH		13
#define SFN_CTIME			14
#define SFN_CDATE			16
#define SFN_ADATE			18
#define SFN_CLUSTH			20
#define SFN_WTIME			22
#define SFN_WDATE			24
#define SFN_CLUSTL			26
#define SFN_FILE_SIZE		28

#define LFN_SEQ				0
#define LFN_SEQ_MSK			0x1F
#define LFN_NAME_1			1
#define LFN_ATTR			11
#define LFN_TYPE			12
#define LFN_CRC				13
#define LFN_NAME_2			14
#define LFN_NAME_3			28

#define ATTR_RO				0x01
#define ATTR_HIDD			0x02
#define ATTR_SYS			0x04
#define ATTR_VOL_LABEL		0x08
#define ATTR_DIR			0x10
#define ATTR_ARCH			0x20
#define ATTR_LFN			0x0F

/// FSinfo structure
#define INFO_CLUST_CNT		488
#define INFO_NEXT_FREE		492

/// File system thread
void fat32_thread(void* arg);

/// Disk functions
u8 disk_mount(disk_e disk);
u8 disk_eject(disk_e disk);

/// Volume functions
struct volume_s* volume_get_first(void);
struct volume_s* volume_get(char letter);
fstatus volume_set_label(struct volume_s* vol, const char* name, u8 length);
fstatus volume_get_label(struct volume_s* vol, char* name);
fstatus volume_format(struct volume_s* vol, struct fat_fmt_s* fmt);

/// Directory actions
fstatus fat_dir_open(struct dir_s* dir, const char* path, u16 length);
fstatus fat_dir_close(struct dir_s* dir);
fstatus fat_dir_read(struct dir_s* dir, struct info_s* info);
fstatus fat_dir_make(const char* path);

/// File actions
fstatus fat_file_open(struct file_s* file, const char* path, u16 length);
fstatus fat_file_close(struct file_s* file);
fstatus fat_file_read(struct file_s* file, u8* buffer, u32 count, u32* status);
fstatus fat_file_write(struct file_s* file, const u8* buffer, u32 count);
fstatus fat_file_jump(struct file_s* file, u32 offset);
fstatus fat_file_flush(struct file_s* file);

/// Directory and file actions
fstatus fat_dir_rename(struct dir_s* dir, const char* name, u8 length);
fstatus fat_dir_delete(struct dir_s* dir);
fstatus fat_dir_chmod(struct dir_s* dir, const char* mod);

#endif