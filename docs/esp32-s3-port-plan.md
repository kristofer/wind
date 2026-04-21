# ESP32-S3 xv6 Porting Plan (Initial)

This document starts the porting plan from `xv6-riscv` to an ESP32-S3 target with 8MB RAM, assuming the toolchain is `clang` + `esptool`.

## 1) Key architecture differences to account for

## 1.1 ISA and privilege model

- `xv6-riscv` assumes RISC-V machine/supervisor privilege behavior and RISC-V trap CSRs.
- ESP32-S3 uses Xtensa LX7, so trap/interrupt entry, register save/restore, and exception causes must be fully reimplemented for Xtensa.
- Many xv6 files under `kernel/` that are architecture-specific (`entry`, trap handling, context switch, low-level locks, timer setup) require a dedicated Xtensa rewrite.

## 1.2 Memory architecture: Harvard-style constraints vs xv6 assumptions

- Typical xv6-on-RISC-V targets are closer to a single, flat memory view used like a von Neumann model.
- ESP32-S3 is Harvard-oriented in practice: instruction fetch and data access have distinct memory paths/regions, with executable memory constraints and flash/cache mapping behavior.
- Porting impact:
  - Keep executable code and vectors in valid executable regions (IRAM / mapped IROM windows as appropriate).
  - Keep mutable kernel data in DRAM.
  - Treat memory layout/linker script as a first-class port artifact, not a minor build detail.

## 1.3 MMU/process model expectations

- `xv6-riscv` depends on paged virtual memory (`sv39`, per-process page tables, copy-on-mapping semantics).
- ESP32-S3 does not provide an xv6-equivalent general-purpose paged VM model in the same way.
- Initial strategy:
  - Bring up a flat physical-address kernel first.
  - Replace or stub xv6 VM paths with a simplified region allocator.
  - Reintroduce process isolation only after basic kernel/user execution and traps are stable.

## 1.4 Boot and image format

- xv6 boot path (OpenSBI/QEMU `virt`) is not applicable.
- ESP32-S3 boot flow requires ESP image format, ROM bootloader conventions, partitioning, and flashing via `esptool`.
- Build artifacts should produce an ESP32-compatible bootable image and a flash map definition.

## 2) Toolchain and build-system migration (`clang` + `esptool`)

1. Add Xtensa target flags and linker flow for `clang`.
2. Split architecture-independent kernel code from RISC-V-specific code.
3. Introduce ESP32-S3 linker script(s) for IRAM/DRAM/flash placement.
4. Add image packaging + flash commands using `esptool`.
5. Preserve an automated build target for reproducible kernel images.

## 3) Phased execution plan

## Phase 0: Source import and baseline

- [x] Import upstream `xv6-riscv` source.
- [x] Record a repeatable upstream-sync process.

## Phase 1: Minimal kernel bring-up on ESP32-S3

- [ ] Create `kernel/xtensa/` port layer (startup, vectors, context switch, timer tick, UART console).
- [ ] Provide a minimal linker script and memory map for 8MB RAM configuration.
- [ ] Boot kernel, print console banner, and service timer interrupt.

## Phase 2: Core kernel services without full VM

- [ ] Implement physical memory allocator for available DRAM/PSRAM policy.
- [ ] Bring up scheduler with a reduced process model.
- [ ] Rework trap/syscall path for Xtensa exception model.

## Phase 3: User-mode and syscall stabilization

- [ ] Run minimal user program from embedded image.
- [ ] Enable core syscalls (`write`, `exit`, `fork`/equivalent strategy, `wait`).
- [ ] Validate context switching and preemption under timer interrupts.

## Phase 4: Filesystem and robustness

- [ ] Adapt xv6 filesystem image strategy to ESP flash/partition layout.
- [ ] Add fault handling and watchdog-friendly panic/reporting behavior.
- [ ] Add regression smoke tests suitable for hardware/emu execution.

## 4) Immediate next engineering tasks

1. Carve out arch-neutral kernel code from current RISC-V-specific files.
2. Draft ESP32-S3 linker script and explicit section placement policy.
3. Implement first-stage Xtensa startup + UART console output.
4. Define flash layout and `esptool` command sequence for iterative bring-up.
5. Decide policy for using external RAM (8MB) for kernel heap vs user pages.
