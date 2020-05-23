# FAT32

This is a lighweight FAT32 file system written in C with no thirdparty dependencies. It requires a small port which provide functions for initializing, reading and writing to the MSD. 

## Requirements

A small disk interface layer is required in order for the FAT32 driver to work. This interface should implement the following functions
 - **get disk status** - this will check if the storage device is plugged in an available
 - **disk initialize** - this will initialize the hardware and software protocols on the storage device
 - **disk read** - this will read a specified number of sectors from the storage device and store it in a buffer
 - **disk write** - this will write a specified number of sectors to the stordage device
 - **get time** - get the current time from a RTC or over NTP (optional)

These functions take in a number which is associated with each disk. The FAT32 disk mount functions will take in the same disk number. For example, if an SD card is referenced in the disk interface as device 2, then disk_mount(2) will mount the SD card. Take a look at the disk_interface.c for some examples.

## Functionality

Disk functions 
 - Mounting a volume
 - Ejecting a volume

Volume functions
 - Get a volume based on the letter
 - Set volume label
 - Get volume label
 - Format a FAT32 volume (both quick format and normal format)
 - LFN and SFN support
 
Directory functions
 - Directory open
 - Directory close
 - Directory read
 - Directory make
 - Directory rename

File functions
  - File open
  - File close
  - File read
  - File write
  - File jump
  - File rename
  - File clear
 
The file and directory functions work the same way as in windows. The functions with take inn a path including the volume letter e.g. C:/home/user/strawberryhacker/README.md
 
## Support 

If more functionality is needed send me a message
