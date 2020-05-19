//----------------------------------------------------------------------------//

// Copyright (c) 2020 Bjørn Brodtkorb
//
// This software is provided without warranty of any kind.
// Permission is granted, free of charge, to copy and modify this
// software, if this copyright notice is included in all copies of
// the software.

#include "disk_interface.h"
#include "board_sd_card.h"
#include "sd_protocol.h"

// Make a clobal SD card structure
sd_card sd_slot_1;

u8 disk_get_status(disk_e disk) {
	switch (disk) {
		case DISK_SD_CARD:
		return (u8)board_sd_card_get_status();
		break;
	}
}

u8 disk_initialize(disk_e disk) {
	switch (disk) {
		case DISK_SD_CARD:
		return (u8)sd_protocol_config(&sd_slot_1);
		break;
	}
}

u8 disk_read(disk_e disk, u8* buffer, u32 lba, u32 count) {
	switch (disk) {
		case DISK_SD_CARD:
		return sd_protocol_read(&sd_slot_1, buffer, lba, count);
		break;
	}
}

u8 disk_write(disk_e disk, const u8* buffer, u32 lba, u32 count) {
	switch (disk) {
		case DISK_SD_CARD:
		return sd_protocol_write(&sd_slot_1, buffer, lba, count);
		break;
	}
}