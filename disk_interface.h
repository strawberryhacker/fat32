//----------------------------------------------------------------------------//

// Copyright (c) 2020 Bjørn Brodtkorb
//
// This software is provided without warranty of any kind.
// Permission is granted, free of charge, to copy and modify this
// software, if this copyright notice is included in all copies of
// the software.

#ifndef DISK_INTERFACE_H
#define DISK_INTERFACE_H

#include "fat_types.h"

typedef enum {
	DISK_SD_CARD
} disk_e;


// Returns the status of the MSD (mass storage device)
u8 disk_get_status(disk_e disk);

// Initializes at disk intrface
u8 disk_initialize(disk_e disk);

// Read a number of sectors from the MSD
u8 disk_read(disk_e disk, u8* buffer, u32 lba, u32 count);

// Write a number of sectors to the MSD
u8 disk_write(disk_e disk, const u8* buffer, u32 lba, u32 count);

#endif