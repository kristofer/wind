# ESP32-S3 8MB RAM bring-up memory map (phase 1)

- `IRAM` (`0x40370000` - `0x403B0000`, 256KB): startup, vectors, kernel text/rodata
- `DRAM` (`0x3FC88000` - `0x3FCD8000`, 320KB): data, bss, bootstrap stack
- `PSRAM` (`0x3D000000` - `0x3D800000`, 8MB): reserved for later heap/page allocator phases

This map is intentionally minimal for early boot, UART banner output, and timer interrupt servicing.
