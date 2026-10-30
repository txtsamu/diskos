/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 diskOS contributors */
ENTRY(_start)
SECTIONS {
  . = 0xa0c00000;
  .text : { *(.text.start) *(.text.entry) *(.text*) *(.rodata*) *(.data*) }
  .bss  : { *(.bss*) *(COMMON) }
}
