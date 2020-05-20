//------------------------------------------------------------------------------
// Copyright (c) 2020 Bjørn Brodtkorb
//
// This software is provided without warranty of any kind. Permission is
// granted, free of charge, to copy and modify this software, if this copyright
// notice is included in all copies of the software.
//------------------------------------------------------------------------------

#include "fat32.h"
#include "disk_interface.h"
#include "board_serial.h"
#include "board_sd_card.h"
#include "dynamic_memory.h"

#include <stddef.h>


// Private variables
static struct volume_s* first_volume;
static u32 volume_bitmask;

static u8 mount_buffer[512];


// Private functions
static void fat_memcpy(const void* src, void* dest, u32 count);
static u8 fat_memcmp(const void* src_1, const void* src_2, u32 count);
static void fat_store32(void* dest, u32 value);
static void fat_store16(void* dest, u16 value);
static u32 fat_load32(const void* src);
static u16 fat_load16(const void* src);
static u8 fat_volume_add(struct volume_s* vol);
static u8 fat_volume_remove(char letter);
static u8 fat_search(const u8* bpb);
static void fat_print_sector(const u8* sector);


static void fat_memcpy(const void* src, void* dest, u32 count) {
	const u8* src_ptr = (const u8 *)src;
	u8* dest_ptr = (u8 *)dest;
	do {
		*dest_ptr++ = *src_ptr++;
	} while (--count);
}

static u8 fat_memcmp(const void* src_1, const void* src_2, u32 count) {
	const u8* src_1_ptr = (const u8 *)src_1;
	const u8* src_2_ptr = (const u8 *)src_2;
	
	do {
		if (*src_1_ptr != *src_2_ptr) {
			return 0;
		}
		src_1_ptr++;
		src_2_ptr++;
	} while (--count);
	
	return 1;
}

static void fat_store32(void* dest, u32 value) {
	u8* dest_ptr = (u8 *)dest;
	
	// Little-endian store 32-bit
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
}

static void fat_store16(void* dest, u16 value) {
	u8* dest_ptr = (u8 *)dest;
	
	// Little-endian store 16-bit
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
}

static u32 fat_load32(const void* src) {
	u32 value = 0;
	const u8* src_ptr = (const u8 *)src;
	
	// Little-endian load 32-bit
	value |= *src_ptr++;
	value |= (*src_ptr++ << 8);
	value |= (*src_ptr++ << 16);
	value |= (*src_ptr++ << 24);
	
	return value;
}

static u16 fat_load16(const void* src) {
	u16 value = 0;
	const u8* src_ptr = (const u8 *)src;
	
	// Little-endian load 32-bit
	value |= *src_ptr++;
	value |= (*src_ptr++ << 8);
	
	return value;
}

static void fat_print_sector(const u8* sector) {
	for (u32 i = 0; i < 512;) {
		print("%c", sector[i]);
		
		if ((i++ % 32) == 0) {
			print("\n");
		}
	}
	print("\n");
}

static u8 fat_volume_add(struct volume_s* vol) {
	if (first_volume == NULL) {
		first_volume = vol; 
	} else {
		struct volume_s* vol_it = first_volume;
		
		while (vol_it->next != NULL) {
			vol_it = vol_it->next;
		}
		
		vol_it->next = vol;
	}
	vol->next = NULL;
	
	// Assign a letter for the volume
	for (u8 i = 0; i < 32; i++) {
		if ((volume_bitmask & (1 << i)) == 0) {
			volume_bitmask |= (1 << i);
			vol->letter = 'C' + i;
			break;
		}
	}
	return 1;
}

static u8 fat_volume_remove(char letter) {
	return 0;
}

// This functions will check for a valid file system and return 1 is found, 0 if
// not. Note that only FAT32 is supported.
static u8 fat_search(const u8* bpb) {
	// Check the BPB boot signature 
	if (fat_load16(bpb + 510) != 0xAA55) {
		return 0;
	}
	
	// Check for the "FAT" string which is an valid file system indicator
	if (!fat_memcmp(bpb + BPB_32_FSTYPE, "FAT", 3)) {
		if (!fat_memcmp(bpb + BPB_16_FSTYPE, "FAT", 3)) {
			return 0;
		}
	}
	
	// We know that we have a valid FAT12 / FAT16 or FAT32 file system
	u32 root_sectors = ((fat_load16(bpb + BPB_ROOT_ENT_CNT) * 32) + 
		(fat_load16(bpb + BPB_SECTOR_SIZE) - 1)) / 
		(fat_load16(bpb + BPB_SECTOR_SIZE) - 1);
	
	u32 fat_size = (fat_load16(bpb + BPB_FAT_SIZE_16)) ? 
		(u32)fat_load16(bpb + BPB_FAT_SIZE_16) : 
		fat_load32(bpb + BPB_32_FAT_SIZE);
		
	u32 tot_sect = (fat_load16(bpb + BPB_TOT_SECT_16)) ? 
		(u32)fat_load16(bpb + BPB_TOT_SECT_16) :
		fat_load32(bpb + BPB_TOT_SECT_32);
		
	u32 data_sectors = tot_sect - (fat_load16(bpb + BPB_RSVD_CNT) + 
		(bpb[BPB_NUM_FATS] * fat_size) + root_sectors);
		
	u32 data_clusters = data_sectors / bpb[BPB_CLUSTER_SIZE];
	
	// Check the cluster count to figure out the file system		
	if (data_clusters < 65525) {
		return 0;
	}
	
	return 1;
}

void fat32_thread(void* arg) {
	
	// Configure the hardware
	board_sd_card_config();
	
	print(ANSI_GREEN "FAT32 thread started\n" NORMAL);
	
	// Wait for the SD card to be insterted
	while (!board_sd_card_get_status());
	
	print("SD card inserted\n");
	
	disk_mount(DISK_SD_CARD);
	
	struct volume_s* vol = volume_get('C');
	print("Volume found (%c:) Sector size: %d Cluster size %d\n",
		vol->letter, vol->sector_size, vol->cluster_size);
	
	while (1) {
		
	}
}


//------------------------------------------------------------------------------
// FAT23 file system API
// This section will implement all the API functions
//------------------------------------------------------------------------------

u8 disk_mount(disk_e disk) {
	// Verify that the MSD are inserted
	if (!disk_get_status(disk)) {
		return 0;
	}
	
	// Initialize the hardware
	if (!disk_initialize(disk)) {
		return 0;
	}
	disk_read(disk, mount_buffer, 0, 1);

	// Check the boot signature at the MBR
	if (fat_load16(mount_buffer + MBR_BOOT_SIG) != MBR_BOOT_SIG_VALUE) {
		return 0;
	}
	
	struct partition_s partitions[4];
	
	for (u8 i = 0; i < 4; i++) {
		u32 offset = MBR_PARTITION + i * MBR_PARTITION_SIZE;
		partitions[i].lba = fat_load32(mount_buffer + offset + PAR_LBA);
		partitions[i].size = fat_load32(mount_buffer + offset + PAR_SIZE);
		partitions[i].type = mount_buffer[offset + PAR_TYPE];
		partitions[i].status = mount_buffer[offset + PAR_STATUS];
	}
	
	// Check the volume for a valid file system
	for (u8 i = 0; i < 4; i++) {
		if (partitions[i].lba) {
			disk_read(disk, mount_buffer, partitions[i].lba, 1);
			
			// The BPB structure is now in the mounting buffer
			if (fat_search(mount_buffer)) {
				
				// We have found a valid file system at partition i
				struct volume_s* vol = (struct volume_s *)
					dynamic_memory_new(DRAM_BANK_0, sizeof(struct volume_s));
				
				// Update FAT32 information
				vol->sector_size = fat_load16(mount_buffer + BPB_SECTOR_SIZE);
				vol->cluster_size = mount_buffer[BPB_CLUSTER_SIZE];
				vol->total_size = fat_load32(mount_buffer + BPB_TOT_SECT_32);
				
				// Update FAT32 offsets that will be used by the driver
				vol->info_lba = partitions[i].lba + fat_load16(mount_buffer +
					BPB_32_FSINFO);
				vol->fat_lba = partitions[i].lba + fat_load16(mount_buffer + 
					BPB_RSVD_CNT);
				vol->root_lba = vol->fat_lba + (fat_load32(mount_buffer + 
					BPB_32_FAT_SIZE) * mount_buffer[BPB_NUM_FATS]);
				
				fat_volume_add(vol);
			}
		}
	}
}

u8 disk_eject(disk_e disk);

struct volume_s* volume_get_first(void) {
	return first_volume;
}

struct volume_s* volume_get(char letter) {
	
	struct volume_s* vol = first_volume;
	
	while (vol != NULL) {
		if (vol->letter == letter) {
			return vol;
		}
		vol = vol->next;
	}
	
	return NULL;
}

fstatus volume_set_label(struct volume_s* vol, const char* name, u8 length);
fstatus volume_get_label(struct volume_s* vol, char* name, u8 length);
fstatus volume_format(struct volume_s* vol);
fstatus fat_dir_open(struct dir_s* dir, const char* path, u16 length);
fstatus fat_dir_close(struct dir_s* dir);
fstatus fat_dir_read(struct dir_s* dir, struct info_s* info);
fstatus fat_dir_make(const char* path);
fstatus fat_file_open(struct file_s* file, const char* path, u16 length);
fstatus fat_file_close(struct file_s* file);
fstatus fat_file_read(struct file_s* file, u8* buffer, u32 count, u32* status);
fstatus fat_file_write(struct file_s* file, const u8* buffer, u32 count);
fstatus fat_file_jump(struct file_s* file, u32 offset);