# FAT32

This is a lighweight FAT32 file system written in C with no thirdparty dependencies. It requires a small port which provide functions for initializing, reading and writing to the MSD. 

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
