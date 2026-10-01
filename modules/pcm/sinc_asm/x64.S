#ifdef __APPLE__
#define LND_SYMBOL(x) _##x
#else
#define LND_SYMBOL(x) x
#endif
.intel_syntax noprefix
.text
#ifdef _WIN32
#define LND_SRC rcx
#define LND_H rdx
#define LND_OUT r8
#define LND_STEP r9d
#else
#define LND_SRC rdi
#define LND_H rsi
#define LND_OUT rdx
#define LND_STEP ecx
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1):
    movups xmm0, [LND_SRC]
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 16]
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2):
    movups xmm0, [LND_SRC + 0]
    movups xmm4, [LND_SRC + 16]
    shufps xmm0, xmm4, 0x88
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 28]
    movups xmm4, [LND_SRC + 44]
    shufps xmm1, xmm4, 0xdd
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 16]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 32]
    movss xmm5, [LND_SRC + 48]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 64]
    movss xmm4, [LND_SRC + 80]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 96]
    movss xmm5, [LND_SRC + 112]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 32]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 64]
    movss xmm5, [LND_SRC + 96]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 128]
    movss xmm4, [LND_SRC + 160]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 192]
    movss xmm5, [LND_SRC + 224]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    movss xmm0, [LND_SRC]
    movss xmm4, [LND_SRC + r10]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + r10 * 2]
    movss xmm5, [LND_SRC + rax]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    lea r11, [LND_SRC + r10 * 4]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2):
    movq xmm5, [LND_H + 0]
    unpcklps xmm5, xmm5
    movups xmm0, [LND_SRC + 0]
    mulps xmm0, xmm5
    movq xmm5, [LND_H + 16]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 32]
    mulps xmm1, xmm5
    addps xmm0, xmm1
    movq xmm5, [LND_H + 8]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 16]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 24]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 48]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm5, xmm0
    addps xmm0, xmm5
    movq [LND_OUT], xmm0
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 128]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 192]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 160]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 224]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 144]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 208]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 176]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 240]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 16], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0)
    #endif
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 2]
    lea rax, [r11 + r11]
    neg r10
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    cmp r9d, 4
    jb .Llnd_sinc_x64_sse2_frame_8_0_4_end
    .Llnd_sinc_x64_sse2_frame_8_0_4:
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_sse2_frame_8_0_4
    .Llnd_sinc_x64_sse2_frame_8_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_sse2_frame_8_0_1_end
    .Llnd_sinc_x64_sse2_frame_8_0_1:
    movss xmm0, [LND_SRC]
    mulss xmm0, [LND_H + 0]
    movss xmm1, [LND_SRC + rax]
    mulss xmm1, [LND_H + 16]
    addss xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 8]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 24]
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 4]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 20]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 12]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 28]
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_sse2_frame_8_0_1
    .Llnd_sinc_x64_sse2_frame_8_0_1_end:
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1):
    movups xmm0, [LND_SRC]
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 32]
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 16]
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 48]
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2):
    movups xmm0, [LND_SRC + 0]
    movups xmm4, [LND_SRC + 16]
    shufps xmm0, xmm4, 0x88
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 64]
    movups xmm4, [LND_SRC + 80]
    shufps xmm1, xmm4, 0x88
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 32]
    movups xmm4, [LND_SRC + 48]
    shufps xmm1, xmm4, 0x88
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 92]
    movups xmm4, [LND_SRC + 108]
    shufps xmm2, xmm4, 0xdd
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 16]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 32]
    movss xmm5, [LND_SRC + 48]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 128]
    movss xmm4, [LND_SRC + 144]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 160]
    movss xmm5, [LND_SRC + 176]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 64]
    movss xmm4, [LND_SRC + 80]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 96]
    movss xmm5, [LND_SRC + 112]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 192]
    movss xmm4, [LND_SRC + 208]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 224]
    movss xmm5, [LND_SRC + 240]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 32]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 64]
    movss xmm5, [LND_SRC + 96]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 256]
    movss xmm4, [LND_SRC + 288]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 320]
    movss xmm5, [LND_SRC + 352]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 128]
    movss xmm4, [LND_SRC + 160]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 192]
    movss xmm5, [LND_SRC + 224]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 384]
    movss xmm4, [LND_SRC + 416]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 448]
    movss xmm5, [LND_SRC + 480]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    movss xmm0, [LND_SRC]
    movss xmm4, [LND_SRC + r10]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + r10 * 2]
    movss xmm5, [LND_SRC + rax]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    lea r11, [LND_SRC + r10 * 8]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    lea r11, [LND_SRC + r10 * 4]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    lea r11, [LND_SRC + rax * 4]
    movss xmm2, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm2, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2):
    movq xmm5, [LND_H + 0]
    unpcklps xmm5, xmm5
    movups xmm0, [LND_SRC + 0]
    mulps xmm0, xmm5
    movq xmm5, [LND_H + 32]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 64]
    mulps xmm1, xmm5
    addps xmm0, xmm1
    movq xmm5, [LND_H + 16]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 32]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 48]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 96]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    addps xmm0, xmm1
    movq xmm5, [LND_H + 8]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 16]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 40]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 80]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    movq xmm5, [LND_H + 24]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 48]
    mulps xmm2, xmm5
    movq xmm5, [LND_H + 56]
    unpcklps xmm5, xmm5
    movups xmm3, [LND_SRC + 112]
    mulps xmm3, xmm5
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm5, xmm0
    addps xmm0, xmm5
    movq [LND_OUT], xmm0
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 128]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 192]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 160]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 224]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 144]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 208]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 176]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 240]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 256]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 128]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 384]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 320]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 192]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 448]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 288]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 160]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 416]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 352]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 224]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 480]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 272]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 144]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 400]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 336]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 208]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 464]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 304]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 176]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 432]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 368]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 240]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 496]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 16], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0)
    #endif
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 4]
    lea rax, [r11 + r11]
    neg r10
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    cmp r9d, 4
    jb .Llnd_sinc_x64_sse2_frame_16_0_4_end
    .Llnd_sinc_x64_sse2_frame_16_0_4:
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_sse2_frame_16_0_4
    .Llnd_sinc_x64_sse2_frame_16_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_sse2_frame_16_0_1_end
    .Llnd_sinc_x64_sse2_frame_16_0_1:
    movss xmm0, [LND_SRC]
    mulss xmm0, [LND_H + 0]
    movss xmm1, [LND_SRC + rax]
    mulss xmm1, [LND_H + 32]
    addss xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 16]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 48]
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 8]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 40]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 24]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 56]
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 4]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 36]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 20]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 52]
    addss xmm2, xmm3
    addss xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 12]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 44]
    addss xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_SRC]
    mulss xmm3, [LND_H + 28]
    movss xmm4, [LND_SRC + rax]
    mulss xmm4, [LND_H + 60]
    addss xmm3, xmm4
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_sse2_frame_16_0_1
    .Llnd_sinc_x64_sse2_frame_16_0_1_end:
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1):
    movups xmm0, [LND_SRC]
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 64]
    movups xmm4, [LND_H + 64]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 32]
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 96]
    movups xmm4, [LND_H + 96]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 16]
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 80]
    movups xmm4, [LND_H + 80]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    movups xmm2, [LND_SRC + 48]
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    movups xmm3, [LND_SRC + 112]
    movups xmm4, [LND_H + 112]
    mulps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2):
    movups xmm0, [LND_SRC + 0]
    movups xmm4, [LND_SRC + 16]
    shufps xmm0, xmm4, 0x88
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movups xmm1, [LND_SRC + 128]
    movups xmm4, [LND_SRC + 144]
    shufps xmm1, xmm4, 0x88
    movups xmm4, [LND_H + 64]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 64]
    movups xmm4, [LND_SRC + 80]
    shufps xmm1, xmm4, 0x88
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 192]
    movups xmm4, [LND_SRC + 208]
    shufps xmm2, xmm4, 0x88
    movups xmm4, [LND_H + 96]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups xmm1, [LND_SRC + 32]
    movups xmm4, [LND_SRC + 48]
    shufps xmm1, xmm4, 0x88
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movups xmm2, [LND_SRC + 160]
    movups xmm4, [LND_SRC + 176]
    shufps xmm2, xmm4, 0x88
    movups xmm4, [LND_H + 80]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    movups xmm2, [LND_SRC + 96]
    movups xmm4, [LND_SRC + 112]
    shufps xmm2, xmm4, 0x88
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    movups xmm3, [LND_SRC + 220]
    movups xmm4, [LND_SRC + 236]
    shufps xmm3, xmm4, 0xdd
    movups xmm4, [LND_H + 112]
    mulps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 16]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 32]
    movss xmm5, [LND_SRC + 48]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 256]
    movss xmm4, [LND_SRC + 272]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 288]
    movss xmm5, [LND_SRC + 304]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 64]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 128]
    movss xmm4, [LND_SRC + 144]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 160]
    movss xmm5, [LND_SRC + 176]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 384]
    movss xmm4, [LND_SRC + 400]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 416]
    movss xmm5, [LND_SRC + 432]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 96]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 64]
    movss xmm4, [LND_SRC + 80]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 96]
    movss xmm5, [LND_SRC + 112]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 320]
    movss xmm4, [LND_SRC + 336]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 352]
    movss xmm5, [LND_SRC + 368]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 80]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    movss xmm2, [LND_SRC + 192]
    movss xmm4, [LND_SRC + 208]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 224]
    movss xmm5, [LND_SRC + 240]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    movss xmm3, [LND_SRC + 448]
    movss xmm4, [LND_SRC + 464]
    unpcklps xmm3, xmm4
    movss xmm4, [LND_SRC + 480]
    movss xmm5, [LND_SRC + 496]
    unpcklps xmm4, xmm5
    movlhps xmm3, xmm4
    movups xmm4, [LND_H + 112]
    mulps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8):
    movss xmm0, [LND_SRC + 0]
    movss xmm4, [LND_SRC + 32]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + 64]
    movss xmm5, [LND_SRC + 96]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    movss xmm1, [LND_SRC + 512]
    movss xmm4, [LND_SRC + 544]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 576]
    movss xmm5, [LND_SRC + 608]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 64]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 256]
    movss xmm4, [LND_SRC + 288]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 320]
    movss xmm5, [LND_SRC + 352]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 768]
    movss xmm4, [LND_SRC + 800]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 832]
    movss xmm5, [LND_SRC + 864]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 96]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_SRC + 128]
    movss xmm4, [LND_SRC + 160]
    unpcklps xmm1, xmm4
    movss xmm4, [LND_SRC + 192]
    movss xmm5, [LND_SRC + 224]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    movss xmm2, [LND_SRC + 640]
    movss xmm4, [LND_SRC + 672]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 704]
    movss xmm5, [LND_SRC + 736]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 80]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    movss xmm2, [LND_SRC + 384]
    movss xmm4, [LND_SRC + 416]
    unpcklps xmm2, xmm4
    movss xmm4, [LND_SRC + 448]
    movss xmm5, [LND_SRC + 480]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    movss xmm3, [LND_SRC + 896]
    movss xmm4, [LND_SRC + 928]
    unpcklps xmm3, xmm4
    movss xmm4, [LND_SRC + 960]
    movss xmm5, [LND_SRC + 992]
    unpcklps xmm4, xmm5
    movlhps xmm3, xmm4
    movups xmm4, [LND_H + 112]
    mulps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    movss xmm0, [LND_SRC]
    movss xmm4, [LND_SRC + r10]
    unpcklps xmm0, xmm4
    movss xmm4, [LND_SRC + r10 * 2]
    movss xmm5, [LND_SRC + rax]
    unpcklps xmm4, xmm5
    movlhps xmm0, xmm4
    movups xmm4, [LND_H + 0]
    mulps xmm0, xmm4
    lea r11, [LND_SRC + rax * 4]
    lea r11, [r11 + r10 * 4]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 64]
    mulps xmm1, xmm4
    addps xmm0, xmm1
    lea r11, [LND_SRC + r10 * 8]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 32]
    mulps xmm1, xmm4
    lea r11, [LND_SRC + rax * 8]
    movss xmm2, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm2, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 96]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea r11, [LND_SRC + r10 * 4]
    movss xmm1, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm1, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm1, xmm4
    movups xmm4, [LND_H + 16]
    mulps xmm1, xmm4
    lea r11, [LND_SRC + rax * 4]
    lea r11, [r11 + r10 * 8]
    movss xmm2, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm2, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 80]
    mulps xmm2, xmm4
    addps xmm1, xmm2
    lea r11, [LND_SRC + rax * 4]
    movss xmm2, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm2, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm2, xmm4
    movups xmm4, [LND_H + 48]
    mulps xmm2, xmm4
    lea r11, [LND_SRC + rax * 8]
    lea r11, [r11 + r10 * 4]
    movss xmm3, [r11]
    movss xmm4, [r11 + r10]
    unpcklps xmm3, xmm4
    movss xmm4, [r11 + r10 * 2]
    movss xmm5, [r11 + rax]
    unpcklps xmm4, xmm5
    movlhps xmm3, xmm4
    movups xmm4, [LND_H + 112]
    mulps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm4, xmm0
    addps xmm0, xmm4
    movaps xmm4, xmm0
    shufps xmm4, xmm4, 0x55
    addss xmm0, xmm4
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_dot_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2):
    movq xmm5, [LND_H + 0]
    unpcklps xmm5, xmm5
    movups xmm0, [LND_SRC + 0]
    mulps xmm0, xmm5
    movq xmm5, [LND_H + 64]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 128]
    mulps xmm1, xmm5
    addps xmm0, xmm1
    movq xmm5, [LND_H + 32]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 64]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 96]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 192]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    addps xmm0, xmm1
    movq xmm5, [LND_H + 16]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 32]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 80]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 160]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    movq xmm5, [LND_H + 48]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 96]
    mulps xmm2, xmm5
    movq xmm5, [LND_H + 112]
    unpcklps xmm5, xmm5
    movups xmm3, [LND_SRC + 224]
    mulps xmm3, xmm5
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movq xmm5, [LND_H + 8]
    unpcklps xmm5, xmm5
    movups xmm1, [LND_SRC + 16]
    mulps xmm1, xmm5
    movq xmm5, [LND_H + 72]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 144]
    mulps xmm2, xmm5
    addps xmm1, xmm2
    movq xmm5, [LND_H + 40]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 80]
    mulps xmm2, xmm5
    movq xmm5, [LND_H + 104]
    unpcklps xmm5, xmm5
    movups xmm3, [LND_SRC + 208]
    mulps xmm3, xmm5
    addps xmm2, xmm3
    addps xmm1, xmm2
    movq xmm5, [LND_H + 24]
    unpcklps xmm5, xmm5
    movups xmm2, [LND_SRC + 48]
    mulps xmm2, xmm5
    movq xmm5, [LND_H + 88]
    unpcklps xmm5, xmm5
    movups xmm3, [LND_SRC + 176]
    mulps xmm3, xmm5
    addps xmm2, xmm3
    movq xmm5, [LND_H + 56]
    unpcklps xmm5, xmm5
    movups xmm3, [LND_SRC + 112]
    mulps xmm3, xmm5
    movq xmm5, [LND_H + 120]
    unpcklps xmm5, xmm5
    movups xmm4, [LND_SRC + 240]
    mulps xmm4, xmm5
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movhlps xmm5, xmm0
    addps xmm0, xmm5
    movq [LND_OUT], xmm0
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 64]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 256]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 128]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 96]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 384]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 80]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 320]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 192]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 112]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 448]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 72]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 288]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 160]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 104]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 416]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 88]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 352]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 224]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 120]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 480]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 68]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 272]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 144]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 100]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 400]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 84]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 336]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 208]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 116]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 464]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 76]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 304]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 176]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 108]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 432]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 92]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 368]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 240]
    mulps xmm4, xmm6
    movss xmm5, [LND_H + 124]
    shufps xmm5, xmm5, 0
    movups xmm6, [LND_SRC + 496]
    mulps xmm5, xmm6
    addps xmm4, xmm5
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8)
    #endif
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 64]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 512]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 256]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 96]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 768]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 128]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 80]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 640]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 384]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 112]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 896]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 64]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 72]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 576]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 320]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 104]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 832]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 192]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 88]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 704]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 448]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 120]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 960]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 32]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 68]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 544]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 288]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 100]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 800]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 160]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 84]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 672]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 416]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 116]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 928]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 96]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 76]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 608]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 352]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 108]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 864]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 224]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 92]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 736]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 480]
    mulps xmm4, xmm6
    movss xmm5, [LND_H + 124]
    shufps xmm5, xmm5, 0
    movups xmm6, [LND_SRC + 992]
    mulps xmm5, xmm6
    addps xmm4, xmm5
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 0], xmm0
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC + 16]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 64]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 528]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 272]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 96]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 784]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 144]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 80]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 656]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 400]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 112]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 912]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 80]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 72]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 592]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 336]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 104]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 848]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 208]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 88]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 720]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 464]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 120]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 976]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + 48]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 68]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 560]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 304]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 100]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 816]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 176]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 84]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 688]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 432]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 116]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 944]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + 112]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 76]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 624]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 368]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 108]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 880]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + 240]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 92]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 752]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + 496]
    mulps xmm4, xmm6
    movss xmm5, [LND_H + 124]
    shufps xmm5, xmm5, 0
    movups xmm6, [LND_SRC + 1008]
    mulps xmm5, xmm6
    addps xmm4, xmm5
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    movups [LND_OUT + 16], xmm0
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0):
    #ifdef _WIN32
    .seh_proc LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0)
    #endif
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 8]
    lea rax, [r11 + r11]
    neg r10
    #ifdef _WIN32
    sub rsp, 24
    .seh_stackalloc 24
    movaps [rsp], xmm6
    .seh_savexmm xmm6, 0
    .seh_endprologue
    #endif
    cmp r9d, 4
    jb .Llnd_sinc_x64_sse2_frame_32_0_4_end
    .Llnd_sinc_x64_sse2_frame_32_0_4:
    movss xmm0, [LND_H + 0]
    shufps xmm0, xmm0, 0
    movups xmm6, [LND_SRC]
    mulps xmm0, xmm6
    movss xmm1, [LND_H + 64]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm1, xmm6
    addps xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_H + 32]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 96]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm1, [LND_H + 16]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 80]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 48]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 112]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm1, [LND_H + 8]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 72]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 40]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 104]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm2, [LND_H + 24]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 88]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_H + 56]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 120]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_H + 4]
    shufps xmm1, xmm1, 0
    movups xmm6, [LND_SRC]
    mulps xmm1, xmm6
    movss xmm2, [LND_H + 68]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm2, xmm6
    addps xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_H + 36]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 100]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    addps xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm2, [LND_H + 20]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 84]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_H + 52]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 116]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm2, [LND_H + 12]
    shufps xmm2, xmm2, 0
    movups xmm6, [LND_SRC]
    mulps xmm2, xmm6
    movss xmm3, [LND_H + 76]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm3, xmm6
    addps xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_H + 44]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 108]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    addps xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm3, [LND_H + 28]
    shufps xmm3, xmm3, 0
    movups xmm6, [LND_SRC]
    mulps xmm3, xmm6
    movss xmm4, [LND_H + 92]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm4, xmm6
    addps xmm3, xmm4
    add LND_SRC, r11
    movss xmm4, [LND_H + 60]
    shufps xmm4, xmm4, 0
    movups xmm6, [LND_SRC]
    mulps xmm4, xmm6
    movss xmm5, [LND_H + 124]
    shufps xmm5, xmm5, 0
    movups xmm6, [LND_SRC + rax]
    mulps xmm5, xmm6
    addps xmm4, xmm5
    addps xmm3, xmm4
    addps xmm2, xmm3
    addps xmm1, xmm2
    addps xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_sse2_frame_32_0_4
    .Llnd_sinc_x64_sse2_frame_32_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_sse2_frame_32_0_1_end
    .Llnd_sinc_x64_sse2_frame_32_0_1:
    movss xmm0, [LND_SRC]
    mulss xmm0, [LND_H + 0]
    movss xmm1, [LND_SRC + rax]
    mulss xmm1, [LND_H + 64]
    addss xmm0, xmm1
    add LND_SRC, r11
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 32]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 96]
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 16]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 80]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 48]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 112]
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 8]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 72]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 40]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 104]
    addss xmm2, xmm3
    addss xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 24]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 88]
    addss xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_SRC]
    mulss xmm3, [LND_H + 56]
    movss xmm4, [LND_SRC + rax]
    mulss xmm4, [LND_H + 120]
    addss xmm3, xmm4
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    movss xmm1, [LND_SRC]
    mulss xmm1, [LND_H + 4]
    movss xmm2, [LND_SRC + rax]
    mulss xmm2, [LND_H + 68]
    addss xmm1, xmm2
    add LND_SRC, r11
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 36]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 100]
    addss xmm2, xmm3
    addss xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 20]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 84]
    addss xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_SRC]
    mulss xmm3, [LND_H + 52]
    movss xmm4, [LND_SRC + rax]
    mulss xmm4, [LND_H + 116]
    addss xmm3, xmm4
    addss xmm2, xmm3
    addss xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    movss xmm2, [LND_SRC]
    mulss xmm2, [LND_H + 12]
    movss xmm3, [LND_SRC + rax]
    mulss xmm3, [LND_H + 76]
    addss xmm2, xmm3
    add LND_SRC, r11
    movss xmm3, [LND_SRC]
    mulss xmm3, [LND_H + 44]
    movss xmm4, [LND_SRC + rax]
    mulss xmm4, [LND_H + 108]
    addss xmm3, xmm4
    addss xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    movss xmm3, [LND_SRC]
    mulss xmm3, [LND_H + 28]
    movss xmm4, [LND_SRC + rax]
    mulss xmm4, [LND_H + 92]
    addss xmm3, xmm4
    add LND_SRC, r11
    movss xmm4, [LND_SRC]
    mulss xmm4, [LND_H + 60]
    movss xmm5, [LND_SRC + rax]
    mulss xmm5, [LND_H + 124]
    addss xmm4, xmm5
    addss xmm3, xmm4
    addss xmm2, xmm3
    addss xmm1, xmm2
    addss xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    movss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_sse2_frame_32_0_1
    .Llnd_sinc_x64_sse2_frame_32_0_1_end:
    #ifdef _WIN32
    movaps xmm6, [rsp]
    add rsp, 24
    #endif
    ret
    #ifdef _WIN32
    .seh_endproc
    #endif
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0), .-LND_SYMBOL(lnd_sinc_x64_sse2_frame_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1):
    vmovups ymm0, [LND_SRC + 0]
    vmulps ymm0, ymm0, [LND_H + 0]
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_8_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2):
    vmovups xmm0, [LND_SRC + 0]
    vshufps xmm0, xmm0, [LND_SRC + 16], 0x88
    vmovups xmm3, [LND_SRC + 28]
    vshufps xmm3, xmm3, [LND_SRC + 44], 0xdd
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 16]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 32]
    vmovss xmm5, [LND_SRC + 48]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 64]
    vmovss xmm4, [LND_SRC + 80]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 96]
    vmovss xmm5, [LND_SRC + 112]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 32]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 64]
    vmovss xmm5, [LND_SRC + 96]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 128]
    vmovss xmm4, [LND_SRC + 160]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 192]
    vmovss xmm5, [LND_SRC + 224]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    vmovss xmm0, [LND_SRC]
    vmovss xmm4, [LND_SRC + r10]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + r10 * 2]
    vmovss xmm5, [LND_SRC + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    lea r11, [LND_SRC + r10 * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2):
    vmovups xmm4, [LND_H + 0]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm0, ymm5, [LND_SRC + 0]
    vmovups xmm4, [LND_H + 16]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vmovq [LND_OUT], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC + 64]
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC + 96]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC + 16]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC + 80]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC + 48]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC + 112]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vmovups [LND_OUT + 0], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC + 128]
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC + 64]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC + 192]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC + 160]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC + 96]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC + 224]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vmovups [LND_OUT + 0], ymm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 2]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx_frame_8_0_8_end
    .Llnd_sinc_x64_avx_frame_8_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx_frame_8_0_8
    .Llnd_sinc_x64_avx_frame_8_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx_frame_8_0_4_end
    .Llnd_sinc_x64_avx_frame_8_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx_frame_8_0_4
    .Llnd_sinc_x64_avx_frame_8_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx_frame_8_0_1_end
    .Llnd_sinc_x64_avx_frame_8_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 16]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 24]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 20]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 28]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx_frame_8_0_1
    .Llnd_sinc_x64_avx_frame_8_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1):
    vmovups ymm0, [LND_SRC + 0]
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovups ymm1, [LND_SRC + 32]
    vmulps ymm1, ymm1, [LND_H + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_16_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2):
    vmovups xmm0, [LND_SRC + 0]
    vshufps xmm0, xmm0, [LND_SRC + 16], 0x88
    vmovups xmm3, [LND_SRC + 32]
    vshufps xmm3, xmm3, [LND_SRC + 48], 0x88
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovups xmm1, [LND_SRC + 64]
    vshufps xmm1, xmm1, [LND_SRC + 80], 0x88
    vmovups xmm3, [LND_SRC + 92]
    vshufps xmm3, xmm3, [LND_SRC + 108], 0xdd
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 16]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 32]
    vmovss xmm5, [LND_SRC + 48]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 64]
    vmovss xmm4, [LND_SRC + 80]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 96]
    vmovss xmm5, [LND_SRC + 112]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + 128]
    vmovss xmm4, [LND_SRC + 144]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 160]
    vmovss xmm5, [LND_SRC + 176]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 192]
    vmovss xmm4, [LND_SRC + 208]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 224]
    vmovss xmm5, [LND_SRC + 240]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 32]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 64]
    vmovss xmm5, [LND_SRC + 96]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 128]
    vmovss xmm4, [LND_SRC + 160]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 192]
    vmovss xmm5, [LND_SRC + 224]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + 256]
    vmovss xmm4, [LND_SRC + 288]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 320]
    vmovss xmm5, [LND_SRC + 352]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 384]
    vmovss xmm4, [LND_SRC + 416]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 448]
    vmovss xmm5, [LND_SRC + 480]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    vmovss xmm0, [LND_SRC]
    vmovss xmm4, [LND_SRC + r10]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + r10 * 2]
    vmovss xmm5, [LND_SRC + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    lea r11, [LND_SRC + r10 * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    lea r11, [LND_SRC + r10 * 8]
    vmovss xmm1, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    lea r11, [LND_SRC + rax * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2):
    vmovups xmm4, [LND_H + 0]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm0, ymm5, [LND_SRC + 0]
    vmovups xmm4, [LND_H + 32]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 64]
    vaddps ymm0, ymm0, ymm1
    vmovups xmm4, [LND_H + 16]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 32]
    vmovups xmm4, [LND_H + 48]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm2, ymm5, [LND_SRC + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vmovq [LND_OUT], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC + 128]
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC + 64]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC + 192]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC + 160]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC + 96]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC + 224]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC + 16]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC + 144]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC + 80]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC + 208]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC + 48]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC + 176]
    vaddps xmm2, xmm2, xmm3
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC + 112]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC + 240]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vmovups [LND_OUT + 0], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC + 256]
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC + 128]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC + 384]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC + 64]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC + 320]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC + 192]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC + 448]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC + 288]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC + 160]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC + 416]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC + 96]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC + 352]
    vaddps ymm2, ymm2, ymm3
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC + 224]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC + 480]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vmovups [LND_OUT + 0], ymm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 4]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx_frame_16_0_8_end
    .Llnd_sinc_x64_avx_frame_16_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx_frame_16_0_8
    .Llnd_sinc_x64_avx_frame_16_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx_frame_16_0_4_end
    .Llnd_sinc_x64_avx_frame_16_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx_frame_16_0_4
    .Llnd_sinc_x64_avx_frame_16_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx_frame_16_0_1_end
    .Llnd_sinc_x64_avx_frame_16_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 32]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 16]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 48]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 40]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 24]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 56]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 36]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 20]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 52]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 44]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 28]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 60]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx_frame_16_0_1
    .Llnd_sinc_x64_avx_frame_16_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1):
    vmovups ymm0, [LND_SRC + 0]
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovups ymm1, [LND_SRC + 64]
    vmulps ymm1, ymm1, [LND_H + 64]
    vaddps ymm0, ymm0, ymm1
    vmovups ymm1, [LND_SRC + 32]
    vmulps ymm1, ymm1, [LND_H + 32]
    vmovups ymm2, [LND_SRC + 96]
    vmulps ymm2, ymm2, [LND_H + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_32_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2):
    vmovups xmm0, [LND_SRC + 0]
    vshufps xmm0, xmm0, [LND_SRC + 16], 0x88
    vmovups xmm3, [LND_SRC + 32]
    vshufps xmm3, xmm3, [LND_SRC + 48], 0x88
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovups xmm1, [LND_SRC + 128]
    vshufps xmm1, xmm1, [LND_SRC + 144], 0x88
    vmovups xmm3, [LND_SRC + 160]
    vshufps xmm3, xmm3, [LND_SRC + 176], 0x88
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 64]
    vaddps ymm0, ymm0, ymm1
    vmovups xmm1, [LND_SRC + 64]
    vshufps xmm1, xmm1, [LND_SRC + 80], 0x88
    vmovups xmm3, [LND_SRC + 96]
    vshufps xmm3, xmm3, [LND_SRC + 112], 0x88
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vmovups xmm2, [LND_SRC + 192]
    vshufps xmm2, xmm2, [LND_SRC + 208], 0x88
    vmovups xmm3, [LND_SRC + 220]
    vshufps xmm3, xmm3, [LND_SRC + 236], 0xdd
    vinsertf128 ymm2, ymm2, xmm3, 1
    vmulps ymm2, ymm2, [LND_H + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 16]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 32]
    vmovss xmm5, [LND_SRC + 48]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 64]
    vmovss xmm4, [LND_SRC + 80]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 96]
    vmovss xmm5, [LND_SRC + 112]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + 256]
    vmovss xmm4, [LND_SRC + 272]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 288]
    vmovss xmm5, [LND_SRC + 304]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 320]
    vmovss xmm4, [LND_SRC + 336]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 352]
    vmovss xmm5, [LND_SRC + 368]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 64]
    vaddps ymm0, ymm0, ymm1
    vmovss xmm1, [LND_SRC + 128]
    vmovss xmm4, [LND_SRC + 144]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 160]
    vmovss xmm5, [LND_SRC + 176]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 192]
    vmovss xmm4, [LND_SRC + 208]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 224]
    vmovss xmm5, [LND_SRC + 240]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vmovss xmm2, [LND_SRC + 384]
    vmovss xmm4, [LND_SRC + 400]
    vunpcklps xmm2, xmm2, xmm4
    vmovss xmm4, [LND_SRC + 416]
    vmovss xmm5, [LND_SRC + 432]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm2, xmm2, xmm4
    vmovss xmm3, [LND_SRC + 448]
    vmovss xmm4, [LND_SRC + 464]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 480]
    vmovss xmm5, [LND_SRC + 496]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm2, ymm2, xmm3, 1
    vmulps ymm2, ymm2, [LND_H + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8):
    vmovss xmm0, [LND_SRC + 0]
    vmovss xmm4, [LND_SRC + 32]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + 64]
    vmovss xmm5, [LND_SRC + 96]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    vmovss xmm3, [LND_SRC + 128]
    vmovss xmm4, [LND_SRC + 160]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 192]
    vmovss xmm5, [LND_SRC + 224]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + 512]
    vmovss xmm4, [LND_SRC + 544]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 576]
    vmovss xmm5, [LND_SRC + 608]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 640]
    vmovss xmm4, [LND_SRC + 672]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 704]
    vmovss xmm5, [LND_SRC + 736]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 64]
    vaddps ymm0, ymm0, ymm1
    vmovss xmm1, [LND_SRC + 256]
    vmovss xmm4, [LND_SRC + 288]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [LND_SRC + 320]
    vmovss xmm5, [LND_SRC + 352]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    vmovss xmm3, [LND_SRC + 384]
    vmovss xmm4, [LND_SRC + 416]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 448]
    vmovss xmm5, [LND_SRC + 480]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    vmovss xmm2, [LND_SRC + 768]
    vmovss xmm4, [LND_SRC + 800]
    vunpcklps xmm2, xmm2, xmm4
    vmovss xmm4, [LND_SRC + 832]
    vmovss xmm5, [LND_SRC + 864]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm2, xmm2, xmm4
    vmovss xmm3, [LND_SRC + 896]
    vmovss xmm4, [LND_SRC + 928]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [LND_SRC + 960]
    vmovss xmm5, [LND_SRC + 992]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm2, ymm2, xmm3, 1
    vmulps ymm2, ymm2, [LND_H + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0):
    mov r10d, LND_STEP
    shl r10, 2
    lea rax, [r10 + r10 * 2]
    vmovss xmm0, [LND_SRC]
    vmovss xmm4, [LND_SRC + r10]
    vunpcklps xmm0, xmm0, xmm4
    vmovss xmm4, [LND_SRC + r10 * 2]
    vmovss xmm5, [LND_SRC + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm0, xmm0, xmm4
    lea r11, [LND_SRC + r10 * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm0, ymm0, xmm3, 1
    vmulps ymm0, ymm0, [LND_H + 0]
    lea r11, [LND_SRC + rax * 4]
    lea r11, [r11 + r10 * 4]
    vmovss xmm1, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    lea r11, [LND_SRC + rax * 4]
    lea r11, [r11 + r10 * 8]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 64]
    vaddps ymm0, ymm0, ymm1
    lea r11, [LND_SRC + r10 * 8]
    vmovss xmm1, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm1, xmm1, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm1, xmm1, xmm4
    lea r11, [LND_SRC + rax * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm1, ymm1, xmm3, 1
    vmulps ymm1, ymm1, [LND_H + 32]
    lea r11, [LND_SRC + rax * 8]
    vmovss xmm2, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm2, xmm2, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm2, xmm2, xmm4
    lea r11, [LND_SRC + rax * 8]
    lea r11, [r11 + r10 * 4]
    vmovss xmm3, [r11]
    vmovss xmm4, [r11 + r10]
    vunpcklps xmm3, xmm3, xmm4
    vmovss xmm4, [r11 + r10 * 2]
    vmovss xmm5, [r11 + rax]
    vunpcklps xmm4, xmm4, xmm5
    vmovlhps xmm3, xmm3, xmm4
    vinsertf128 ymm2, ymm2, xmm3, 1
    vmulps ymm2, ymm2, [LND_H + 96]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0), .-LND_SYMBOL(lnd_sinc_x64_avx_dot_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2):
    vmovups xmm4, [LND_H + 0]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm0, ymm5, [LND_SRC + 0]
    vmovups xmm4, [LND_H + 64]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 128]
    vaddps ymm0, ymm0, ymm1
    vmovups xmm4, [LND_H + 32]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 64]
    vmovups xmm4, [LND_H + 96]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm2, ymm5, [LND_SRC + 192]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vmovups xmm4, [LND_H + 16]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm1, ymm5, [LND_SRC + 32]
    vmovups xmm4, [LND_H + 80]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm2, ymm5, [LND_SRC + 160]
    vaddps ymm1, ymm1, ymm2
    vmovups xmm4, [LND_H + 48]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm2, ymm5, [LND_SRC + 96]
    vmovups xmm4, [LND_H + 112]
    vunpcklps xmm5, xmm4, xmm4
    vunpckhps xmm4, xmm4, xmm4
    vinsertf128 ymm5, ymm5, xmm4, 1
    vmulps ymm3, ymm5, [LND_SRC + 224]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vmovq [LND_OUT], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps xmm1, xmm1, [LND_SRC + 256]
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC + 128]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps xmm2, xmm2, [LND_SRC + 384]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC + 64]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps xmm2, xmm2, [LND_SRC + 320]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC + 192]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps xmm3, xmm3, [LND_SRC + 448]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps xmm2, xmm2, [LND_SRC + 288]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC + 160]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps xmm3, xmm3, [LND_SRC + 416]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC + 96]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps xmm3, xmm3, [LND_SRC + 352]
    vaddps xmm2, xmm2, xmm3
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC + 224]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps xmm4, xmm4, [LND_SRC + 480]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC + 16]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps xmm2, xmm2, [LND_SRC + 272]
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC + 144]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps xmm3, xmm3, [LND_SRC + 400]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC + 80]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps xmm3, xmm3, [LND_SRC + 336]
    vaddps xmm2, xmm2, xmm3
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC + 208]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps xmm4, xmm4, [LND_SRC + 464]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC + 48]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps xmm3, xmm3, [LND_SRC + 304]
    vaddps xmm2, xmm2, xmm3
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC + 176]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps xmm4, xmm4, [LND_SRC + 432]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC + 112]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps xmm4, xmm4, [LND_SRC + 368]
    vaddps xmm3, xmm3, xmm4
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC + 240]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps xmm5, xmm5, [LND_SRC + 496]
    vaddps xmm4, xmm4, xmm5
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    vmovups [LND_OUT + 0], xmm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8):
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps ymm1, ymm1, [LND_SRC + 512]
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC + 256]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps ymm2, ymm2, [LND_SRC + 768]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC + 128]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps ymm2, ymm2, [LND_SRC + 640]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC + 384]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps ymm3, ymm3, [LND_SRC + 896]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC + 64]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps ymm2, ymm2, [LND_SRC + 576]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC + 320]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps ymm3, ymm3, [LND_SRC + 832]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC + 192]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps ymm3, ymm3, [LND_SRC + 704]
    vaddps ymm2, ymm2, ymm3
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC + 448]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps ymm4, ymm4, [LND_SRC + 960]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC + 32]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps ymm2, ymm2, [LND_SRC + 544]
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC + 288]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps ymm3, ymm3, [LND_SRC + 800]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC + 160]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps ymm3, ymm3, [LND_SRC + 672]
    vaddps ymm2, ymm2, ymm3
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC + 416]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps ymm4, ymm4, [LND_SRC + 928]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC + 96]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps ymm3, ymm3, [LND_SRC + 608]
    vaddps ymm2, ymm2, ymm3
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC + 352]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps ymm4, ymm4, [LND_SRC + 864]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC + 224]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps ymm4, ymm4, [LND_SRC + 736]
    vaddps ymm3, ymm3, ymm4
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC + 480]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps ymm5, ymm5, [LND_SRC + 992]
    vaddps ymm4, ymm4, ymm5
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    vmovups [LND_OUT + 0], ymm0
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 8]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx_frame_32_0_8_end
    .Llnd_sinc_x64_avx_frame_32_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    add LND_SRC, r11
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps ymm5, ymm5, [LND_SRC + rax]
    vaddps ymm4, ymm4, ymm5
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx_frame_32_0_8
    .Llnd_sinc_x64_avx_frame_32_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx_frame_32_0_4_end
    .Llnd_sinc_x64_avx_frame_32_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    add LND_SRC, r11
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps xmm5, xmm5, [LND_SRC + rax]
    vaddps xmm4, xmm4, xmm5
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx_frame_32_0_4
    .Llnd_sinc_x64_avx_frame_32_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx_frame_32_0_1_end
    .Llnd_sinc_x64_avx_frame_32_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 64]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 32]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 96]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 16]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 80]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 48]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 112]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 72]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 40]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 104]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 24]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 88]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 56]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 120]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 68]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 36]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 100]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 20]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 84]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 52]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 116]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 76]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 44]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 108]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 28]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 92]
    vaddss xmm3, xmm3, xmm4
    add LND_SRC, r11
    vmovss xmm4, [LND_SRC]
    vmulss xmm4, xmm4, [LND_H + 60]
    vmovss xmm5, [LND_SRC + rax]
    vmulss xmm5, xmm5, [LND_H + 124]
    vaddss xmm4, xmm4, xmm5
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx_frame_32_0_1
    .Llnd_sinc_x64_avx_frame_32_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0), .-LND_SYMBOL(lnd_sinc_x64_avx_frame_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 2]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 16
    jb .Llnd_sinc_x64_avx512_frame_8_0_16_end
    .Llnd_sinc_x64_avx512_frame_8_0_16:
    vbroadcastss zmm0, [LND_H + 0]
    vmulps zmm0, zmm0, [LND_SRC]
    vbroadcastss zmm1, [LND_H + 16]
    vmulps zmm1, zmm1, [LND_SRC + rax]
    vaddps zmm0, zmm0, zmm1
    add LND_SRC, r11
    vbroadcastss zmm1, [LND_H + 8]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 24]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss zmm1, [LND_H + 4]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 20]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 12]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 28]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], zmm0
    add LND_SRC, 64
    add LND_OUT, 64
    sub r9d, 16
    cmp r9d, 16
    jae .Llnd_sinc_x64_avx512_frame_8_0_16
    .Llnd_sinc_x64_avx512_frame_8_0_16_end:
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx512_frame_8_0_8_end
    .Llnd_sinc_x64_avx512_frame_8_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx512_frame_8_0_8
    .Llnd_sinc_x64_avx512_frame_8_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx512_frame_8_0_4_end
    .Llnd_sinc_x64_avx512_frame_8_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx512_frame_8_0_4
    .Llnd_sinc_x64_avx512_frame_8_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx512_frame_8_0_1_end
    .Llnd_sinc_x64_avx512_frame_8_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 16]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 24]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 20]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 28]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx512_frame_8_0_1
    .Llnd_sinc_x64_avx512_frame_8_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0), .-LND_SYMBOL(lnd_sinc_x64_avx512_frame_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1):
    vmovups zmm0, [LND_SRC + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2):
    vmovdqu32 zmm4, [rip + .Lindex2]
    vmovups zmm0, [LND_SRC + 0]
    vpermt2ps zmm0, zmm4, [LND_SRC + 60]
    vmulps zmm0, zmm0, [LND_H + 0]
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4):
    vmovdqu32 zmm4, [rip + .Lindex4]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4 + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8):
    vmovdqu32 zmm4, [rip + .Lindex8]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4 + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0):
    mov r10d, LND_STEP
    vpbroadcastd zmm4, r10d
    vpmulld zmm4, zmm4, [rip + .Lindex1]
    shl r10, 2
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4]
    vmulps zmm0, zmm0, [LND_H + 0]
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 4]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 16
    jb .Llnd_sinc_x64_avx512_frame_16_0_16_end
    .Llnd_sinc_x64_avx512_frame_16_0_16:
    vbroadcastss zmm0, [LND_H + 0]
    vmulps zmm0, zmm0, [LND_SRC]
    vbroadcastss zmm1, [LND_H + 32]
    vmulps zmm1, zmm1, [LND_SRC + rax]
    vaddps zmm0, zmm0, zmm1
    add LND_SRC, r11
    vbroadcastss zmm1, [LND_H + 16]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 48]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss zmm1, [LND_H + 8]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 40]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 24]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 56]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss zmm1, [LND_H + 4]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 36]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 20]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 52]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss zmm2, [LND_H + 12]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 44]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    add LND_SRC, r11
    vbroadcastss zmm3, [LND_H + 28]
    vmulps zmm3, zmm3, [LND_SRC]
    vbroadcastss zmm4, [LND_H + 60]
    vmulps zmm4, zmm4, [LND_SRC + rax]
    vaddps zmm3, zmm3, zmm4
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], zmm0
    add LND_SRC, 64
    add LND_OUT, 64
    sub r9d, 16
    cmp r9d, 16
    jae .Llnd_sinc_x64_avx512_frame_16_0_16
    .Llnd_sinc_x64_avx512_frame_16_0_16_end:
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx512_frame_16_0_8_end
    .Llnd_sinc_x64_avx512_frame_16_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx512_frame_16_0_8
    .Llnd_sinc_x64_avx512_frame_16_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx512_frame_16_0_4_end
    .Llnd_sinc_x64_avx512_frame_16_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx512_frame_16_0_4
    .Llnd_sinc_x64_avx512_frame_16_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx512_frame_16_0_1_end
    .Llnd_sinc_x64_avx512_frame_16_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 32]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 16]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 48]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 40]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 24]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 56]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 36]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 20]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 52]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 44]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 28]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 60]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx512_frame_16_0_1
    .Llnd_sinc_x64_avx512_frame_16_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0), .-LND_SYMBOL(lnd_sinc_x64_avx512_frame_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1):
    vmovups zmm0, [LND_SRC + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    vmovups zmm1, [LND_SRC + 64]
    vmulps zmm1, zmm1, [LND_H + 64]
    vaddps zmm0, zmm0, zmm1
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2):
    vmovdqu32 zmm4, [rip + .Lindex2]
    vmovups zmm0, [LND_SRC + 0]
    vpermt2ps zmm0, zmm4, [LND_SRC + 60]
    vmulps zmm0, zmm0, [LND_H + 0]
    vmovups zmm1, [LND_SRC + 128]
    vpermt2ps zmm1, zmm4, [LND_SRC + 188]
    vmulps zmm1, zmm1, [LND_H + 64]
    vaddps zmm0, zmm0, zmm1
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4):
    vmovdqu32 zmm4, [rip + .Lindex4]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4 + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm1{k1}, [LND_SRC + zmm4 * 4 + 256]
    vmulps zmm1, zmm1, [LND_H + 64]
    vaddps zmm0, zmm0, zmm1
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8):
    vmovdqu32 zmm4, [rip + .Lindex8]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4 + 0]
    vmulps zmm0, zmm0, [LND_H + 0]
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm1{k1}, [LND_SRC + zmm4 * 4 + 512]
    vmulps zmm1, zmm1, [LND_H + 64]
    vaddps zmm0, zmm0, zmm1
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0):
    mov r10d, LND_STEP
    vpbroadcastd zmm4, r10d
    vpmulld zmm4, zmm4, [rip + .Lindex1]
    shl r10, 2
    mov eax, 65535
    kmovw k1, eax
    vgatherdps zmm0{k1}, [LND_SRC + zmm4 * 4]
    vmulps zmm0, zmm0, [LND_H + 0]
    mov eax, 65535
    kmovw k1, eax
    lea r11, [LND_SRC + r10 * 8]
    lea r11, [r11 + r10 * 8]
    vgatherdps zmm1{k1}, [r11 + zmm4 * 4]
    vmulps zmm1, zmm1, [LND_H + 64]
    vaddps zmm0, zmm0, zmm1
    vextractf64x4 ymm4, zmm0, 1
    vaddps ymm0, ymm0, ymm4
    vextractf128 xmm4, ymm0, 1
    vaddps xmm0, xmm0, xmm4
    vmovhlps xmm4, xmm0, xmm0
    vaddps xmm0, xmm0, xmm4
    vshufps xmm4, xmm0, xmm0, 0x55
    vaddss xmm0, xmm0, xmm4
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0), .-LND_SYMBOL(lnd_sinc_x64_avx512_dot_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0), @function
.hidden LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0)
#endif
LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0):
    #ifdef _WIN32
    mov r9d, DWORD PTR [rsp + 40]
    #else
    mov r9d, r8d
    #endif
    mov r10d, r9d
    shl r10, 2
    lea r11, [r10 * 8]
    lea rax, [r11 + r11]
    neg r10
    cmp r9d, 16
    jb .Llnd_sinc_x64_avx512_frame_32_0_16_end
    .Llnd_sinc_x64_avx512_frame_32_0_16:
    vbroadcastss zmm0, [LND_H + 0]
    vmulps zmm0, zmm0, [LND_SRC]
    vbroadcastss zmm1, [LND_H + 64]
    vmulps zmm1, zmm1, [LND_SRC + rax]
    vaddps zmm0, zmm0, zmm1
    add LND_SRC, r11
    vbroadcastss zmm1, [LND_H + 32]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 96]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss zmm1, [LND_H + 16]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 80]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 48]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 112]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss zmm1, [LND_H + 8]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 72]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 40]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 104]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss zmm2, [LND_H + 24]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 88]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    add LND_SRC, r11
    vbroadcastss zmm3, [LND_H + 56]
    vmulps zmm3, zmm3, [LND_SRC]
    vbroadcastss zmm4, [LND_H + 120]
    vmulps zmm4, zmm4, [LND_SRC + rax]
    vaddps zmm3, zmm3, zmm4
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss zmm1, [LND_H + 4]
    vmulps zmm1, zmm1, [LND_SRC]
    vbroadcastss zmm2, [LND_H + 68]
    vmulps zmm2, zmm2, [LND_SRC + rax]
    vaddps zmm1, zmm1, zmm2
    add LND_SRC, r11
    vbroadcastss zmm2, [LND_H + 36]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 100]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss zmm2, [LND_H + 20]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 84]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    add LND_SRC, r11
    vbroadcastss zmm3, [LND_H + 52]
    vmulps zmm3, zmm3, [LND_SRC]
    vbroadcastss zmm4, [LND_H + 116]
    vmulps zmm4, zmm4, [LND_SRC + rax]
    vaddps zmm3, zmm3, zmm4
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss zmm2, [LND_H + 12]
    vmulps zmm2, zmm2, [LND_SRC]
    vbroadcastss zmm3, [LND_H + 76]
    vmulps zmm3, zmm3, [LND_SRC + rax]
    vaddps zmm2, zmm2, zmm3
    add LND_SRC, r11
    vbroadcastss zmm3, [LND_H + 44]
    vmulps zmm3, zmm3, [LND_SRC]
    vbroadcastss zmm4, [LND_H + 108]
    vmulps zmm4, zmm4, [LND_SRC + rax]
    vaddps zmm3, zmm3, zmm4
    vaddps zmm2, zmm2, zmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss zmm3, [LND_H + 28]
    vmulps zmm3, zmm3, [LND_SRC]
    vbroadcastss zmm4, [LND_H + 92]
    vmulps zmm4, zmm4, [LND_SRC + rax]
    vaddps zmm3, zmm3, zmm4
    add LND_SRC, r11
    vbroadcastss zmm4, [LND_H + 60]
    vmulps zmm4, zmm4, [LND_SRC]
    vbroadcastss zmm5, [LND_H + 124]
    vmulps zmm5, zmm5, [LND_SRC + rax]
    vaddps zmm4, zmm4, zmm5
    vaddps zmm3, zmm3, zmm4
    vaddps zmm2, zmm2, zmm3
    vaddps zmm1, zmm1, zmm2
    vaddps zmm0, zmm0, zmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], zmm0
    add LND_SRC, 64
    add LND_OUT, 64
    sub r9d, 16
    cmp r9d, 16
    jae .Llnd_sinc_x64_avx512_frame_32_0_16
    .Llnd_sinc_x64_avx512_frame_32_0_16_end:
    cmp r9d, 8
    jb .Llnd_sinc_x64_avx512_frame_32_0_8_end
    .Llnd_sinc_x64_avx512_frame_32_0_8:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps ymm0, ymm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps ymm1, ymm1, [LND_SRC + rax]
    vaddps ymm0, ymm0, ymm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 32]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 48]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 40]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 56]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps ymm1, ymm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps ymm2, ymm2, [LND_SRC + rax]
    vaddps ymm1, ymm1, ymm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 36]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 52]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps ymm2, ymm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps ymm3, ymm3, [LND_SRC + rax]
    vaddps ymm2, ymm2, ymm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 44]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps ymm3, ymm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps ymm4, ymm4, [LND_SRC + rax]
    vaddps ymm3, ymm3, ymm4
    add LND_SRC, r11
    vbroadcastss ymm4, [LND_H + 60]
    vmulps ymm4, ymm4, [LND_SRC]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps ymm5, ymm5, [LND_SRC + rax]
    vaddps ymm4, ymm4, ymm5
    vaddps ymm3, ymm3, ymm4
    vaddps ymm2, ymm2, ymm3
    vaddps ymm1, ymm1, ymm2
    vaddps ymm0, ymm0, ymm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], ymm0
    add LND_SRC, 32
    add LND_OUT, 32
    sub r9d, 8
    cmp r9d, 8
    jae .Llnd_sinc_x64_avx512_frame_32_0_8
    .Llnd_sinc_x64_avx512_frame_32_0_8_end:
    cmp r9d, 4
    jb .Llnd_sinc_x64_avx512_frame_32_0_4_end
    .Llnd_sinc_x64_avx512_frame_32_0_4:
    vbroadcastss ymm0, [LND_H + 0]
    vmulps xmm0, xmm0, [LND_SRC]
    vbroadcastss ymm1, [LND_H + 64]
    vmulps xmm1, xmm1, [LND_SRC + rax]
    vaddps xmm0, xmm0, xmm1
    add LND_SRC, r11
    vbroadcastss ymm1, [LND_H + 32]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 96]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm1, [LND_H + 16]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 80]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 48]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 112]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm1, [LND_H + 8]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 72]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 40]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 104]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 24]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 88]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 56]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 120]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vbroadcastss ymm1, [LND_H + 4]
    vmulps xmm1, xmm1, [LND_SRC]
    vbroadcastss ymm2, [LND_H + 68]
    vmulps xmm2, xmm2, [LND_SRC + rax]
    vaddps xmm1, xmm1, xmm2
    add LND_SRC, r11
    vbroadcastss ymm2, [LND_H + 36]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 100]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm2, [LND_H + 20]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 84]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 52]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 116]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vbroadcastss ymm2, [LND_H + 12]
    vmulps xmm2, xmm2, [LND_SRC]
    vbroadcastss ymm3, [LND_H + 76]
    vmulps xmm3, xmm3, [LND_SRC + rax]
    vaddps xmm2, xmm2, xmm3
    add LND_SRC, r11
    vbroadcastss ymm3, [LND_H + 44]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 108]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vbroadcastss ymm3, [LND_H + 28]
    vmulps xmm3, xmm3, [LND_SRC]
    vbroadcastss ymm4, [LND_H + 92]
    vmulps xmm4, xmm4, [LND_SRC + rax]
    vaddps xmm3, xmm3, xmm4
    add LND_SRC, r11
    vbroadcastss ymm4, [LND_H + 60]
    vmulps xmm4, xmm4, [LND_SRC]
    vbroadcastss ymm5, [LND_H + 124]
    vmulps xmm5, xmm5, [LND_SRC + rax]
    vaddps xmm4, xmm4, xmm5
    vaddps xmm3, xmm3, xmm4
    vaddps xmm2, xmm2, xmm3
    vaddps xmm1, xmm1, xmm2
    vaddps xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovups [LND_OUT], xmm0
    add LND_SRC, 16
    add LND_OUT, 16
    sub r9d, 4
    cmp r9d, 4
    jae .Llnd_sinc_x64_avx512_frame_32_0_4
    .Llnd_sinc_x64_avx512_frame_32_0_4_end:
    cmp r9d, 1
    jb .Llnd_sinc_x64_avx512_frame_32_0_1_end
    .Llnd_sinc_x64_avx512_frame_32_0_1:
    vmovss xmm0, [LND_SRC]
    vmulss xmm0, xmm0, [LND_H + 0]
    vmovss xmm1, [LND_SRC + rax]
    vmulss xmm1, xmm1, [LND_H + 64]
    vaddss xmm0, xmm0, xmm1
    add LND_SRC, r11
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 32]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 96]
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 16]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 80]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 48]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 112]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 8]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 72]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 40]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 104]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 24]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 88]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 56]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 120]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 4]
    lea LND_SRC, [LND_SRC + r10]
    vmovss xmm1, [LND_SRC]
    vmulss xmm1, xmm1, [LND_H + 4]
    vmovss xmm2, [LND_SRC + rax]
    vmulss xmm2, xmm2, [LND_H + 68]
    vaddss xmm1, xmm1, xmm2
    add LND_SRC, r11
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 36]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 100]
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 20]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 84]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 52]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 116]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    lea LND_SRC, [LND_SRC + r10 * 8]
    lea LND_SRC, [LND_SRC + r10 * 2]
    vmovss xmm2, [LND_SRC]
    vmulss xmm2, xmm2, [LND_H + 12]
    vmovss xmm3, [LND_SRC + rax]
    vmulss xmm3, xmm3, [LND_H + 76]
    vaddss xmm2, xmm2, xmm3
    add LND_SRC, r11
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 44]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 108]
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    lea LND_SRC, [LND_SRC + r10 * 4]
    vmovss xmm3, [LND_SRC]
    vmulss xmm3, xmm3, [LND_H + 28]
    vmovss xmm4, [LND_SRC + rax]
    vmulss xmm4, xmm4, [LND_H + 92]
    vaddss xmm3, xmm3, xmm4
    add LND_SRC, r11
    vmovss xmm4, [LND_SRC]
    vmulss xmm4, xmm4, [LND_H + 60]
    vmovss xmm5, [LND_SRC + rax]
    vmulss xmm5, xmm5, [LND_H + 124]
    vaddss xmm4, xmm4, xmm5
    vaddss xmm3, xmm3, xmm4
    vaddss xmm2, xmm2, xmm3
    vaddss xmm1, xmm1, xmm2
    vaddss xmm0, xmm0, xmm1
    sub LND_SRC, rax
    sub LND_SRC, r10
    vmovss [LND_OUT], xmm0
    add LND_SRC, 4
    add LND_OUT, 4
    sub r9d, 1
    cmp r9d, 1
    jae .Llnd_sinc_x64_avx512_frame_32_0_1
    .Llnd_sinc_x64_avx512_frame_32_0_1_end:
    vzeroupper
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0), .-LND_SYMBOL(lnd_sinc_x64_avx512_frame_32_0)
#endif
#ifdef _WIN32
.section .rdata,"dr"
#elif defined(__APPLE__)
.section __TEXT,__const
#else
.section .rodata
#endif
.p2align 6
.Lindex1:
.long 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
.p2align 6
.Lindex2:
.long 0, 2, 4, 6, 8, 10, 12, 14, 17, 19, 21, 23, 25, 27, 29, 31
.p2align 6
.Lindex4:
.long 0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60
.p2align 6
.Lindex8:
.long 0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 120
#if !defined(_WIN32) && !defined(__APPLE__)
.section .note.GNU-stack,"",@progbits
#endif
