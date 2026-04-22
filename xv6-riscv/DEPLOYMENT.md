# ESP32-S3 xv6 Xtensa Kernel Deployment Guide

## Build Status
✅ **Build Complete** - All phases of the first concrete milestone implemented and compiled successfully.

Build details:
- Build steps: [1073/1073] completed
- Bootloader: bootloader.bin (21KB, 36% free)
- Partition table: partition-table.bin (3KB)
- Application: wind_esp32s3.bin (171KB, 83% app partition free)
- Build location: `/Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/build/`

## What Was Implemented

### Phase 1: Syscall Interface
- Added `WIND_SYSCALL_SLEEP_ON_CHAN` (ID 2)
- Added `WIND_SYSCALL_WAKEUP_CHAN` (ID 3)
- Defined unified `struct xtensa_proc` with pid, state, wait_chan, kstack
- All in [kernel/xtensa/port.h](kernel/xtensa/port.h)

### Phase 2: Trap Handlers
- `xtensa_sys_sleep_on_chan()` - sleeps current process on channel
- `xtensa_sys_wakeup_chan()` - wakes first process waiting on channel
- Integrated into `xtensa_trap_handle_syscall()` dispatcher
- All in [kernel/xtensa/trap_syscall_stub.c](kernel/xtensa/trap_syscall_stub.c)

### Phase 3: Scheduler Migration
- Migrated from local `wind_sched_proc` to shared `struct xtensa_proc`
- Updated all state machine to `XTENSA_PROC_*` enum values
- Preserved round-robin scheduling and fairness logic
- In [kernel/xtensa/scheduler_stub.c](kernel/xtensa/scheduler_stub.c)

### Phase 4: Atomic Kstack Allocation
- Moved kstack allocation into `xtensa_sched_create_proc()`
- Uses `xtensa_page_alloc()` for memory management
- Atomic: either full success or complete rollback, no orphaned allocations
- In [kernel/xtensa/scheduler_stub.c](kernel/xtensa/scheduler_stub.c)

### Phase 5: Syscall-Driven Demo
- Routed synthetic sleep/wakeup through trap dispatcher
- Demo exercises all three syscall paths every 5-7 seconds
- Generates diagnostic logs showing state transitions
- In [kernel/xtensa/main.c](kernel/xtensa/main.c)

## Hardware Requirements
- ESP32-S3 development board
- USB-UART cable (for serial flashing and monitoring)
- Serial port at `/dev/cu.usbmodem1101` (on macOS) or equivalent

## Flashing Instructions

### Method 1: Using Makefile (Recommended)
```bash
cd /Users/kryounger/LocalProjects/wind/xv6-riscv

# Flash the binary
make esp32s3-flash

# Monitor serial output
make esp32s3-monitor
```

### Method 2: Manual esptool Command
```bash
cd /Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/build

python -m esptool \
  --chip esp32s3 \
  -b 460800 \
  --before default-reset \
  --after hard-reset \
  write-flash \
  --flash-mode dio \
  --flash-size 8MB \
  --flash-freq 80m \
  0x0 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x10000 wind_esp32s3.bin
```

### Method 3: Using Flash Arguments File
```bash
cd /Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/build
python -m esptool --chip esp32s3 -b 460800 --before default-reset --after hard-reset write-flash @flash_args
```

## Monitoring Serial Output

After flashing, monitor kernel output:
```bash
make esp32s3-monitor
```

Expected boot sequence output:
1. ESP-IDF bootloader messages
2. "wind: ESP32-S3 kernel bring-up"
3. "wind: page allocator ready: X free pages"
4. "wind: allocator selftest PASS"
5. "wind: scheduler bootstrap PASS"
6. Periodic tick messages with syscall logs:
   - "wind: syscall yield ret_pid=..."
   - "wind: syscall sleep_on_chan(chan=1) ret_pid=..."
   - "wind: syscall wakeup_chan(chan=1) ret=..."

## Expected Runtime Behavior

### Periodic Demo Pattern (Every 5-7 seconds)
- **Second 0**: Yield syscall dispatched, scheduler reschedules
- **Second 5**: Wakeup syscall tries to wake processes on channel 1
- **Second 9**: Sleep syscall puts current process to sleep on channel 1
- **Scheduler output**: Shows runnable/sleeping process counts, free memory

### State Transitions
- Process state changes logged: RUNNING → SLEEPING, SLEEPING → RUNNABLE
- Free page count tracked (should remain stable, no leaks)
- Scheduler dump every 10 seconds shows all process slots

## Build Artifacts Location
```
/Users/kryounger/LocalProjects/wind/xv6-riscv/esp32s3-idf/build/
├── wind_esp32s3.bin                    # Main application binary
├── wind_esp32s3.elf                    # ELF with debug symbols
├── bootloader/
│   └── bootloader.bin                  # Bootloader binary
├── partition_table/
│   └── partition-table.bin             # Partition table
└── flash_args                          # esptool flash arguments file
```

## Files Modified in Implementation
1. `kernel/xtensa/port.h` - Syscall IDs and shared proc types
2. `kernel/xtensa/trap_syscall_stub.c` - Trap handlers
3. `kernel/xtensa/scheduler_stub.c` - Scheduler migration and kstack allocation
4. `kernel/xtensa/main.c` - Demo routing and code quality
5. `esp32s3-idf/main/CMakeLists.txt` - Source list cleanup

## Verification Checklist
- ✅ All 5 implementation phases complete
- ✅ Code compiles without errors or warnings
- ✅ Full ESP-IDF build successful [1073/1073]
- ✅ All kernel symbols present in final ELF
- ✅ Binary artifacts generated and verified
- ✅ Code ready for hardware deployment

## Next Steps
After successful hardware deployment:
1. Verify boot logs show "scheduler bootstrap PASS"
2. Observe syscall dispatch logs
3. Monitor process state transitions
4. Verify memory remains stable (no leaks)
5. Plan Phase 2 improvements (real trapframe contexts, user task lifecycle)

## Troubleshooting

### Serial Port Not Found
Check available ports:
```bash
ls -la /dev/cu.* /dev/tty.*
```

Adjust the Makefile or esptool command with the correct port.

### Flashing Fails
Ensure:
1. ESP32-S3 is in bootloader mode (hold BOOT button while connecting)
2. USB cable is properly connected
3. Serial driver is installed
4. Port permissions are correct (`sudo` if needed)

### No Serial Output After Flash
1. Check serial monitor baud rate (should be 115200)
2. Verify bootloader.bin was flashed correctly
3. Try pressing the RESET button on the board
4. Check that partition table is at offset 0x8000

## Build Environment Details
- Target: ESP32-S3 (Xtensa LX7 processor)
- ESP-IDF Version: Latest (338f3411)
- Toolchain: xtensa-esp32s3-elf (esp-15.2.0_20251204)
- Python: 3.13 (in venv)
- Host OS: macOS

---

**Implementation Date:** April 22, 2025  
**Status:** Ready for Hardware Deployment  
**Milestone:** First Concrete xv6 Xtensa Task Complete
