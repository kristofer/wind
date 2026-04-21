ENTRY(_start)

MEMORY
{
  iram (rx)  : ORIGIN = 0x40370000, LENGTH = 256K
  dram (rw)  : ORIGIN = 0x3FC88000, LENGTH = 320K
  psram (rw) : ORIGIN = 0x3D000000, LENGTH = 8M
}

SECTIONS
{
  .WindowVectors ORIGIN(iram) :
  {
    KEEP(*(.WindowVectors.text))
  } > iram

  .Level2InterruptVector ORIGIN(iram) + 0x180 :
  {
    KEEP(*(.Level2InterruptVector.text))
  } > iram

  .Level1InterruptVector ORIGIN(iram) + 0x1A0 :
  {
    KEEP(*(.Level1InterruptVector.text))
  } > iram

  .Level3InterruptVector ORIGIN(iram) + 0x1C0 :
  {
    KEEP(*(.Level3InterruptVector.text))
  } > iram

  .Level4InterruptVector ORIGIN(iram) + 0x200 :
  {
    KEEP(*(.Level4InterruptVector.text))
  } > iram

  .Level5InterruptVector ORIGIN(iram) + 0x240 :
  {
    KEEP(*(.Level5InterruptVector.text))
  } > iram

  .DebugExceptionVector ORIGIN(iram) + 0x280 :
  {
    KEEP(*(.DebugExceptionVector.text))
  } > iram

  .NMIExceptionVector ORIGIN(iram) + 0x2C0 :
  {
    KEEP(*(.NMIExceptionVector.text))
  } > iram

  .KernelExceptionVector ORIGIN(iram) + 0x300 :
  {
    KEEP(*(.KernelExceptionVector.text))
  } > iram

  .UserExceptionVector ORIGIN(iram) + 0x340 :
  {
    KEEP(*(.UserExceptionVector.text))
  } > iram

  .DoubleExceptionVector ORIGIN(iram) + 0x3C0 :
  {
    KEEP(*(.DoubleExceptionVector.text))
  } > iram

  .text :
  {
    KEEP(*(.text.start))
    *(.text .text.*)
    *(.rodata .rodata.*)
  } > iram

  .data :
  {
    *(.data .data.*)
  } > dram

  .bss (NOLOAD) :
  {
    . = ALIGN(16);
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } > dram

  .stack (NOLOAD) :
  {
    . = ALIGN(16);
    _stack_bottom = .;
    . = . + 0x4000;
    _stack_top = .;
  } > dram

  .psram (NOLOAD) :
  {
    __psram_start = ORIGIN(psram);
    __psram_end = ORIGIN(psram) + LENGTH(psram);
  } > psram
}
