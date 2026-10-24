; AliceOS 2 - Multiboot header, kernel entry, interrupt stubs
[bits 32]

MB_MAGIC equ 0x1BADB002
MB_FLAGS equ (1 << 0) | (1 << 1) | (1 << 2)   ; align modules, memory info, video mode

section .multiboot
align 4
    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)
    dd 0, 0, 0, 0, 0          ; address fields (unused, ELF)
    dd 0                      ; mode_type: linear graphics
    dd 1024                   ; width
    dd 768                    ; height
    dd 32                     ; depth

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .text
global _start
extern kmain
_start:
    mov esp, stack_top
    push ebx                  ; multiboot info pointer
    push eax                  ; multiboot magic
    call kmain
.hang:
    cli
    hlt
    jmp .hang

extern isr_handler

%macro ISR_NOERR 1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro
%macro ISR_ERR 1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
%assign i 18
%rep 30
ISR_NOERR i
%assign i i+1
%endrep

isr_common:
    pusha
    cld
    push esp
    call isr_handler
    add esp, 4
    popa
    add esp, 8
    iret

section .data
global isr_table
isr_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep
