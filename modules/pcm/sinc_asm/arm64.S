#ifdef __APPLE__
#define LND_SYMBOL(x) _##x
#else
#define LND_SYMBOL(x) x
#endif
.text
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_8_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_8_1), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_8_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_8_1)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_8_1):
    ldp q0, q1, [x0, #0]
    ldp q2, q3, [x1, #0]
    fmul v16.4s, v0.4s, v2.4s
    fmul v17.4s, v1.4s, v3.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_8_1), .-LND_SYMBOL(lnd_sinc_arm64_dot_8_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_8_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_8_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_8_2):
    ldp q0, q1, [x0, #0]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldur q0, [x0, #28]
    ldur q1, [x0, #44]
    uzp2 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_8_2), .-LND_SYMBOL(lnd_sinc_arm64_dot_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_8_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_8_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_8_4):
    ldr s0, [x0, #0]
    ldr s1, [x0, #16]
    ldr s2, [x0, #32]
    ldr s3, [x0, #48]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #64]
    ldr s1, [x0, #80]
    ldr s2, [x0, #96]
    ldr s3, [x0, #112]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_8_4), .-LND_SYMBOL(lnd_sinc_arm64_dot_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_8_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_8_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_8_8):
    ldr s0, [x0, #0]
    ldr s1, [x0, #32]
    ldr s2, [x0, #64]
    ldr s3, [x0, #96]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #128]
    ldr s1, [x0, #160]
    ldr s2, [x0, #192]
    ldr s3, [x0, #224]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_8_8), .-LND_SYMBOL(lnd_sinc_arm64_dot_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_8_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_8_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_8_0):
    uxtw x3, w3
    lsl x3, x3, #2
    lsl x4, x3, #1
    add x5, x4, x3
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_8_0), .-LND_SYMBOL(lnd_sinc_arm64_dot_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_2):
    ldp q0, q1, [x1]
    ldp q4, q5, [x0]
    ldp q6, q7, [x0, #32]
    zip1 v2.4s, v0.4s, v0.4s
    zip2 v0.4s, v0.4s, v0.4s
    zip1 v3.4s, v1.4s, v1.4s
    zip2 v1.4s, v1.4s, v1.4s
    fmul v2.4s, v4.4s, v2.4s
    fmul v0.4s, v0.4s, v5.4s
    fmul v3.4s, v6.4s, v3.4s
    fmul v1.4s, v1.4s, v7.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v0.4s, v2.4s, v0.4s
    ext v1.16b, v0.16b, v0.16b, #8
    fadd v0.2s, v0.2s, v1.2s
    str d0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_2), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_4):
    ldp q16, q17, [x1]
    ldp q0, q1, [x0, #0]
    ldp q2, q3, [x0, #32]
    ldp q4, q5, [x0, #64]
    ldp q6, q7, [x0, #96]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v2.4s, v2.4s, v16.s[2]
    fmul v3.4s, v3.4s, v16.s[3]
    fmul v4.4s, v4.4s, v17.s[0]
    fmul v5.4s, v5.4s, v17.s[1]
    fmul v6.4s, v6.4s, v17.s[2]
    fmul v7.4s, v7.4s, v17.s[3]
    fadd v0.4s, v0.4s, v4.4s
    fadd v1.4s, v1.4s, v5.4s
    fadd v2.4s, v2.4s, v6.4s
    fadd v3.4s, v3.4s, v7.4s
    fadd v0.4s, v0.4s, v2.4s
    fadd v1.4s, v1.4s, v3.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_4), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_8):
    ldp q16, q17, [x1, #0]
    ldp q0, q24, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #128]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #64]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #192]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #32]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #160]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #96]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #224]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_8), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_0):
    uxtw x5, w4
    lsl x5, x5, #2
    ldp q16, q17, [x1, #0]
    cmp w4, #8
    b.lo .Larm_frame_8_8_end
    .Larm_frame_8_8:
    ldp q0, q24, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    add x9, x0, x5, lsl #2
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #1
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    mov x9, #6
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #0
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    mov x9, #5
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    mov x9, #7
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    add x0, x0, #32
    add x2, x2, #32
    sub w4, w4, #8
    cmp w4, #8
    b.hs .Larm_frame_8_8
    .Larm_frame_8_8_end:
    cmp w4, #4
    b.lo .Larm_frame_8_4_end
    .Larm_frame_8_4:
    ldr q0, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    add x9, x0, x5, lsl #2
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #1
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    mov x9, #6
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #0
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    mov x9, #5
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    mov x9, #7
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    add x0, x0, #16
    add x2, x2, #16
    sub w4, w4, #4
    cmp w4, #4
    b.hs .Larm_frame_8_4
    .Larm_frame_8_4_end:
    cmp w4, #1
    b.lo .Larm_frame_8_1_end
    .Larm_frame_8_1:
    ldr s0, [x0]
    fmul s0, s0, v16.s[0]
    add x9, x0, x5, lsl #2
    ldr s1, [x9]
    fmul s1, s1, v17.s[0]
    fadd s0, s0, s1
    add x9, x0, x5, lsl #1
    ldr s1, [x9]
    fmul s1, s1, v16.s[2]
    mov x9, #6
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[2]
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #0
    ldr s1, [x9]
    fmul s1, s1, v16.s[1]
    mov x9, #5
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[1]
    fadd s1, s1, s2
    mov x9, #3
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v16.s[3]
    mov x9, #7
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v17.s[3]
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    str s0, [x2]
    add x0, x0, #4
    add x2, x2, #4
    sub w4, w4, #1
    cmp w4, #1
    b.hs .Larm_frame_8_1
    .Larm_frame_8_1_end:
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_0), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_16_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_16_1), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_16_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_16_1)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_16_1):
    ldp q0, q1, [x0, #0]
    ldp q2, q3, [x1, #0]
    fmul v16.4s, v0.4s, v2.4s
    fmul v17.4s, v1.4s, v3.4s
    ldp q0, q1, [x0, #32]
    ldp q2, q3, [x1, #32]
    fmul v18.4s, v0.4s, v2.4s
    fmul v19.4s, v1.4s, v3.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_16_1), .-LND_SYMBOL(lnd_sinc_arm64_dot_16_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_16_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_16_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_16_2):
    ldp q0, q1, [x0, #0]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #32]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #64]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldur q0, [x0, #92]
    ldur q1, [x0, #108]
    uzp2 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_16_2), .-LND_SYMBOL(lnd_sinc_arm64_dot_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_16_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_16_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_16_4):
    ldr s0, [x0, #0]
    ldr s1, [x0, #16]
    ldr s2, [x0, #32]
    ldr s3, [x0, #48]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #64]
    ldr s1, [x0, #80]
    ldr s2, [x0, #96]
    ldr s3, [x0, #112]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0, #128]
    ldr s1, [x0, #144]
    ldr s2, [x0, #160]
    ldr s3, [x0, #176]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0, #192]
    ldr s1, [x0, #208]
    ldr s2, [x0, #224]
    ldr s3, [x0, #240]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_16_4), .-LND_SYMBOL(lnd_sinc_arm64_dot_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_16_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_16_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_16_8):
    ldr s0, [x0, #0]
    ldr s1, [x0, #32]
    ldr s2, [x0, #64]
    ldr s3, [x0, #96]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #128]
    ldr s1, [x0, #160]
    ldr s2, [x0, #192]
    ldr s3, [x0, #224]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0, #256]
    ldr s1, [x0, #288]
    ldr s2, [x0, #320]
    ldr s3, [x0, #352]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0, #384]
    ldr s1, [x0, #416]
    ldr s2, [x0, #448]
    ldr s3, [x0, #480]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_16_8), .-LND_SYMBOL(lnd_sinc_arm64_dot_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_16_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_16_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_16_0):
    uxtw x3, w3
    lsl x3, x3, #2
    lsl x4, x3, #1
    add x5, x4, x3
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_16_0), .-LND_SYMBOL(lnd_sinc_arm64_dot_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_16_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_16_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_16_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_16_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_16_2):
    ldp q16, q17, [x1]
    ldp q18, q19, [x1, #32]
    ldp d0, d1, [x0, #0]
    ldp d2, d3, [x0, #16]
    ldp d4, d5, [x0, #32]
    ldp d6, d7, [x0, #48]
    ldp d24, d25, [x0, #64]
    ldp d26, d27, [x0, #80]
    ldp d28, d29, [x0, #96]
    ldp d30, d31, [x0, #112]
    fmul v0.2s, v0.2s, v16.s[0]
    fmul v1.2s, v1.2s, v16.s[1]
    fmul v2.2s, v2.2s, v16.s[2]
    fmul v3.2s, v3.2s, v16.s[3]
    fmul v4.2s, v4.2s, v17.s[0]
    fmul v5.2s, v5.2s, v17.s[1]
    fmul v6.2s, v6.2s, v17.s[2]
    fmul v7.2s, v7.2s, v17.s[3]
    fmul v24.2s, v24.2s, v18.s[0]
    fmul v25.2s, v25.2s, v18.s[1]
    fmul v26.2s, v26.2s, v18.s[2]
    fmul v27.2s, v27.2s, v18.s[3]
    fmul v28.2s, v28.2s, v19.s[0]
    fmul v29.2s, v29.2s, v19.s[1]
    fmul v30.2s, v30.2s, v19.s[2]
    fmul v31.2s, v31.2s, v19.s[3]
    fadd v0.2s, v0.2s, v24.2s
    fadd v1.2s, v1.2s, v25.2s
    fadd v2.2s, v2.2s, v26.2s
    fadd v3.2s, v3.2s, v27.2s
    fadd v4.2s, v4.2s, v28.2s
    fadd v5.2s, v5.2s, v29.2s
    fadd v6.2s, v6.2s, v30.2s
    fadd v7.2s, v7.2s, v31.2s
    fadd v0.2s, v0.2s, v4.2s
    fadd v1.2s, v1.2s, v5.2s
    fadd v2.2s, v2.2s, v6.2s
    fadd v3.2s, v3.2s, v7.2s
    fadd v0.2s, v0.2s, v2.2s
    fadd v1.2s, v1.2s, v3.2s
    fadd v0.2s, v0.2s, v1.2s
    str d0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_16_2), .-LND_SYMBOL(lnd_sinc_arm64_frame_16_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_16_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_16_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_16_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_16_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_16_4):
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    ldr q0, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    ldr q1, [x0, #128]
    fmul v1.4s, v1.4s, v18.s[0]
    fadd v0.4s, v0.4s, v1.4s
    ldr q1, [x0, #64]
    fmul v1.4s, v1.4s, v17.s[0]
    ldr q2, [x0, #192]
    fmul v2.4s, v2.4s, v19.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    ldr q1, [x0, #32]
    fmul v1.4s, v1.4s, v16.s[2]
    ldr q2, [x0, #160]
    fmul v2.4s, v2.4s, v18.s[2]
    fadd v1.4s, v1.4s, v2.4s
    ldr q2, [x0, #96]
    fmul v2.4s, v2.4s, v17.s[2]
    ldr q3, [x0, #224]
    fmul v3.4s, v3.4s, v19.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    ldr q1, [x0, #16]
    fmul v1.4s, v1.4s, v16.s[1]
    ldr q2, [x0, #144]
    fmul v2.4s, v2.4s, v18.s[1]
    fadd v1.4s, v1.4s, v2.4s
    ldr q2, [x0, #80]
    fmul v2.4s, v2.4s, v17.s[1]
    ldr q3, [x0, #208]
    fmul v3.4s, v3.4s, v19.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    ldr q2, [x0, #48]
    fmul v2.4s, v2.4s, v16.s[3]
    ldr q3, [x0, #176]
    fmul v3.4s, v3.4s, v18.s[3]
    fadd v2.4s, v2.4s, v3.4s
    ldr q3, [x0, #112]
    fmul v3.4s, v3.4s, v17.s[3]
    ldr q4, [x0, #240]
    fmul v4.4s, v4.4s, v19.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_16_4), .-LND_SYMBOL(lnd_sinc_arm64_frame_16_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_16_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_16_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_16_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_16_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_16_8):
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    ldp q0, q24, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #256]
    fmul v1.4s, v1.4s, v18.s[0]
    fmul v25.4s, v25.4s, v18.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #128]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    ldp q2, q26, [x0, #384]
    fmul v2.4s, v2.4s, v19.s[0]
    fmul v26.4s, v26.4s, v19.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #64]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #320]
    fmul v2.4s, v2.4s, v18.s[2]
    fmul v26.4s, v26.4s, v18.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #192]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    ldp q3, q27, [x0, #448]
    fmul v3.4s, v3.4s, v19.s[2]
    fmul v27.4s, v27.4s, v19.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #32]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #288]
    fmul v2.4s, v2.4s, v18.s[1]
    fmul v26.4s, v26.4s, v18.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #160]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    ldp q3, q27, [x0, #416]
    fmul v3.4s, v3.4s, v19.s[1]
    fmul v27.4s, v27.4s, v19.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #96]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #352]
    fmul v3.4s, v3.4s, v18.s[3]
    fmul v27.4s, v27.4s, v18.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    ldp q3, q27, [x0, #224]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    ldp q4, q28, [x0, #480]
    fmul v4.4s, v4.4s, v19.s[3]
    fmul v28.4s, v28.4s, v19.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_16_8), .-LND_SYMBOL(lnd_sinc_arm64_frame_16_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_16_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_16_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_16_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_16_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_16_0):
    uxtw x5, w4
    lsl x5, x5, #2
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    cmp w4, #8
    b.lo .Larm_frame_16_8_end
    .Larm_frame_16_8:
    ldp q0, q24, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    add x9, x0, x5, lsl #3
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v18.s[0]
    fmul v25.4s, v25.4s, v18.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #2
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    mov x9, #12
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v19.s[0]
    fmul v26.4s, v26.4s, v19.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #1
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    mov x9, #10
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v18.s[2]
    fmul v26.4s, v26.4s, v18.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #6
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    mov x9, #14
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v19.s[2]
    fmul v27.4s, v27.4s, v19.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #0
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    mov x9, #9
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v18.s[1]
    fmul v26.4s, v26.4s, v18.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #5
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    mov x9, #13
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v19.s[1]
    fmul v27.4s, v27.4s, v19.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    mov x9, #11
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v18.s[3]
    fmul v27.4s, v27.4s, v18.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    mov x9, #7
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    mov x9, #15
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v19.s[3]
    fmul v28.4s, v28.4s, v19.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    add x0, x0, #32
    add x2, x2, #32
    sub w4, w4, #8
    cmp w4, #8
    b.hs .Larm_frame_16_8
    .Larm_frame_16_8_end:
    cmp w4, #4
    b.lo .Larm_frame_16_4_end
    .Larm_frame_16_4:
    ldr q0, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    add x9, x0, x5, lsl #3
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v18.s[0]
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #2
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    mov x9, #12
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v19.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #1
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    mov x9, #10
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v18.s[2]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #6
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    mov x9, #14
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v19.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #0
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    mov x9, #9
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v18.s[1]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #5
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    mov x9, #13
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v19.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    mov x9, #11
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v18.s[3]
    fadd v2.4s, v2.4s, v3.4s
    mov x9, #7
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    mov x9, #15
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v19.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    add x0, x0, #16
    add x2, x2, #16
    sub w4, w4, #4
    cmp w4, #4
    b.hs .Larm_frame_16_4
    .Larm_frame_16_4_end:
    cmp w4, #1
    b.lo .Larm_frame_16_1_end
    .Larm_frame_16_1:
    ldr s0, [x0]
    fmul s0, s0, v16.s[0]
    add x9, x0, x5, lsl #3
    ldr s1, [x9]
    fmul s1, s1, v18.s[0]
    fadd s0, s0, s1
    add x9, x0, x5, lsl #2
    ldr s1, [x9]
    fmul s1, s1, v17.s[0]
    mov x9, #12
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v19.s[0]
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #1
    ldr s1, [x9]
    fmul s1, s1, v16.s[2]
    mov x9, #10
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v18.s[2]
    fadd s1, s1, s2
    mov x9, #6
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[2]
    mov x9, #14
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v19.s[2]
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #0
    ldr s1, [x9]
    fmul s1, s1, v16.s[1]
    mov x9, #9
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v18.s[1]
    fadd s1, s1, s2
    mov x9, #5
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[1]
    mov x9, #13
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v19.s[1]
    fadd s2, s2, s3
    fadd s1, s1, s2
    mov x9, #3
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v16.s[3]
    mov x9, #11
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v18.s[3]
    fadd s2, s2, s3
    mov x9, #7
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v17.s[3]
    mov x9, #15
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v19.s[3]
    fadd s3, s3, s4
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    str s0, [x2]
    add x0, x0, #4
    add x2, x2, #4
    sub w4, w4, #1
    cmp w4, #1
    b.hs .Larm_frame_16_1
    .Larm_frame_16_1_end:
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_16_0), .-LND_SYMBOL(lnd_sinc_arm64_frame_16_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_32_1)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_32_1), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_32_1)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_32_1)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_32_1):
    ldp q0, q1, [x0, #0]
    ldp q2, q3, [x1, #0]
    fmul v16.4s, v0.4s, v2.4s
    fmul v17.4s, v1.4s, v3.4s
    ldp q0, q1, [x0, #32]
    ldp q2, q3, [x1, #32]
    fmul v18.4s, v0.4s, v2.4s
    fmul v19.4s, v1.4s, v3.4s
    ldp q0, q1, [x0, #64]
    ldp q2, q3, [x1, #64]
    fmul v20.4s, v0.4s, v2.4s
    fmul v21.4s, v1.4s, v3.4s
    ldp q0, q1, [x0, #96]
    ldp q2, q3, [x1, #96]
    fmul v22.4s, v0.4s, v2.4s
    fmul v23.4s, v1.4s, v3.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_32_1), .-LND_SYMBOL(lnd_sinc_arm64_dot_32_1)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_32_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_32_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_32_2):
    ldp q0, q1, [x0, #0]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #32]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #64]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #96]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #128]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #64]
    fmul v20.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #160]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #80]
    fmul v21.4s, v0.4s, v4.4s
    ldp q0, q1, [x0, #192]
    uzp1 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #96]
    fmul v22.4s, v0.4s, v4.4s
    ldur q0, [x0, #220]
    ldur q1, [x0, #236]
    uzp2 v0.4s, v0.4s, v1.4s
    ldr q4, [x1, #112]
    fmul v23.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_32_2), .-LND_SYMBOL(lnd_sinc_arm64_dot_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_32_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_32_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_32_4):
    ldr s0, [x0, #0]
    ldr s1, [x0, #16]
    ldr s2, [x0, #32]
    ldr s3, [x0, #48]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #64]
    ldr s1, [x0, #80]
    ldr s2, [x0, #96]
    ldr s3, [x0, #112]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0, #128]
    ldr s1, [x0, #144]
    ldr s2, [x0, #160]
    ldr s3, [x0, #176]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0, #192]
    ldr s1, [x0, #208]
    ldr s2, [x0, #224]
    ldr s3, [x0, #240]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    ldr s0, [x0, #256]
    ldr s1, [x0, #272]
    ldr s2, [x0, #288]
    ldr s3, [x0, #304]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #64]
    fmul v20.4s, v0.4s, v4.4s
    ldr s0, [x0, #320]
    ldr s1, [x0, #336]
    ldr s2, [x0, #352]
    ldr s3, [x0, #368]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #80]
    fmul v21.4s, v0.4s, v4.4s
    ldr s0, [x0, #384]
    ldr s1, [x0, #400]
    ldr s2, [x0, #416]
    ldr s3, [x0, #432]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #96]
    fmul v22.4s, v0.4s, v4.4s
    ldr s0, [x0, #448]
    ldr s1, [x0, #464]
    ldr s2, [x0, #480]
    ldr s3, [x0, #496]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #112]
    fmul v23.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_32_4), .-LND_SYMBOL(lnd_sinc_arm64_dot_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_32_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_32_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_32_8):
    ldr s0, [x0, #0]
    ldr s1, [x0, #32]
    ldr s2, [x0, #64]
    ldr s3, [x0, #96]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0, #128]
    ldr s1, [x0, #160]
    ldr s2, [x0, #192]
    ldr s3, [x0, #224]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0, #256]
    ldr s1, [x0, #288]
    ldr s2, [x0, #320]
    ldr s3, [x0, #352]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0, #384]
    ldr s1, [x0, #416]
    ldr s2, [x0, #448]
    ldr s3, [x0, #480]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    ldr s0, [x0, #512]
    ldr s1, [x0, #544]
    ldr s2, [x0, #576]
    ldr s3, [x0, #608]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #64]
    fmul v20.4s, v0.4s, v4.4s
    ldr s0, [x0, #640]
    ldr s1, [x0, #672]
    ldr s2, [x0, #704]
    ldr s3, [x0, #736]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #80]
    fmul v21.4s, v0.4s, v4.4s
    ldr s0, [x0, #768]
    ldr s1, [x0, #800]
    ldr s2, [x0, #832]
    ldr s3, [x0, #864]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #96]
    fmul v22.4s, v0.4s, v4.4s
    ldr s0, [x0, #896]
    ldr s1, [x0, #928]
    ldr s2, [x0, #960]
    ldr s3, [x0, #992]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #112]
    fmul v23.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_32_8), .-LND_SYMBOL(lnd_sinc_arm64_dot_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_dot_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_dot_32_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_dot_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_dot_32_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_dot_32_0):
    uxtw x3, w3
    lsl x3, x3, #2
    lsl x4, x3, #1
    add x5, x4, x3
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #0]
    fmul v16.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #16]
    fmul v17.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #32]
    fmul v18.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #48]
    fmul v19.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #64]
    fmul v20.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #80]
    fmul v21.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    add x0, x0, x3, lsl #2
    ldr q4, [x1, #96]
    fmul v22.4s, v0.4s, v4.4s
    ldr s0, [x0]
    ldr s1, [x0, x3]
    ldr s2, [x0, x4]
    ldr s3, [x0, x5]
    zip1 v0.2s, v0.2s, v1.2s
    zip1 v2.2s, v2.2s, v3.2s
    zip1 v0.2d, v0.2d, v2.2d
    ldr q4, [x1, #112]
    fmul v23.4s, v0.4s, v4.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    dup v1.2d, v16.d[1]
    fadd v0.2s, v16.2s, v1.2s
    faddp s0, v0.2s
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_dot_32_0), .-LND_SYMBOL(lnd_sinc_arm64_dot_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_32_2)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_32_2), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_32_2)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_32_2)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_32_2):
    ldp q0, q1, [x1, #0]
    ldp q4, q5, [x0, #0]
    ldp q6, q7, [x0, #32]
    zip1 v2.4s, v0.4s, v0.4s
    zip2 v0.4s, v0.4s, v0.4s
    zip1 v3.4s, v1.4s, v1.4s
    zip2 v1.4s, v1.4s, v1.4s
    fmul v16.4s, v4.4s, v2.4s
    fmul v17.4s, v5.4s, v0.4s
    fmul v18.4s, v6.4s, v3.4s
    fmul v19.4s, v7.4s, v1.4s
    ldp q0, q1, [x1, #32]
    ldp q4, q5, [x0, #64]
    ldp q6, q7, [x0, #96]
    zip1 v2.4s, v0.4s, v0.4s
    zip2 v0.4s, v0.4s, v0.4s
    zip1 v3.4s, v1.4s, v1.4s
    zip2 v1.4s, v1.4s, v1.4s
    fmul v20.4s, v4.4s, v2.4s
    fmul v21.4s, v5.4s, v0.4s
    fmul v22.4s, v6.4s, v3.4s
    fmul v23.4s, v7.4s, v1.4s
    ldp q0, q1, [x1, #64]
    ldp q4, q5, [x0, #128]
    ldp q6, q7, [x0, #160]
    zip1 v2.4s, v0.4s, v0.4s
    zip2 v0.4s, v0.4s, v0.4s
    zip1 v3.4s, v1.4s, v1.4s
    zip2 v1.4s, v1.4s, v1.4s
    fmul v24.4s, v4.4s, v2.4s
    fmul v25.4s, v5.4s, v0.4s
    fmul v26.4s, v6.4s, v3.4s
    fmul v27.4s, v7.4s, v1.4s
    ldp q0, q1, [x1, #96]
    ldp q4, q5, [x0, #192]
    ldp q6, q7, [x0, #224]
    zip1 v2.4s, v0.4s, v0.4s
    zip2 v0.4s, v0.4s, v0.4s
    zip1 v3.4s, v1.4s, v1.4s
    zip2 v1.4s, v1.4s, v1.4s
    fmul v28.4s, v4.4s, v2.4s
    fmul v29.4s, v5.4s, v0.4s
    fmul v30.4s, v6.4s, v3.4s
    fmul v31.4s, v7.4s, v1.4s
    fadd v16.4s, v16.4s, v24.4s
    fadd v17.4s, v17.4s, v25.4s
    fadd v18.4s, v18.4s, v26.4s
    fadd v19.4s, v19.4s, v27.4s
    fadd v20.4s, v20.4s, v28.4s
    fadd v21.4s, v21.4s, v29.4s
    fadd v22.4s, v22.4s, v30.4s
    fadd v23.4s, v23.4s, v31.4s
    fadd v16.4s, v16.4s, v20.4s
    fadd v17.4s, v17.4s, v21.4s
    fadd v18.4s, v18.4s, v22.4s
    fadd v19.4s, v19.4s, v23.4s
    fadd v16.4s, v16.4s, v18.4s
    fadd v17.4s, v17.4s, v19.4s
    fadd v16.4s, v16.4s, v17.4s
    ext v1.16b, v16.16b, v16.16b, #8
    fadd v0.2s, v16.2s, v1.2s
    str d0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_32_2), .-LND_SYMBOL(lnd_sinc_arm64_frame_32_2)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_32_4)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_32_4), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_32_4)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_32_4)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_32_4):
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    ldp q20, q21, [x1, #64]
    ldp q22, q23, [x1, #96]
    ldp q24, q25, [x0, #0]
    ldp q26, q27, [x0, #128]
    ldp q28, q29, [x0, #256]
    ldp q30, q31, [x0, #384]
    fmul v24.4s, v24.4s, v16.s[0]
    fmul v25.4s, v25.4s, v16.s[1]
    fmul v26.4s, v26.4s, v18.s[0]
    fmul v27.4s, v27.4s, v18.s[1]
    fmul v28.4s, v28.4s, v20.s[0]
    fmul v29.4s, v29.4s, v20.s[1]
    fmul v30.4s, v30.4s, v22.s[0]
    fmul v31.4s, v31.4s, v22.s[1]
    fadd v24.4s, v24.4s, v28.4s
    fadd v26.4s, v26.4s, v30.4s
    fadd v0.4s, v24.4s, v26.4s
    fadd v25.4s, v25.4s, v29.4s
    fadd v27.4s, v27.4s, v31.4s
    fadd v1.4s, v25.4s, v27.4s
    ldp q24, q25, [x0, #32]
    ldp q26, q27, [x0, #160]
    ldp q28, q29, [x0, #288]
    ldp q30, q31, [x0, #416]
    fmul v24.4s, v24.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[3]
    fmul v26.4s, v26.4s, v18.s[2]
    fmul v27.4s, v27.4s, v18.s[3]
    fmul v28.4s, v28.4s, v20.s[2]
    fmul v29.4s, v29.4s, v20.s[3]
    fmul v30.4s, v30.4s, v22.s[2]
    fmul v31.4s, v31.4s, v22.s[3]
    fadd v24.4s, v24.4s, v28.4s
    fadd v26.4s, v26.4s, v30.4s
    fadd v2.4s, v24.4s, v26.4s
    fadd v25.4s, v25.4s, v29.4s
    fadd v27.4s, v27.4s, v31.4s
    fadd v3.4s, v25.4s, v27.4s
    ldp q24, q25, [x0, #64]
    ldp q26, q27, [x0, #192]
    ldp q28, q29, [x0, #320]
    ldp q30, q31, [x0, #448]
    fmul v24.4s, v24.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[1]
    fmul v26.4s, v26.4s, v19.s[0]
    fmul v27.4s, v27.4s, v19.s[1]
    fmul v28.4s, v28.4s, v21.s[0]
    fmul v29.4s, v29.4s, v21.s[1]
    fmul v30.4s, v30.4s, v23.s[0]
    fmul v31.4s, v31.4s, v23.s[1]
    fadd v24.4s, v24.4s, v28.4s
    fadd v26.4s, v26.4s, v30.4s
    fadd v4.4s, v24.4s, v26.4s
    fadd v25.4s, v25.4s, v29.4s
    fadd v27.4s, v27.4s, v31.4s
    fadd v5.4s, v25.4s, v27.4s
    ldp q24, q25, [x0, #96]
    ldp q26, q27, [x0, #224]
    ldp q28, q29, [x0, #352]
    ldp q30, q31, [x0, #480]
    fmul v24.4s, v24.4s, v17.s[2]
    fmul v25.4s, v25.4s, v17.s[3]
    fmul v26.4s, v26.4s, v19.s[2]
    fmul v27.4s, v27.4s, v19.s[3]
    fmul v28.4s, v28.4s, v21.s[2]
    fmul v29.4s, v29.4s, v21.s[3]
    fmul v30.4s, v30.4s, v23.s[2]
    fmul v31.4s, v31.4s, v23.s[3]
    fadd v24.4s, v24.4s, v28.4s
    fadd v26.4s, v26.4s, v30.4s
    fadd v6.4s, v24.4s, v26.4s
    fadd v25.4s, v25.4s, v29.4s
    fadd v27.4s, v27.4s, v31.4s
    fadd v7.4s, v25.4s, v27.4s
    fadd v0.4s, v0.4s, v4.4s
    fadd v1.4s, v1.4s, v5.4s
    fadd v2.4s, v2.4s, v6.4s
    fadd v3.4s, v3.4s, v7.4s
    fadd v0.4s, v0.4s, v2.4s
    fadd v1.4s, v1.4s, v3.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_32_4), .-LND_SYMBOL(lnd_sinc_arm64_frame_32_4)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_32_8)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_32_8), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_32_8)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_32_8)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_32_8):
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    ldp q20, q21, [x1, #64]
    ldp q22, q23, [x1, #96]
    ldp q0, q24, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #512]
    fmul v1.4s, v1.4s, v20.s[0]
    fmul v25.4s, v25.4s, v20.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #256]
    fmul v1.4s, v1.4s, v18.s[0]
    fmul v25.4s, v25.4s, v18.s[0]
    ldp q2, q26, [x0, #768]
    fmul v2.4s, v2.4s, v22.s[0]
    fmul v26.4s, v26.4s, v22.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #128]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    ldp q2, q26, [x0, #640]
    fmul v2.4s, v2.4s, v21.s[0]
    fmul v26.4s, v26.4s, v21.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #384]
    fmul v2.4s, v2.4s, v19.s[0]
    fmul v26.4s, v26.4s, v19.s[0]
    ldp q3, q27, [x0, #896]
    fmul v3.4s, v3.4s, v23.s[0]
    fmul v27.4s, v27.4s, v23.s[0]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #64]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #576]
    fmul v2.4s, v2.4s, v20.s[2]
    fmul v26.4s, v26.4s, v20.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #320]
    fmul v2.4s, v2.4s, v18.s[2]
    fmul v26.4s, v26.4s, v18.s[2]
    ldp q3, q27, [x0, #832]
    fmul v3.4s, v3.4s, v22.s[2]
    fmul v27.4s, v27.4s, v22.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #192]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    ldp q3, q27, [x0, #704]
    fmul v3.4s, v3.4s, v21.s[2]
    fmul v27.4s, v27.4s, v21.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    ldp q3, q27, [x0, #448]
    fmul v3.4s, v3.4s, v19.s[2]
    fmul v27.4s, v27.4s, v19.s[2]
    ldp q4, q28, [x0, #960]
    fmul v4.4s, v4.4s, v23.s[2]
    fmul v28.4s, v28.4s, v23.s[2]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #32]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #544]
    fmul v2.4s, v2.4s, v20.s[1]
    fmul v26.4s, v26.4s, v20.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #288]
    fmul v2.4s, v2.4s, v18.s[1]
    fmul v26.4s, v26.4s, v18.s[1]
    ldp q3, q27, [x0, #800]
    fmul v3.4s, v3.4s, v22.s[1]
    fmul v27.4s, v27.4s, v22.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #160]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    ldp q3, q27, [x0, #672]
    fmul v3.4s, v3.4s, v21.s[1]
    fmul v27.4s, v27.4s, v21.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    ldp q3, q27, [x0, #416]
    fmul v3.4s, v3.4s, v19.s[1]
    fmul v27.4s, v27.4s, v19.s[1]
    ldp q4, q28, [x0, #928]
    fmul v4.4s, v4.4s, v23.s[1]
    fmul v28.4s, v28.4s, v23.s[1]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #96]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #608]
    fmul v3.4s, v3.4s, v20.s[3]
    fmul v27.4s, v27.4s, v20.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    ldp q3, q27, [x0, #352]
    fmul v3.4s, v3.4s, v18.s[3]
    fmul v27.4s, v27.4s, v18.s[3]
    ldp q4, q28, [x0, #864]
    fmul v4.4s, v4.4s, v22.s[3]
    fmul v28.4s, v28.4s, v22.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    ldp q3, q27, [x0, #224]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    ldp q4, q28, [x0, #736]
    fmul v4.4s, v4.4s, v21.s[3]
    fmul v28.4s, v28.4s, v21.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    ldp q4, q28, [x0, #480]
    fmul v4.4s, v4.4s, v19.s[3]
    fmul v28.4s, v28.4s, v19.s[3]
    ldp q5, q29, [x0, #992]
    fmul v5.4s, v5.4s, v23.s[3]
    fmul v29.4s, v29.4s, v23.s[3]
    fadd v4.4s, v4.4s, v5.4s
    fadd v28.4s, v28.4s, v29.4s
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_32_8), .-LND_SYMBOL(lnd_sinc_arm64_frame_32_8)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_32_0)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_32_0), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_32_0)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_32_0)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_32_0):
    uxtw x5, w4
    lsl x5, x5, #2
    ldp q16, q17, [x1, #0]
    ldp q18, q19, [x1, #32]
    ldp q20, q21, [x1, #64]
    ldp q22, q23, [x1, #96]
    cmp w4, #8
    b.lo .Larm_frame_32_8_end
    .Larm_frame_32_8:
    ldp q0, q24, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    add x9, x0, x5, lsl #4
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v20.s[0]
    fmul v25.4s, v25.4s, v20.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #3
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v18.s[0]
    fmul v25.4s, v25.4s, v18.s[0]
    mov x9, #24
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v22.s[0]
    fmul v26.4s, v26.4s, v22.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #2
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    mov x9, #20
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v21.s[0]
    fmul v26.4s, v26.4s, v21.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #12
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v19.s[0]
    fmul v26.4s, v26.4s, v19.s[0]
    mov x9, #28
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v23.s[0]
    fmul v27.4s, v27.4s, v23.s[0]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #1
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    mov x9, #18
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v20.s[2]
    fmul v26.4s, v26.4s, v20.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #10
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v18.s[2]
    fmul v26.4s, v26.4s, v18.s[2]
    mov x9, #26
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v22.s[2]
    fmul v27.4s, v27.4s, v22.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #6
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    mov x9, #22
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v21.s[2]
    fmul v27.4s, v27.4s, v21.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    mov x9, #14
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v19.s[2]
    fmul v27.4s, v27.4s, v19.s[2]
    mov x9, #30
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v23.s[2]
    fmul v28.4s, v28.4s, v23.s[2]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    add x9, x0, x5, lsl #0
    ldp q1, q25, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    mov x9, #17
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v20.s[1]
    fmul v26.4s, v26.4s, v20.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #9
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v18.s[1]
    fmul v26.4s, v26.4s, v18.s[1]
    mov x9, #25
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v22.s[1]
    fmul v27.4s, v27.4s, v22.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #5
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    mov x9, #21
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v21.s[1]
    fmul v27.4s, v27.4s, v21.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    mov x9, #13
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v19.s[1]
    fmul v27.4s, v27.4s, v19.s[1]
    mov x9, #29
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v23.s[1]
    fmul v28.4s, v28.4s, v23.s[1]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldp q2, q26, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    mov x9, #19
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v20.s[3]
    fmul v27.4s, v27.4s, v20.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    mov x9, #11
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v18.s[3]
    fmul v27.4s, v27.4s, v18.s[3]
    mov x9, #27
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v22.s[3]
    fmul v28.4s, v28.4s, v22.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    mov x9, #7
    madd x9, x5, x9, x0
    ldp q3, q27, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    mov x9, #23
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v21.s[3]
    fmul v28.4s, v28.4s, v21.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    mov x9, #15
    madd x9, x5, x9, x0
    ldp q4, q28, [x9]
    fmul v4.4s, v4.4s, v19.s[3]
    fmul v28.4s, v28.4s, v19.s[3]
    mov x9, #31
    madd x9, x5, x9, x0
    ldp q5, q29, [x9]
    fmul v5.4s, v5.4s, v23.s[3]
    fmul v29.4s, v29.4s, v23.s[3]
    fadd v4.4s, v4.4s, v5.4s
    fadd v28.4s, v28.4s, v29.4s
    fadd v3.4s, v3.4s, v4.4s
    fadd v27.4s, v27.4s, v28.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2]
    add x0, x0, #32
    add x2, x2, #32
    sub w4, w4, #8
    cmp w4, #8
    b.hs .Larm_frame_32_8
    .Larm_frame_32_8_end:
    cmp w4, #4
    b.lo .Larm_frame_32_4_end
    .Larm_frame_32_4:
    ldr q0, [x0]
    fmul v0.4s, v0.4s, v16.s[0]
    add x9, x0, x5, lsl #4
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v20.s[0]
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #3
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v18.s[0]
    mov x9, #24
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v22.s[0]
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #2
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v17.s[0]
    mov x9, #20
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v21.s[0]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #12
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v19.s[0]
    mov x9, #28
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v23.s[0]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #1
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[2]
    mov x9, #18
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v20.s[2]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #10
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v18.s[2]
    mov x9, #26
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v22.s[2]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #6
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[2]
    mov x9, #22
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v21.s[2]
    fadd v2.4s, v2.4s, v3.4s
    mov x9, #14
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v19.s[2]
    mov x9, #30
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v23.s[2]
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    add x9, x0, x5, lsl #0
    ldr q1, [x9]
    fmul v1.4s, v1.4s, v16.s[1]
    mov x9, #17
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v20.s[1]
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #9
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v18.s[1]
    mov x9, #25
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v22.s[1]
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #5
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v17.s[1]
    mov x9, #21
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v21.s[1]
    fadd v2.4s, v2.4s, v3.4s
    mov x9, #13
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v19.s[1]
    mov x9, #29
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v23.s[1]
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    mov x9, #3
    madd x9, x5, x9, x0
    ldr q2, [x9]
    fmul v2.4s, v2.4s, v16.s[3]
    mov x9, #19
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v20.s[3]
    fadd v2.4s, v2.4s, v3.4s
    mov x9, #11
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v18.s[3]
    mov x9, #27
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v22.s[3]
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    mov x9, #7
    madd x9, x5, x9, x0
    ldr q3, [x9]
    fmul v3.4s, v3.4s, v17.s[3]
    mov x9, #23
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v21.s[3]
    fadd v3.4s, v3.4s, v4.4s
    mov x9, #15
    madd x9, x5, x9, x0
    ldr q4, [x9]
    fmul v4.4s, v4.4s, v19.s[3]
    mov x9, #31
    madd x9, x5, x9, x0
    ldr q5, [x9]
    fmul v5.4s, v5.4s, v23.s[3]
    fadd v4.4s, v4.4s, v5.4s
    fadd v3.4s, v3.4s, v4.4s
    fadd v2.4s, v2.4s, v3.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v0.4s, v0.4s, v1.4s
    str q0, [x2]
    add x0, x0, #16
    add x2, x2, #16
    sub w4, w4, #4
    cmp w4, #4
    b.hs .Larm_frame_32_4
    .Larm_frame_32_4_end:
    cmp w4, #1
    b.lo .Larm_frame_32_1_end
    .Larm_frame_32_1:
    ldr s0, [x0]
    fmul s0, s0, v16.s[0]
    add x9, x0, x5, lsl #4
    ldr s1, [x9]
    fmul s1, s1, v20.s[0]
    fadd s0, s0, s1
    add x9, x0, x5, lsl #3
    ldr s1, [x9]
    fmul s1, s1, v18.s[0]
    mov x9, #24
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v22.s[0]
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #2
    ldr s1, [x9]
    fmul s1, s1, v17.s[0]
    mov x9, #20
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v21.s[0]
    fadd s1, s1, s2
    mov x9, #12
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v19.s[0]
    mov x9, #28
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v23.s[0]
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #1
    ldr s1, [x9]
    fmul s1, s1, v16.s[2]
    mov x9, #18
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v20.s[2]
    fadd s1, s1, s2
    mov x9, #10
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v18.s[2]
    mov x9, #26
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v22.s[2]
    fadd s2, s2, s3
    fadd s1, s1, s2
    mov x9, #6
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[2]
    mov x9, #22
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v21.s[2]
    fadd s2, s2, s3
    mov x9, #14
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v19.s[2]
    mov x9, #30
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v23.s[2]
    fadd s3, s3, s4
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    add x9, x0, x5, lsl #0
    ldr s1, [x9]
    fmul s1, s1, v16.s[1]
    mov x9, #17
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v20.s[1]
    fadd s1, s1, s2
    mov x9, #9
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v18.s[1]
    mov x9, #25
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v22.s[1]
    fadd s2, s2, s3
    fadd s1, s1, s2
    mov x9, #5
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v17.s[1]
    mov x9, #21
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v21.s[1]
    fadd s2, s2, s3
    mov x9, #13
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v19.s[1]
    mov x9, #29
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v23.s[1]
    fadd s3, s3, s4
    fadd s2, s2, s3
    fadd s1, s1, s2
    mov x9, #3
    madd x9, x5, x9, x0
    ldr s2, [x9]
    fmul s2, s2, v16.s[3]
    mov x9, #19
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v20.s[3]
    fadd s2, s2, s3
    mov x9, #11
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v18.s[3]
    mov x9, #27
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v22.s[3]
    fadd s3, s3, s4
    fadd s2, s2, s3
    mov x9, #7
    madd x9, x5, x9, x0
    ldr s3, [x9]
    fmul s3, s3, v17.s[3]
    mov x9, #23
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v21.s[3]
    fadd s3, s3, s4
    mov x9, #15
    madd x9, x5, x9, x0
    ldr s4, [x9]
    fmul s4, s4, v19.s[3]
    mov x9, #31
    madd x9, x5, x9, x0
    ldr s5, [x9]
    fmul s5, s5, v23.s[3]
    fadd s4, s4, s5
    fadd s3, s3, s4
    fadd s2, s2, s3
    fadd s1, s1, s2
    fadd s0, s0, s1
    str s0, [x2]
    add x0, x0, #4
    add x2, x2, #4
    sub w4, w4, #1
    cmp w4, #1
    b.hs .Larm_frame_32_1
    .Larm_frame_32_1_end:
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_32_0), .-LND_SYMBOL(lnd_sinc_arm64_frame_32_0)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_16)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_16), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_16)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_16)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_16):
    ldp q16, q17, [x1]
    ldp q0, q24, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #256]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #128]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #384]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #64]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #320]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #192]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #448]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #0]
    ldp q0, q24, [x0, #32]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #288]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #160]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #416]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #96]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #352]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #224]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #480]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #32]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_16), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_16)
#endif
.p2align 4
.globl LND_SYMBOL(lnd_sinc_arm64_frame_8_32)
#if !defined(_WIN32) && !defined(__APPLE__)
.type LND_SYMBOL(lnd_sinc_arm64_frame_8_32), %function
.hidden LND_SYMBOL(lnd_sinc_arm64_frame_8_32)
#elif defined(__APPLE__)
.private_extern LND_SYMBOL(lnd_sinc_arm64_frame_8_32)
#endif
LND_SYMBOL(lnd_sinc_arm64_frame_8_32):
    ldp q16, q17, [x1]
    ldp q0, q24, [x0, #0]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #512]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #256]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #768]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #128]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #640]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #384]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #896]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #0]
    ldp q0, q24, [x0, #32]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #544]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #288]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #800]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #160]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #672]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #416]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #928]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #32]
    ldp q0, q24, [x0, #64]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #576]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #320]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #832]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #192]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #704]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #448]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #960]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #64]
    ldp q0, q24, [x0, #96]
    fmul v0.4s, v0.4s, v16.s[0]
    fmul v24.4s, v24.4s, v16.s[0]
    ldp q1, q25, [x0, #608]
    fmul v1.4s, v1.4s, v17.s[0]
    fmul v25.4s, v25.4s, v17.s[0]
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #352]
    fmul v1.4s, v1.4s, v16.s[2]
    fmul v25.4s, v25.4s, v16.s[2]
    ldp q2, q26, [x0, #864]
    fmul v2.4s, v2.4s, v17.s[2]
    fmul v26.4s, v26.4s, v17.s[2]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    ldp q1, q25, [x0, #224]
    fmul v1.4s, v1.4s, v16.s[1]
    fmul v25.4s, v25.4s, v16.s[1]
    ldp q2, q26, [x0, #736]
    fmul v2.4s, v2.4s, v17.s[1]
    fmul v26.4s, v26.4s, v17.s[1]
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    ldp q2, q26, [x0, #480]
    fmul v2.4s, v2.4s, v16.s[3]
    fmul v26.4s, v26.4s, v16.s[3]
    ldp q3, q27, [x0, #992]
    fmul v3.4s, v3.4s, v17.s[3]
    fmul v27.4s, v27.4s, v17.s[3]
    fadd v2.4s, v2.4s, v3.4s
    fadd v26.4s, v26.4s, v27.4s
    fadd v1.4s, v1.4s, v2.4s
    fadd v25.4s, v25.4s, v26.4s
    fadd v0.4s, v0.4s, v1.4s
    fadd v24.4s, v24.4s, v25.4s
    stp q0, q24, [x2, #96]
    ret
#if !defined(_WIN32) && !defined(__APPLE__)
.size LND_SYMBOL(lnd_sinc_arm64_frame_8_32), .-LND_SYMBOL(lnd_sinc_arm64_frame_8_32)
#endif
#if !defined(_WIN32) && !defined(__APPLE__)
.section .note.GNU-stack,"",%progbits
#endif
