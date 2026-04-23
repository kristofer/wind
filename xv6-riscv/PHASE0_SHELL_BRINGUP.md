# Phase 0: xv6 Shell Bring-Up Baseline (ESP32-S3)

This phase intentionally locks the current scheduler/spawn/wait behavior as a known-good baseline before readline/ROMFS shell plumbing changes.

## Baseline lock (must stay true)

The baseline is considered valid when captured serial logs contain:

- `wind: page allocator ready: N free pages` + `wind: allocator selftest PASS`
- `wind: romfs catalog registered count=N`
- `wind: scheduler bootstrap PASS`
- `wind: trap/syscall scaffold ready`
- Tick messages with `free_pages=X/Y` from the boot window (first 10 seconds), stable totals

Note: per-syscall and per-scheduler-step debug messages are intentionally removed as of Phase 8 to allow the serial console to be used for interactive shell I/O.

## Repeatable smoke test

1. Build and flash:

```bash
cd /home/runner/work/wind/wind/xv6-riscv
make esp32s3
make esp32s3-flash ESP_PORT=/dev/ttyACM0
```

2. Capture serial logs:

```bash
make esp32s3-monitor ESP_PORT=/dev/ttyACM0 | tee /tmp/wind-phase0.log
```

3. Validate against the locked baseline:

```bash
python3 /home/runner/work/wind/wind/xv6-riscv/test-phase0-shell.py /tmp/wind-phase0.log
```

## Phase 8 regression suite

For full shell-interactive testing (Phase 8):

```bash
# Capture a log that includes interactive shell use
make esp32s3-monitor ESP_PORT=/dev/ttyACM0 | tee /tmp/wind-phase8.log
# (type some commands at the $ prompt, then stop capture)
python3 /home/runner/work/wind/wind/xv6-riscv/test-phase8-shell.py /tmp/wind-phase8.log
```

## Command-level acceptance tests (defined now for later phases)

These are the shell-level acceptance checks that Phase 1+ work must satisfy:

1. Prompt loop: shell prints prompt repeatedly without deadlock.
2. Readline editing: typed characters echo; backspace edits; newline commits one command line.
3. Parse + launch: command name resolves from ROMFS and child process starts.
4. Completion status: parent shell waits and reports child completion status.
5. Core commands: `echo`, `ls`, `cat`, `wc` produce expected stdout.
6. Error paths: unknown command, missing ROMFS entry, malformed/overlong line, read-only write attempt.
7. Resource stability: free-page totals remain stable after many command runs.
