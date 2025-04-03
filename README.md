# FAT32

This is a minimal FAT32 library written in C. No external dependencies. A little over 1K lines of code. Binary size is around 8KiB (ARM) when all functionality is used. It is not threadsafe.

## Supported functionality

- Long file names (LFN)
- Multipartition (MBR only)
- Probing the disk before mounting to detect file systems
- File timestamps
- Unlink file or empty directory
- Stat a file or directory
- File create, open, read, write, seek and sync
- Directory create, open, read, rewind

## Usage

Create functions for reading/writing 512 byte sectors on the drive (LBA addressing). Create a `DiskOps` structure and pass it to `fat_mount`. User is responsible for all disk related operations. See example.

Override `fat_get_timestamp` to enable timestamps. Default is 01/01/1980 00:00:00. See example.

## Example

The library includes an example. It uses disk image formatted FAT32 (attached). I ran the following commands to make it:

```
dd if=/dev/zero of=disk.img bs=1M count=64
parted disk.img --script mklabel msdos
parted disk.img --script mkpart primary fat32 1MiB 100%
```

To mount the image:

```
mkdir fat
losetup -P /dev/loopX disk.img
mount -o sync /dev/loopXp1 fat
```

To synchronize changes:

```
umount fat
losetup -d /dev/loopX
```

## Documentation

See the source file.