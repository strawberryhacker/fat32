# FAT32

This repository contains a minimal implementation of the FAT32 file system written in C with no dependencies. Just copy the fat.c and fat.h files. Its a little over 1K lines of code. It supports the most basic functions as well as reading and writing. 

The implementation buffers data in both the files and file system. Make sure to call `fat_fclose` and `fat_umount` after doing operations.

## Demo

The demo uses a disk image file (included in the repo). Becuse it uses stdio to read/write to the disk, it works on any Linux platform. Just run `make all`. If you want to create a disk image yourself, you can run these commands (note that there is a minimum size for FAT32).

```
dd if=/dev/zero of=disk.img bs=1M count=64
parted disk.img --script mklabel msdos
parted disk.img --script mkpart primary fat32 1MiB 100%
```

I you want to mount the disk image you can run

```
mkdir fat
losetup -P /dev/loopX disk.img
mount -o sync /dev/loopXp1 fat
```

If you want to synchronize changes you can run

```
umount fat
losetup -d /dev/loopX
```

## Paths

When mounting a file system you must specify a path. This must be used when accessing the file system. For example, if you mount in `usb`, you must use `/usb/path` to access it. The `fat_opendir` supports relative paths. You can use both . and .. in the path. 

## Porting

In order to use the filesystem you must provide functions for reading and writing to the disk. Note that only a block size of 512 bytes is supported (LBA). See the demo.

If you want to add timestamps to the file, you need to implement `fat_get_timestamp`. It is declared as weak, so you can just override it.

## Errors

```
FAT_ERR_NONE    - success
FAT_ERR_NOFAT   - not a supported FAT32 volume at the partition
FAT_ERR_BROKEN  - file system is broken
FAT_ERR_DISK    - unable to read/write to the disk
FAT_ERR_PARAM   - error in parameter list
FAT_ERR_PATH    - error in the path
FAT_ERR_EOF     - end of file or directory
FAT_ERR_DENIED  - permission errors related to fat_fopen
FAT_ERR_FULL    - disk is full
```

## API

#### fat_mount

Mounts a file system. Requires MBR partitioning scheme. It will scan the given partition in the MBR and try to detect and mount the FAT32 file system.

#### fat_umount

Sync the file system and remove it from the linked list.

#### fat_fopen

Opens a file for access. Note that the directory must exist. Standard fopen modes are supported (rwa+x). Refer to fopen documentation.

#### fat_fclose

Synchronizes the file buffer and the file system. Memsets the file object.

#### fat_fread

Reads a given number of bytes from the file. Use `fat_fseek` to change position. It returns the number of bytes read or an error code.

#### fat_fprintf

Writes a formatted string to the file system. The formatter uses a statically allocated buffer of 4096 bytes. This is the maximum allowed size for each call. If you do not need it, you can remove `fat_fprintf`, and all functions and enums prefixed with `fmt_xxx` in `fat.c`.

#### fat_fwrite

Writes a given number of bytes to the file. It updates the file size accordingly. It returns the number of bytes written or an error code.

#### fat_fseek

Updates the position in the file. If seeking beyond the end of the file, more clusters are allocated if needed. Supports FAT_SEEK_SET, FAT_SEEK_END and FAT_SEEK_CURR with a given offset.

#### fat_ftell

Returns the current file offset. This is always up to date.

#### fat_fsize

Returns the current file size. This is always up to date.

#### fat_fsync

Synchronizes the file buffer and the file system. Dirty data in the buffer or the fat window is written back. The fsinfo is update if needed. The parent directory is updated with the timestamp and file size.

#### fat_unlink

Deletes a file. 

#### fat_mkdir

Makes a directory. Note that recursive creation is not supported. If no folder exist and you need to create `/mnt/test/folder`, first create `/mnt/test`, then create `/mnt/test/folder`.

#### fat_opendir

Opens a directory. Directory position is placed at the beginning.

#### fat_readdir

Read the directory entry at the current position.

#### fat_nextdir

Advances the current position in the directory. If no more directories or files exist it will return FAT_ERR_EOF.

#### fat_get_error

Returns the string representation of the error code.
