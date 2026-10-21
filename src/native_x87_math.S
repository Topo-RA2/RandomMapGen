.intel_syntax noprefix

.section .rodata
.align 8
rmg_uint32_unit:
    .quad 0x3DF0000000100000
rmg_one:
    .quad 0x3FF0000000000000

.text
.globl rmg_native_x87_gaussian_scale_argument
.type rmg_native_x87_gaussian_scale_argument, @function
.globl rmg_native_x87_chop53_add
.type rmg_native_x87_chop53_add, @function
.globl rmg_native_x87_chop53_multiply
.type rmg_native_x87_chop53_multiply, @function

# System V x86_64 ABI: xmm0=left, xmm1=right, xmm0=result.
rmg_native_x87_chop53_add:
    sub rsp, 24
    movsd qword ptr [rsp + 8], xmm0
    movsd qword ptr [rsp + 16], xmm1
    fnstcw word ptr [rsp]
    mov word ptr [rsp + 2], 0x0E7F
    fldcw word ptr [rsp + 2]
    fld qword ptr [rsp + 8]
    fadd qword ptr [rsp + 16]
    fstp qword ptr [rsp + 8]
    fldcw word ptr [rsp]
    movsd xmm0, qword ptr [rsp + 8]
    add rsp, 24
    ret
.size rmg_native_x87_chop53_add, .-rmg_native_x87_chop53_add

rmg_native_x87_chop53_multiply:
    sub rsp, 24
    movsd qword ptr [rsp + 8], xmm0
    movsd qword ptr [rsp + 16], xmm1
    fnstcw word ptr [rsp]
    mov word ptr [rsp + 2], 0x0E7F
    fldcw word ptr [rsp + 2]
    fld qword ptr [rsp + 8]
    fmul qword ptr [rsp + 16]
    fstp qword ptr [rsp + 8]
    fldcw word ptr [rsp]
    movsd xmm0, qword ptr [rsp + 8]
    add rsp, 24
    ret
.size rmg_native_x87_chop53_multiply, .-rmg_native_x87_chop53_multiply

# System V x86_64 ABI:
# edi=rawX, esi=rawY, rdx=x, rcx=y, r8=radiusSquared, r9=scaleArgument.
rmg_native_x87_gaussian_scale_argument:
    sub rsp, 24

    fnstcw word ptr [rsp]
    mov word ptr [rsp + 2], 0x0E7F
    fldcw word ptr [rsp + 2]

    mov eax, edi
    mov qword ptr [rsp + 8], rax
    mov eax, esi
    mov qword ptr [rsp + 16], rax

    fild qword ptr [rsp + 8]
    fmul qword ptr [rip + rmg_uint32_unit]
    fadd st(0), st(0)
    fsub qword ptr [rip + rmg_one]
    fstp qword ptr [rdx]

    fild qword ptr [rsp + 16]
    fmul qword ptr [rip + rmg_uint32_unit]
    fadd st(0), st(0)
    fsub qword ptr [rip + rmg_one]
    fst qword ptr [rcx]

    fld st(0)
    fmulp st(1), st(0)
    fld qword ptr [rdx]
    fmul qword ptr [rdx]
    faddp st(1), st(0)
    fst qword ptr [r8]

    fld st(0)
    fldln2
    fxch st(1)
    fyl2x
    fld st(0)
    fchs
    fsub st(0), st(1)
    fdiv st(0), st(2)
    fstp qword ptr [r9]
    fstp st(0)
    fstp st(0)

    fldcw word ptr [rsp]
    add rsp, 24
    ret
.size rmg_native_x87_gaussian_scale_argument, .-rmg_native_x87_gaussian_scale_argument

.section .note.GNU-stack,"",@progbits
