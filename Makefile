.PHONY : all

CFLAGS = -O0 -ggdb

all : vmm bios.bin

clean :
	@-rm -f vmm vmm.o
	@-rm -f bios.bin

vmm : vmm.o

vmm.o : vmm.c

bios.bin : bios.asm
	nasm -f bin -o $@ $<
	
