; AliceOS - kernel entry point and interrupt stubs
[bits 32]
section .text.entry
global _start
extern kmain
_start:
    call kmain
.hang:
    hlt
    jmp .hang

section .text
extern isr_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

; CPU exceptions 0-31 (8, 10-14, 17 push an error code)
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
%rep 14
ISR_NOERR i
%assign i i+1
%endrep
; hardware IRQs 0-15 -> vectors 32-47
%assign i 32
%rep 16
ISR_NOERR i
%assign i i+1
%endrep

isr_common:
    pusha
    push esp            ; pointer to the register frame
    call isr_handler
    add esp, 4
    popa
    add esp, 8          ; vector + error code
    iret

global isr_table
section .data
isr_table:
%assign i 0
%rep 48
    dd isr %+ i
%assign i i+1
%endrep
