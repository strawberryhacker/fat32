all:
	gcc -Wno-unused-function -Wall -O2 fat.c example.c -o example
	./example disk.img

clean:
	rm -f example