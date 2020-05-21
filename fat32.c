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


// These are used for volume mounting. All the volumes (i.e. partitions with a
// valid FAT32 file system) will be added to this linked list. All volumes will
// be dynamically allocated.
static struct volume_s* first_volume;
static u32 volume_bitmask;

// The mount buffer is used as a temporarily data buffer, untill the volume has
// mounted. It is used for MBR and FAT32 info retrieval. After the volume has
// been mounted the volumes own buffer will be used.
static u8 mount_buffer[512];

// LFN UCS-2 offsets
static const u8 lfn_off[] =  {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};


// Private function declarations
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

static u8 fat_dir_lfn_cmp(const u8* lfn, const char* name, u32 size);
static u8 fat_dir_sfn_cmp(const char* sfn, const char* name, u8 size);
static u8 fat_dir_sfn_crc(const u8* sfn);
static u8 fat_dir_set_index(struct dir_s* dir, u32 index);
static u8 fat_dir_get_next(struct dir_s* dir);
static u8 fat_dir_find(struct dir_s* dir, const char* name, u32 size);

static u8 fat_table_get(struct volume_s* vol, u32 cluster, u32* fat);
static u8 fat_table_set(struct volume_s* vol, u32 cluster, u32 fat_entry);

static u8 fat_read(struct volume_s* vol, u32 lba);
static u8 fat_flush(struct volume_s* vol);

static inline u32 fat_sect_to_clust(struct volume_s* vol, u32 sect);
static inline u32 fat_clust_to_sect(struct volume_s* vol, u32 clust);

static fstatus fat_follow_path(struct dir_s* dir, const char* path, u32 length);
static fstatus fat_get_vol_label(struct volume_s* vol, char* label);



// The following memory functions will not be called on big data sets. 
// Therefore it is not optimized towards speed. 
static void fat_memcpy(const void* src, void* dest, u32 count) {
	const u8* src_ptr = (const u8 *)src;
	u8* dest_ptr = (u8 *)dest;
	do {
		*dest_ptr++ = *src_ptr++;
	} while (--count);
}

static u8 fat_memcmp(const void* src_1, const void* src_2, u32 count) {
	if (count == 0) {
		return 1;
	}
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

// Store a 32-bit value in little-endian format
static void fat_store32(void* dest, u32 value) {
	u8* dest_ptr = (u8 *)dest;
	
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
}

// Store a 16-bit value in little-endian format
static void fat_store16(void* dest, u16 value) {
	u8* dest_ptr = (u8 *)dest;

	*dest_ptr++ = (u8)value;
	value >>= 8;
	*dest_ptr++ = (u8)value;
}

// Loads a 32-bit value from the src pointer in little-endian format
static u32 fat_load32(const void* src) {
	u32 value = 0;
	const u8* src_ptr = (const u8 *)src;

	value |= *src_ptr++;
	value |= (*src_ptr++ << 8);
	value |= (*src_ptr++ << 16);
	value |= (*src_ptr++ << 24);
	
	return value;
}

// Loads a 16-bit value from the src pointer in little-endian format
static u16 fat_load16(const void* src) {
	u16 value = 0;
	const u8* src_ptr = (const u8 *)src;
	
	value |= *src_ptr++;
	value |= (*src_ptr++ << 8);
	
	return value;
}

// Remove
static void fat_print_sector(const u8* sector) {
	for (u32 i = 0; i < 512;) {
		print("%c", sector[i]);
		
		if ((i++ % 32) == 0) {
			print("\n");
		}
	}
	print("\n");
}

// Adds a volume to the linked list and assignes a letter to it
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
	
	// Assign a letter to the volume
	for (u8 i = 0; i < 32; i++) {
		if ((volume_bitmask & (1 << i)) == 0) {
			volume_bitmask |= (1 << i);
			vol->letter = 'C' + i;
			break;
		}
	}
	return 1;
}

// Removes a volume from the linked list, but does not delete the memory
static u8 fat_volume_remove(char letter) {
	struct volume_s* curr;
	if (first_volume == NULL) {
		return 0;
	} else if (first_volume->letter == letter) {
		curr = first_volume;
		first_volume = first_volume->next;
	} else {
		struct volume_s* prev = first_volume;
		curr = first_volume->next;
		
		while (curr != NULL) {
			if (curr->letter == letter) {
				break;
			}
			prev = curr;
			curr = curr->next;
		}
		
		if (curr == NULL) return 0;
		prev->next = curr->next;
	}
	
	// Clear the bitmask so that the letter can be used by other volumes
	u8 bit_pos = curr->letter - 'C';
	volume_bitmask &= ~(1 << bit_pos);
	
	return 1;
}

// This functions will check for a valid file system and returns 1 if valid, and
// 0 if not. Note that only FAT32 is supported. The argument is a 512-byte BPB
static u8 fat_search(const u8* bpb) {
	
	// Check the BPB boot signature 
	if (fat_load16(bpb + 510) != 0xAA55) return 0;
	
	// Check for the "FAT" string
	// This will indicate a FAT file system, but does not say anything about
	// the FAT12 / FAT16 or FAT32 type.
	if (!fat_memcmp(bpb + BPB_32_FSTYPE, "FAT", 3)) {
		if (!fat_memcmp(bpb + BPB_16_FSTYPE, "FAT", 3)) {
			return 0;
		}
	}
	
	// We know that we have a valid FAT12 / FAT16 or FAT32 file system
	// To determine which, we count the number of data clusters.
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

// Calculates the SFN checksum based on the 8.3 file name
static u8 fat_dir_sfn_crc(const u8* sfn) {
	u8 crc = 0;
	u8 count = 11;
	do {
		crc = ((crc & 1) << 7) + (crc >> 1) + *sfn++;
	} while (--count);
	
	return crc;
}

// This function moves the directory pointer to the directory entry number
// 'index'. One entry is counted as 32-byte and does not represent a LFN block.
static u8 fat_dir_set_index(struct dir_s* dir, u32 index) {
	return 1;
}

// Gets the next 32-byte directory entry. This returns 0 if it is the last 
// entry in the FAT chain.
static u8 fat_dir_get_next(struct dir_s* dir) {
	dir->rw_offset += 32;
	
	// Check for rw position overflow
	if (dir->rw_offset >= dir->vol->sector_size) {
		dir->rw_offset -= dir->vol->sector_size;
		dir->sector++;
		
		// Check for sector overflow. In this case the next sector (and cluster)
		// can be retrieved from the FAT table
		if (dir->sector >= (fat_clust_to_sect(dir->vol, dir->cluster) +
				dir->vol->cluster_size)) {	
			u32 new_cluster;
			if (!fat_table_get(dir->vol, dir->cluster, &new_cluster)) {
				return 0;
			}
			
			// Check if the FAT table entry is the EOC
			u32 eoc_value = new_cluster & 0xFFFFFFF;
			if ((eoc_value >= 0xFFFFFF8) && (eoc_value <= 0xFFFFFFF)) {
				return 0;
			}
			
			dir->cluster = new_cluster;
			dir->sector = fat_clust_to_sect(dir->vol, dir->cluster);
		}
	}
	return 1;
}

// Takes in a cluster and return the 4-byte FAT table entry corresponding to
// this cluster. 
static u8 fat_table_get(struct volume_s* vol, u32 cluster, u32* fat) {
	u32 start_sect = vol->fat_lba + cluster / 128;
	u32 start_off = cluster % 128;
	
	if (!fat_read(vol, start_sect)) {
		return 0;
	}
	*fat = fat_load32(vol->buffer + start_off * 4);
	return 1;
}

// Takes in a cluster and a FAT table entry and updates the cluster FAT table
// entry with this value.
static u8 fat_table_set(struct volume_s* vol, u32 cluster, u32 fat_entry) {
	u32 start_sect = vol->fat_lba + cluster / 128;
	u32 start_offset = cluster % 128;
	
	if (!fat_read(vol, start_sect)) {
		return 0;
	}
	fat_store32(vol->buffer + start_offset, fat_entry);
	
	return 1;
}

// Takes in a LBA and update the volumes local buffer if needed. After this 
// function returns, the LBA sector is clean and valid
static u8 fat_read(struct volume_s* vol, u32 lba) {
	
	// Check if the current buffer contains any dirty data, and if this is the
	// case, write it back to the MSD before reading
	if (!fat_flush(vol)) {
		return 0;
	}
	if (vol->buffer_lba == lba) {
		// Not nessecary to update buffer
		return 1;
	}
	if (!disk_read(vol->disk, vol->buffer, lba, 1)) {
		return 0;
	}
	vol->buffer_lba = lba;
	return 1;
}

// Check if the buffer is dirty and cleans it
static u8 fat_flush(struct volume_s* vol) {
	if (vol->buffer_dirty) {
		if (!disk_write(vol->disk, vol->buffer, vol->buffer_lba, 1)) {
			return 0;
		}
		vol->buffer_dirty = 0;
	}
	return 1;
}

// Maps a sector LBA to the relative cluster number and vica verca
static inline u32 fat_sect_to_clust(struct volume_s* vol, u32 sect) {
	return (sect - vol->data_lba) / vol->cluster_size + 2;
}

static inline u32 fat_clust_to_sect(struct volume_s* vol, u32 clust) {
	return ((clust - 2) * vol->cluster_size) + vol->data_lba;
}

// Universal compare without caring for capitals vs non-capitals
static u8 fat_dir_sfn_cmp(const char* sfn, const char* name, u8 size) {
	if (size > 8) {
		size = 8;
	}
	do {
		char tmp_char = *name;
		if (tmp_char >= 'a' && tmp_char <= 'z') {
			tmp_char -= 32;
		}
		
		if (tmp_char != *sfn) {
			return 0;
		}
		sfn++;
		name++;
	} while (--size);
	return 1;
}

// Takes in a pointer to a LFN entry and check if it matches the given name
static u8 fat_dir_lfn_cmp(const u8* lfn, const char* name, u32 size) {
	
	// Calculate the name offset
	u8 name_off = 13 * ((lfn[LFN_SEQ] & LFN_SEQ_MSK) - 1);
	
	for (u8 i = 0; i < 13; i++) {
		if (lfn[lfn_off[i]] == 0x00 || lfn[lfn_off[i]] == 0xff) {
			break;
		}

		if (lfn[lfn_off[i]] != name[name_off + i]) {
			return 0;
		}
	}
	return 1;
}

// Takes in a pointer to a directory and tries to find a name match
static u8 fat_dir_find(struct dir_s* dir, const char* name, u32 size) {
	
	// If the directory object is not pointing to the directory start, fix it.
	if (dir->start_sect != dir->sector) {
		dir->sector = dir->start_sect;
		dir->cluster = fat_sect_to_clust(dir->vol, dir->sector);
		dir->rw_offset = 0;
	}
	
	u8 lfn_crc = 0;
	u8 lfn_match;
	u8 match = 0;
	
	while (1) {
		// Update the buffer if needed
		if (!fat_read(dir->vol, dir->sector)) {
			
			return 0;
		}
		u8* curr_buff = dir->vol->buffer;
		
		// Check if the directory is valid
		u32 rw_tmp = dir->rw_offset;
		u8 sfn_tmp = curr_buff[rw_tmp];
		
		if (sfn_tmp == 0x00) {
			break;
		}
		
		// Only allow to compare used folders
		if (!((sfn_tmp == 0x00) || (sfn_tmp == 0x05) || (sfn_tmp == 0xE5))) {
			
			// Check if the entry pointed to by dir is a LFN or a SFN
			if ((curr_buff[rw_tmp + SFN_ATTR] & ATTR_LFN) == ATTR_LFN) {
				
				// If the LFN name does not match the input
				if (!fat_dir_lfn_cmp(curr_buff + rw_tmp, name, size)) {
					lfn_match = 0;
				}
				lfn_crc = curr_buff[rw_tmp + LFN_CRC];
			} else {
				if (lfn_crc && lfn_match) {
					// Update the directory
					if (lfn_crc == fat_dir_sfn_crc(curr_buff + rw_tmp)) {
						match = 1;
					}
				} else {
					// Compare name with 8.3 file name
					if (fat_dir_sfn_cmp((char *)curr_buff + rw_tmp, name, size)) {
						match = 1;
					}
				}
				if (match) {
					// The directory is found, so update the dir pointer
					dir->cluster = (fat_load16(curr_buff + rw_tmp +
						SFN_CLUSTH) << 16) | fat_load16(curr_buff +
						rw_tmp + SFN_CLUSTL);
					dir->sector = fat_clust_to_sect(dir->vol, dir->cluster);
					dir->start_sect = dir->sector;
					dir->rw_offset = 0;

					return 1;
				}
				lfn_match = 1;
				lfn_crc = 0;
			}
		}
		if (!fat_dir_get_next(dir)) {
			return 0;
		}
	}
	return 0;
}

// This function will take in a path and a blank directory object
// The function will follow the path and return the directory object
// to the last found folder. 
static fstatus fat_follow_path(struct dir_s* dir, const char* path, u32 length) {
	
	// Get the right volume from the path
	struct volume_s* vol = volume_get(*path++);
	if (vol == NULL) {
		return FSTATUS_NO_VOLUME;
	}
	// Update the dir info with the root cluster data
	dir->vol = vol;
	dir->sector = vol->root_lba;
	dir->start_sect = vol->root_lba;
	dir->cluster = fat_clust_to_sect(vol, vol->root_lba);
	dir->rw_offset = 0;
	
	// Check for the colon
	if (*path != ':') {
		return FSTATUS_PATH_ERR;
	}
	path++;
	if (*path != '/') {
		return FSTATUS_PATH_ERR;
	}
	
	// These variables with hold the temporarily fragmented name
	const char* frag_ptr;
	u8 frag_size;

	while (1) {
		
		// Interate the path until a backslash is found
		while (*path && (*path != '/')) {
			path++;
		}
		if (*path++ == '\0') break;
		if (*path == '\0') break;
		
		// Path points to the first character in the name fragment
		const char* tmp_ptr = path;
		frag_ptr = path;
		frag_size = 0;
		
		while ((*tmp_ptr != '\0') && (*tmp_ptr != '/')) {
			
			// The current name fragment is a file, However, the name fragment
			// before it has been found. The dir is pointing to that entry. 
			if (*tmp_ptr == '.') {
				return FSTATUS_OK;
			}
			frag_size++;
			tmp_ptr++;
		}
		
		// After this point frag_ptr will point to the first character in
		// the name fragment, and the frag_size will contain the name fragment
		// size.
		
		// Print the directory name we are searching for
		print("Searching for directory: " ANSI_GREEN);
		print_count(frag_ptr, frag_size);
		print("\n" ANSI_NORMAL);

		// Start from the first entry and search for the name fragment
		if (!fat_dir_find(dir, frag_ptr, frag_size)) {
			print(ANSI_RED "Directory not fount\n" ANSI_NORMAL);
			return 0;
		} else {
			print(ANSI_GREEN "Directory found\n" ANSI_NORMAL);
		}
	}	
	print(ANSI_RED "Path found\n" ANSI_NORMAL);
	return FSTATUS_OK;
}

// Gets the volume label
// The volume label can be found two different places, in the BPB section or in
// the root directory. For some reason Microsoft tend to store it in the root
// directory. This functions will do the same. 
static fstatus fat_get_vol_label(struct volume_s* vol, char* label) {	
	// Make a dir object pointing to the root directory
	struct dir_s dir;
	dir.sector = vol->root_lba;
	dir.rw_offset = 0;
	dir.cluster = fat_sect_to_clust(vol, dir.sector);
	
	while (1) {
		if (!fat_read(vol, dir.sector)) {
			return FSTATUS_ERROR;
		}
		
		// Check if the attribute is volume label
		u8 attribute = vol->buffer[dir.rw_offset + SFN_ATTR];
		if (attribute & ATTR_VOL_LABEL) {
			
			// Check that it is not a LFN entry
			if ((attribute & ATTR_LFN) != ATTR_LFN) {
				const char* src = (const char *)(vol->buffer + dir.rw_offset);
				for (u8 i = 0; i < 11; i++) {
					// The volume label is padded with spaces
					*label++ = *src++;
				}
				return 1;
			}
		}
		
		// Get the next directory
		if (!fat_dir_get_next(&dir)) {
			return 0;
		}
	}
}


//------------------------------------------------------------------------------
// FAT23 file system API
// This section will implement the API exposed to the user
//------------------------------------------------------------------------------

void fat32_thread(void* arg) {
	
	// Configure the hardware
	board_sd_card_config();
	
	// Wait for the SD card to be insterted
	while (!board_sd_card_get_status());
	
	// Try to mount the disk. If this is not working the disk initialize 
	// functions may be ehh...
	disk_mount(DISK_SD_CARD);
	
	// Print all the volumes on the system
	print("Displaying system volumes:\n");
	struct volume_s* vol = volume_get_first();
	while (vol) {
		for (u8 i = 0; i < 11; i++) {
			if (vol->label[i]) {
				print("%c", vol->label[i]);
			}
		}
		print(" (" ANSI_RED "%c" ANSI_NORMAL ":)\n", vol->letter);
		vol = vol->next;
	}
	print("\n");
	
	struct dir_s dir;
	fat_follow_path(&dir, "C:/alpha/bravo/charlie", 0);
	fat_follow_path(&dir, "D:/tommy/tyckar/om/meg/", 0);
	
	while (1) {
		
	}
}

// Mounts a physical disk. This will try to mount all the filesystem available. 
// With the standard MBR one physical disk can contain 4 native file system.
u8 disk_mount(disk_e disk) {
	
	// Verify that the MSD are inserted
	if (!disk_get_status(disk)) {
		return 0;
	}
	
	// Initialize the hardware
	if (!disk_initialize(disk)) {
		return 0;
	}
	// Read the MBR sector at LBA address zero
	disk_read(disk, mount_buffer, 0, 1);

	// Check the boot signature in the MBR
	if (fat_load16(mount_buffer + MBR_BOOT_SIG) != MBR_BOOT_SIG_VALUE) {
		return 0;
	}
	
	// The first part will only retrive the partition info. This is to aviod
	// multiple MBR sector memory loads. 
	struct partition_s partitions[4];
	for (u8 i = 0; i < 4; i++) {
		u32 offset = MBR_PARTITION + i * MBR_PARTITION_SIZE;
		partitions[i].lba = fat_load32(mount_buffer + offset + PAR_LBA);
		partitions[i].size = fat_load32(mount_buffer + offset + PAR_SIZE);
		partitions[i].type = mount_buffer[offset + PAR_TYPE];
		partitions[i].status = mount_buffer[offset + PAR_STATUS];
	}
	
	// Check all the valid partitions for a valid file system
	for (u8 i = 0; i < 4; i++) {
		if (partitions[i].lba) {
			disk_read(disk, mount_buffer, partitions[i].lba, 1);
			
			// The BPB structure is now in the mounting buffer
			if (fat_search(mount_buffer)) {
				
				// We have found a valid file system at partition number i.
				// Allocate memory for the volume (more than 1 KiB)
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
				vol->data_lba = vol->fat_lba + (fat_load32(mount_buffer + 
					BPB_32_FAT_SIZE) * mount_buffer[BPB_NUM_FATS]);
				
				vol->root_lba = fat_clust_to_sect(vol, fat_load32(mount_buffer +
					BPB_32_ROOT_CLUST));
				vol->disk = disk;
				
				vol->buffer_lba = 0;

				// Add the newly made volume to the list of system volumes
				fat_volume_add(vol);
				
				// Get the volume label
				fat_get_vol_label(vol, vol->label);
			}
		}
	}
	return 1;
}

// This function should be called after a MSD has been unplugged. It will remove
// all the corresponding volumes and delete the memory. 
u8 disk_eject(disk_e disk) {
	struct volume_s* vol = volume_get_first();
	
	while (vol != NULL) {
		if (vol->disk == disk) {
			if (!fat_volume_remove(vol->letter)) {
				return 0;
			}
			dynamic_memory_free(vol);
		}
		vol = vol->next;
	}
	return 1;
}

// Gets the first volume available. Typically used by the kernel or other 
// software to list all the available volumes. 
struct volume_s* volume_get_first(void) {
	return first_volume;
}

// Gets a volume from the linked list base on the volume letter
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