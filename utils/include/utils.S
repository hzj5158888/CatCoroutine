.text
.global mm_get_mxcsr
mm_get_mxcsr:
    push $0x0
    stmxcsr (%rsp)
    pop %rax
    ret

.global mm_get_x87cw
mm_get_x87cw:
    push $0x0
    fnstcw (%rsp)
    pop %rax
    ret

.section .note.GNU-stack,"",%progbits