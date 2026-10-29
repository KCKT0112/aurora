/* Aurora v2 - ring-3 C runtime entry. */
.section .text.start, "ax"
.global _start
.type _start, @function
_start:
    xorq %rbp, %rbp
    andq $-16, %rsp
    call umain

    movq $1, %rax              /* SYS_EXIT */
    movq %rax, %rdi
    int $0x80
1:
    hlt
    jmp 1b
.size _start, . - _start

.section .note.GNU-stack, "", @progbits

.section .note.aurora, "a", @note
.balign 4
.long 8, 4, 1
.asciz "AURORA"
.byte 0
.long 4
