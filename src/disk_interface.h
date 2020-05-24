//------------------------------------------------------------------------------
// MIT license
// Copyright (c) 2020 StrawberryHacker
//------------------------------------------------------------------------------

#ifndef DISK_INTERFACE_H
#define DISK_INTERFACE_H

#include "fat_types.h"

/// Add new physical disk here
typedef enum {
	DISK_SD_CARD
} disk_e;

/// Returns the status of the MSD (mass storage device)
u8 disk_get_status(disk_e disk);

/// Initializes at disk intrface
u8 disk_initialize(disk_e disk);

/// Read a number of sectors from the MSD
u8 disk_read(disk_e disk, u8* buffer, u32 lba, u32 count);

/// Write a number of sectors to the MSD
u8 disk_write(disk_e disk, const u8* buffer, u32 lba, u32 count);

#endif