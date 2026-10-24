CC      = gcc
CFLAGS  = -m32 -std=gnu11 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -nostdlib \
          -mgeneral-regs-only -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS = -m32 -nostdlib -no-pie -Wl,--build-id=none -T linker.ld

OBJS = build/boot.o build/kernel.o build/mem.o build/fs.o build/gfx.o build/gui.o build/apps.o \
       build/app_terminal.o build/app_files.o build/app_notes.o build/app_paint.o build/app_calc.o \
       build/app_snake.o build/app_system.o build/div64.o build/assets.o

all: aliceos.iso

src/assets.c src/assets.h: tools/gen_assets.py
	python3 tools/gen_assets.py

build/%.o: src/%.c src/os.h src/types.h src/assets.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/boot.o: src/boot.asm | build
	nasm -f elf32 $< -o $@

build:
	mkdir -p build

aliceos.elf: $(OBJS) linker.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@

aliceos.iso: aliceos.elf grub.cfg
	mkdir -p iso/boot/grub
	cp aliceos.elf iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o $@ iso 2>/dev/null

run: aliceos.iso
	qemu-system-i386 -cdrom aliceos.iso -m 256

clean:
	rm -rf build iso aliceos.elf aliceos.iso
