## Plan: xv6 Shell Bring-Up on ESP32-S3

Goal: run xv6-style shell behavior from console input with command execution from user program set, using embedded user blobs and read-only ROMFS in app flash as the first filesystem substrate. Recommended path is staged to avoid blocking on full xv6 disk/VM assumptions while preserving upgrade paths to a real filesystem and stronger user/kernel separation later.

**Steps**
1. Phase 0: Lock current baseline and define acceptance tests.  
   - Freeze the current working scheduler/spawn/wait behavior as the known-good baseline.  
   - Capture a repeatable smoke test for shell lifecycle, sleep/wakeup, and memory page counters.  
   - Define command-level acceptance tests now (prompt, parse, run command, report exit status).

2. Phase 1: Console readline substrate in kernel I/O path.  
   - Add line-buffered input ingestion (editable line discipline: echo, backspace, newline commit) in the Xtensa console path.  
   - Feed input from UART RX polling in the kernel poll loop first (interrupt-driven RX can come later).  
   - Add a wake channel for line-ready events so shell read blocks correctly and resumes on newline.  
   - Depends on Phase 0.

3. Phase 2: Syscall surface needed by interactive shell.  
   - Add read-oriented syscall path and user-facing wrappers for line input.  
   - Add minimal file/process syscalls required by early shell commands and command launching path.  
   - Keep syscall ABI stable for later transition to richer user-binary runtime.  
   - Depends on Phase 1.

4. Phase 3: Read-only ROMFS in app flash (mock filesystem bootstrap).  
   - Introduce a ROMFS catalog in flash with entries for shell + selected user commands + simple device nodes.  
   - Provide name lookup/open/read primitives from kernel side (path-based command lookup).  
   - Keep writes out-of-scope in this phase (read-only only).  
   - Parallel with Phase 2 once syscall IDs are reserved; integration depends on both.

5. Phase 4: User program packaging and launch pipeline.  
   - Build xv6 user sources into Xtensa-compatible embedded user blobs.  
   - Create a loader path from ROMFS entry to runnable process image in flat user region model.  
   - Wire path-based exec/spawn so shell can launch command by name from ROMFS.  
   - Depends on Phases 2 and 3.

6. Phase 5: Shell compatibility layer for xv6 user sh.c.  
   - Port and compile xv6 user shell source for this target runtime (ABI + syscall wrapper alignment).  
   - Implement command execution flow expected by shell core: parse, spawn/exec-by-path, wait, status handling.  
   - Enable at least core command invocation and argument passing from console line input.  
   - Depends on Phase 4.

7. Phase 6: Command set bring-up from xv6 user directory.  
   - Prioritize command binaries in this order: echo, cat, ls, wc, grep, mkdir/rm (as feasible under read-only ROMFS constraints).  
   - For commands requiring write/create/remove, either:  
     - return clear read-only errors in this phase, or  
     - provide temporary in-memory overlay for writable paths (/tmp-like behavior).  
   - Depends on Phase 5.

8. Phase 7: Device/file namespace minimum for shell usability.  
   - Provide minimal device endpoints used by shell interactions (console stdin/stdout/stderr mapping).  
   - Ensure open/read/write on console-backed descriptors works consistently across shell and child commands.  
   - Depends on Phase 5; can run partially in parallel with Phase 6.

9. Phase 8: Hardening and parity milestones.  
   - Add regression suite for readline behavior, command launch, wait/reap correctness, and ROMFS lookup failures.  
   - Measure latency and memory usage under repeated command loops.  
   - Define migration seam for next filesystem step (VFS shim and/or SPIFFS/LittleFS backend) without breaking shell ABI.  
   - Depends on Phases 6 and 7.

**Relevant files**
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/console_idf.c — extend from output-only to line-input plus line-ready signaling.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/uart.c — add RX polling primitives for console input ingestion.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/main.c — integrate input polling and keep tick-driven scheduling responsive.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/trap_syscall_stub.c — add read/file/exec-by-path syscall plumbing and wrappers.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/port.h — syscall IDs, process/IO contracts, shell-facing API declarations.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/xtensa/scheduler_stub.c — ensure spawn/wait/read blocking semantics remain correct under shell workload.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/file.c — reference patterns for file descriptor behavior, adapt subset for ROMFS-backed descriptors.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/fs.c — reference inode/path concepts to emulate minimally in ROMFS lookup layer.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/kernel/exec.c — reference exec semantics to map into flat-region loader behavior.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/user/sh.c — target shell source for phased compatibility bring-up.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/user/*.c — command sources to package into ROMFS user blobs.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/main/CMakeLists.txt — include generated blob/ROMFS objects and new kernel sources.
- /Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/sdkconfig.defaults — adjust partition/fs feature defaults as needed for ROMFS footprint.

**Verification**
1. Boot + scheduler baseline: no regression in spawn/wait/reap and sleep/wakeup logs under sustained runtime.
2. Readline checks: typed characters echo correctly, backspace edits line, newline commits one line to shell reader.
3. Shell prompt loop: repeated prompt/input cycles without deadlock or memory leak.
4. Command launch: shell resolves command names from ROMFS and launches child process, then reports completion status.
5. Core command tests: echo, ls, cat, wc run from shell prompt with expected stdout behavior.
6. Error-path tests: unknown command, missing ROMFS entry, malformed line input, overlong line, and read-only operation attempts.
7. Resource tests: stable free page counts and user-region reclaim after many command runs.

**Decisions**
- Included scope:
  - xv6-like shell core behavior from console with command launch.
  - Embedded user blobs in app flash ROMFS.
  - Read-only filesystem bootstrap first.
- Excluded in this plan (deferred):
  - Full upstream xv6 disk image path and block-device-backed fs.c bring-up.
  - Full MMU-style isolation equivalent to original RISC-V xv6.
  - Immediate mutable persistent filesystem semantics.

**Further Considerations**
1. Read-only ROMFS vs temporary writable overlay: recommended to add a tiny RAM overlay for paths shell might touch, while keeping ROMFS authoritative for binaries.
2. Syscall compatibility strategy: keep wrapper naming aligned with xv6 user-space expectations so moving from shimmed shell to nearer-upstream shell is mechanical, not architectural.
3. Preemption stability under input bursts: keep UART input ingestion non-blocking and avoid ISR-side scheduler mutation until fully hardened.