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

#include "disk_interface.h"
#include "board_sd_card.h"
#include "sd_protocol.h"

/// Make a clobal SD card structure
sd_card sd_slot_1;

u8 disk_get_status(disk_e disk) {
	switch (disk) {
		case DISK_SD_CARD: {
			return (u8)board_sd_card_get_status();
		}
	}
	return 0;
}

u8 disk_initialize(disk_e disk) {
	switch (disk) {
		case DISK_SD_CARD: {
			return (u8)sd_protocol_config(&sd_slot_1);
		}
	}
	return 0;
}

u8 disk_read(disk_e disk, u8* buffer, u32 lba, u32 count) {
	switch (disk) {
		case DISK_SD_CARD: {
			return sd_protocol_read(&sd_slot_1, buffer, lba, count);
		}
	}
	return 0;
}

u8 disk_write(disk_e disk, const u8* buffer, u32 lba, u32 count) {
	switch (disk) {
		case DISK_SD_CARD: {
			return sd_protocol_write(&sd_slot_1, buffer, lba, count);
		}
	}
	return 0;
}
