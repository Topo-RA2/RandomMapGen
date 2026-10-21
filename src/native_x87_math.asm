default rel

section .rdata align=8
rmg_uint32_unit: dq 0x3DF0000000100000
rmg_one:         dq 0x3FF0000000000000

section .text
global rmg_native_x87_gaussian_scale_argument
global rmg_native_x87_chop53_add
global rmg_native_x87_chop53_multiply

; Windows x64 ABI: xmm0=left, xmm1=right, xmm0=result.
rmg_native_x87_chop53_add:
    sub rsp, 24
    movsd [rsp + 8], xmm0
    movsd [rsp + 16], xmm1
    fnstcw word [rsp]
    mov word [rsp + 2], 0x0E7F
    fldcw word [rsp + 2]
    fld qword [rsp + 8]
    fadd qword [rsp + 16]
    fstp qword [rsp + 8]
    fldcw word [rsp]
    movsd xmm0, [rsp + 8]
    add rsp, 24
    ret

rmg_native_x87_chop53_multiply:
    sub rsp, 24
    movsd [rsp + 8], xmm0
    movsd [rsp + 16], xmm1
    fnstcw word [rsp]
    mov word [rsp + 2], 0x0E7F
    fldcw word [rsp + 2]
    fld qword [rsp + 8]
    fmul qword [rsp + 16]
    fstp qword [rsp + 8]
    fldcw word [rsp]
    movsd xmm0, [rsp + 8]
    add rsp, 24
    ret

; Windows x64 ABI:
; rcx=rawX, rdx=rawY, r8=x, r9=y,
; [rsp+40]=radiusSquared, [rsp+48]=scaleArgument.
rmg_native_x87_gaussian_scale_argument:
    mov r10, [rsp + 40]
    mov r11, [rsp + 48]
    sub rsp, 24

    fnstcw word [rsp]
    mov word [rsp + 2], 0x0E7F
    fldcw word [rsp + 2]

    mov eax, ecx
    mov [rsp + 8], rax
    mov eax, edx
    mov [rsp + 16], rax

    fild qword [rsp + 8]
    fmul qword [rmg_uint32_unit]
    fadd st0, st0
    fsub qword [rmg_one]
    fstp qword [r8]

    fild qword [rsp + 16]
    fmul qword [rmg_uint32_unit]
    fadd st0, st0
    fsub qword [rmg_one]
    fst qword [r9]

    fld st0
    fmulp st1, st0
    fld qword [r8]
    fmul qword [r8]
    faddp st1, st0
    fst qword [r10]

    fld st0
    fldln2
    fxch st1
    fyl2x
    fld st0
    fchs
    fsub st0, st1
    fdiv st0, st2
    fstp qword [r11]
    fstp st0
    fstp st0

    fldcw word [rsp]
    add rsp, 24
    ret
