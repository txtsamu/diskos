/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 diskOS contributors */
SECTIONS {
  . = 0xa0c00060;
  .text : { *(.text.entry) *(.text*) *(.rodata*) }
  .data : { *(.data*) *(.sdata*) }
  .bss : { *(.bss*) *(.sbss*) *(COMMON) }
  /DISCARD/ : { *(.MIPS.*) *(.comment) *(.reginfo) *(.pdr) *(.gnu.*) *(.note*) }
}
