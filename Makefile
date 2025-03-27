all:
	gcc -Wno-unused-function -Wall -O2 fat.c demo.c -o demo

test:
	-@sudo losetup -d /dev/loop2 || true
	sudo losetup -P /dev/loop2 disk.img
	sudo fsck /dev/loop2p1