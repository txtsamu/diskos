/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 diskOS contributors */
    .set noreorder
    .section .text.start,"ax"
    .globl _start
_start:
    lui   $8,  0xb240
    sw    $31, 0x17f0($8)
    sw    $29, 0x17ec($8)
    lui   $29, 0xa0bf
    ori   $29, $29, 0xfff0
    jal   my_write
    nop
    lui   $8,  0xb240
    lw    $31, 0x17f0($8)
    lw    $29, 0x17ec($8)
    jr    $31
    nop
