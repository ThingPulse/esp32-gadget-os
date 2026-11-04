/* Keep literal pools in .text and pad it to 4 bytes: classic ESP32 instruction
 * RAM accepts word access, and the loader rounds .text's copy size to 4. */
SECTIONS
{
  . = 0x100;
  .text : ALIGN(4) { *(.literal .literal.*) *(.text .text.*) . = ALIGN(4); }
  .rodata : ALIGN(4) { *(.rodata .rodata.*) }
  .data : ALIGN(4) { *(.data .data.*) *(.got .got.*) }
  .bss (NOLOAD) : ALIGN(4) { *(.bss .bss.*) *(COMMON) }
}
