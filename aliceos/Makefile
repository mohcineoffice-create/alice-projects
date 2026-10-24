CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -nostdlib -O2 -Wall -Wextra -fno-builtin -mno-sse -mno-mmx -mno-80387

all: aliceos.img

boot.bin: boot.asm
	nasm -f bin boot.asm -o boot.bin

entry.o: entry.asm
	nasm -f elf32 entry.asm -o entry.o

kernel.o: kernel.c
	gcc $(CFLAGS) -c kernel.c -o kernel.o

kernel.bin: entry.o kernel.o link.ld
	ld -m elf_i386 -T link.ld entry.o kernel.o -o kernel.elf
	objcopy -O binary kernel.elf kernel.bin
	@test $$(stat -c%s kernel.bin) -le 32768 || (echo "kernel too big"; exit 1)

aliceos.img: boot.bin kernel.bin
	dd if=/dev/zero of=aliceos.img bs=512 count=2880 status=none
	dd if=boot.bin of=aliceos.img conv=notrunc status=none
	dd if=kernel.bin of=aliceos.img seek=1 conv=notrunc status=none

run: aliceos.img
	qemu-system-i386 -drive format=raw,file=aliceos.img

clean:
	rm -f *.bin *.o *.elf aliceos.img
