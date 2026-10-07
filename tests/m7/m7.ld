/* QEMU's mps2-an500, a Cortex-M7 board: the vector table at zero, and the
   image, heap and stack in its 16 MB of RAM at 0x60000000. */
MEMORY
{
  vec (rx)  : ORIGIN = 0x00000000, LENGTH = 1K
  ram (rwx) : ORIGIN = 0x60000000, LENGTH = 16M
}

ENTRY(m7_reset)

SECTIONS
{
  .vectors : { KEEP(*(.vectors)) } > vec
  .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) } > ram
  .ARM.exidx : { *(.ARM.exidx*) } > ram
  .init_array : { PROVIDE(__init_array_start = .); KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) PROVIDE(__init_array_end = .); } > ram
  .data : { *(.data*) } > ram
  .bss : { __bss_start__ = .; *(.bss*) *(COMMON) __bss_end__ = .; } > ram
  end = .;
  __stack_top = ORIGIN(ram) + LENGTH(ram);
}
