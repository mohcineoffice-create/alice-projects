# AliceOS 0.1

A tiny 32-bit operating system written from scratch in x86 assembly and C — no Linux, no libraries.

## What it does
- **Bootloader (512 bytes):** loads the kernel from disk with BIOS LBA, enables A20, sets up a GDT and switches the CPU to 32-bit protected mode.
- **Kernel:** VGA text console with scrolling, interrupt table (IDT) with CPU-exception handlers, remapped PIC, 100 Hz timer, keyboard driver (with Shift), real-time clock, CPUID.
- **Status bar:** OS name, uptime and clock, refreshed every second by the timer interrupt.
- **Shell commands:** `help`, `clear`, `about`, `echo`, `time`, `uptime`, `sysinfo`, `calc`, `color`, `panic`, `reboot`.
- **Kernel panic screen:** any CPU exception (e.g. division by zero) halts the system with a clear message.

## Files
| File | Role |
|------|------|
| `boot.asm` | Stage-1 bootloader (MBR) |
| `entry.asm` | Kernel entry point and 48 interrupt stubs |
| `kernel.c` | Kernel: console, interrupts, drivers, shell |
| `link.ld` | Linker script (kernel at 0x10000) |
| `Makefile` | Build and run |
| `aliceos.img` | Ready-to-boot disk image (1.44 MB) |

## Run it
**Windows:** install QEMU (qemu.org), then in the folder:
```
qemu-system-i386 -drive format=raw,file=aliceos.img
```
**Linux:** `make run`

## Build it (Linux)
```
sudo apt install nasm gcc binutils qemu-system-x86
make
```

## Limits (v0.1)
- BIOS boot only: real PCs need "Legacy/CSM" mode; UEFI-only machines will not boot it.
- No file system, no multitasking, no memory paging, no mouse, no graphics.

## Possible next steps
Memory manager and paging · simple file system (FAT12) · multitasking · VGA graphics mode · small games (snake).
