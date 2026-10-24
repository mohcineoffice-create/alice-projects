; AliceOS - stage 1 bootloader (512 bytes, MBR)
; Loads the kernel from disk (LBA 1..), enables A20, switches to 32-bit protected mode.
[org 0x7c00]
[bits 16]

KERNEL_SEG     equ 0x1000          ; kernel loaded at 0x1000:0000 = 0x10000
KERNEL_SECTORS equ 64              ; 32 KB

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00
    sti
    mov [boot_drive], dl

    mov si, msg_loading
    call print16

    ; read kernel with BIOS LBA extensions (int 13h, ah=42h)
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error

    ; enable A20 (fast method)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; enter protected mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEL:pm_entry

disk_error:
    mov si, msg_disk
    call print16
    jmp $

print16:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    jmp print16
.done:
    ret

[bits 32]
pm_entry:
    mov ax, DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    jmp CODE_SEL:0x10000

; ---------------- data ----------------
boot_drive db 0
msg_loading db "AliceOS: loading kernel...", 13, 10, 0
msg_disk    db "Disk read error", 0

align 4
dap:
    db 0x10, 0
    dw KERNEL_SECTORS
    dw 0x0000, KERNEL_SEG      ; offset, segment
    dq 1                       ; start LBA

gdt_start:
    dq 0
gdt_code:                      ; base 0, limit 4G, exec/read
    dw 0xffff, 0
    db 0, 10011010b, 11001111b, 0
gdt_data:                      ; base 0, limit 4G, read/write
    dw 0xffff, 0
    db 0, 10010010b, 11001111b, 0
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEL equ gdt_code - gdt_start
DATA_SEL equ gdt_data - gdt_start

times 510-($-$$) db 0
dw 0xaa55
