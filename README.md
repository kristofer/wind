# wind

A working area for porting [`mit-pdos/xv6-riscv`](https://github.com/mit-pdos/xv6-riscv) to the ESP32-S3.

## Imported upstream source

Upstream xv6-riscv is imported in:

- `/home/runner/work/wind/wind/xv6-riscv`

Imported snapshot:

- upstream repository: `https://github.com/mit-pdos/xv6-riscv`
- commit: `5474d4bf72fd95a6e5c735c2d7f208f58990ceab`

## Porting plan

See `/home/runner/work/wind/wind/docs/esp32-s3-port-plan.md` for the initial ESP32-S3 porting plan, including architecture differences and phased implementation milestones using a `gcc` + `esptool` workflow.

## macOS host: detailed setup and test instructions

This repository currently has two practical test paths from a macOS host:

1. Run xv6 userspace/kernel regression tests under RISC-V QEMU.
2. Build and flash the phase-1 ESP32-S3 bring-up image.

All commands below assume you start from the repository root and then `cd xv6-riscv` before building/testing.

### 1) Install host dependencies on macOS

Install Homebrew if needed, then install core tools:

```bash
brew update
brew install git make python qemu bc
```

`qemu-system-riscv64` must be at least version 7.2 for the `make qemu` target.

Check it:

```bash
qemu-system-riscv64 --version
```

### 2) Install a RISC-V cross toolchain for xv6 QEMU tests

The xv6 `Makefile` expects a `riscv64-*` cross compiler/binutils toolchain.
Install one of these options:

Option A (recommended if available in your setup): install a prebuilt RISC-V GNU toolchain and ensure binaries such as `riscv64-unknown-elf-gcc` and `riscv64-unknown-elf-objdump` are in `PATH`.

Option B (fallback): build/install from `https://github.com/riscv-collab/riscv-gnu-toolchain` and add its `bin` directory to `PATH`.

Verify detection from `xv6-riscv/`:

```bash
make -n kernel/kernel
```

If toolchain auto-detection fails, explicitly set the prefix (example):

```bash
make TOOLPREFIX=riscv64-unknown-elf- qemu
```

### 3) Run xv6 under QEMU (interactive smoke test)

```bash
cd xv6-riscv
make qemu
```

At the xv6 shell prompt, run a quick manual smoke test:

```text
ls
echo hello
usertests -q
```

`ALL TESTS PASSED` indicates the quick userspace regression set succeeded.

### 4) Run automated test script on macOS

From `xv6-riscv/`:

```bash
python3 test-xv6.py usertests
python3 test-xv6.py -q usertests
python3 test-xv6.py forphan
python3 test-xv6.py dorphan
```

Notes for macOS:

- `test-xv6.py crash`/`test-xv6.py log` use GNU-style `ps` flags and may fail on default macOS `ps`.
- Prefer the `usertests`, `forphan`, and `dorphan` paths unless you adapt that script for BSD `ps` semantics.

### 5) Install ESP32-S3 toolchain on macOS

For hardware bring-up (`make esp32s3` and flash), use Espressif's official install flow:

```bash
mkdir -p ~/esp
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

This should make `idf.py`, `xtensa-esp32s3-elf-gcc`, and the rest of the ESP-IDF toolchain available in your shell.

Verify:

```bash
idf.py --version
xtensa-esp32s3-elf-gcc --version
esptool version
```

### 6) Build and flash ESP32-S3 image from macOS

From `xv6-riscv/`:

```bash
make esp32s3
```

This now builds the repo-local ESP-IDF wrapper project in `xv6-riscv/esp32s3-idf`.
ESP-IDF generates the bootloader, partition table, and application image together, so you do not need to build or reuse `hello_world` artifacts from `~/esp/esp-idf`.

The main build outputs are under:

- `esp32s3-idf/build/bootloader/bootloader.bin`
- `esp32s3-idf/build/partition_table/partition-table.bin`
- `esp32s3-idf/build/wind_esp32s3.bin`

Find your serial device (typical macOS device paths are under `/dev/cu.*`):

```bash
ls /dev/cu.*
```

Flash:

```bash
make esp32s3-flash ESP_PORT=/dev/cu.usbmodem1101 ESP_BAUD=460800
```

Monitor serial output using the IDF-backed helper target:

```bash
make esp32s3-monitor ESP_PORT=/dev/cu.usbmodem1101
```

### 7) Observe UART output (hardware sanity check)

After flashing, you can either use the helper above or open a raw serial monitor yourself:

```bash
screen /dev/cu.usbmodem1101 115200
```

You should see the phase-1 bring-up log output from `kernel/xtensa`, followed by periodic tick messages that include reduced-process scheduler state (current pid, runnable/sleeping counts), periodic sleep/wakeup transition logs, and allocator free-page counts.

### 7.1) Optional allocator negative self-test (intentional panic)

For allocator hardening verification, there is an opt-in negative test that intentionally double-frees one page and should trigger allocator panic.

Enable it:

```bash
cd xv6-riscv
idf.py -C esp32s3-idf menuconfig
```

Then enable:

- `wind ESP32-S3 Options` -> `Enable allocator negative self-test (intentional double-free)`
- optional: `wind ESP32-S3 Options` -> `Enable allocator negative self-test (intentional invalid free)`

Build/flash/monitor again. Expected behavior:

- normal allocator self-test prints PASS
- proc service self-test prints PASS
- negative test announces start
- allocator panic message appears after second `kfree` (double-free mode) or immediately on out-of-pool `kfree` (invalid-free mode)

Disable this option for normal bring-up and tick-loop work.

### 8) Typical macOS troubleshooting

- `kernel/xtensa/main.c: fatal error: types.h: No such file or directory`:
  use the latest repository version where Xtensa sources include `kernel/types.h`.
  If you are carrying local changes, update these includes in `kernel/xtensa/*.c` and `kernel/xtensa/*.h` from `types.h` to `kernel/types.h`.
- `ERROR: Source ~/esp/esp-idf/export.sh before running ESP32-S3 targets`:
  source the ESP-IDF environment in the same shell before running `make esp32s3`, `make esp32s3-flash`, or `make esp32s3-monitor`.
- Boot logs mention missing or invalid app partitions:
  rebuild and flash using the repo-local ESP-IDF flow above instead of flashing a raw `kernel.bin` directly.
- `Error: Couldn't find a riscv64 version of GCC/binutils`:
  install/add a RISC-V toolchain and retry, or set `TOOLPREFIX=...` explicitly.
- `ERROR: Need qemu version >= 7.2`:
  upgrade QEMU via Homebrew.
- `xtensa-esp32s3-elf-gcc: command not found`:
  rerun `. ~/esp/esp-idf/export.sh` in your current shell.
- `esptool.py` cannot connect:
  confirm `ESP_PORT`, cable/data support, and board bootloader mode.

## ESP32-S3 phase 1 bring-up artifacts

Minimal phase-1 ESP32-S3 bring-up sources now live in:

- `/home/runner/work/wind/wind/xv6-riscv/kernel/xtensa`

The ESP-IDF wrapper project used for local ESP32-S3 builds now lives in:

- `/home/runner/work/wind/wind/xv6-riscv/esp32s3-idf`

Build and flash helpers were added to:

- `/home/runner/work/wind/wind/xv6-riscv/Makefile`

Example commands (from `/home/runner/work/wind/wind/xv6-riscv`):

```bash
make esp32s3
make esp32s3-flash ESP_PORT=/dev/ttyACM0
make esp32s3-monitor ESP_PORT=/dev/ttyACM0
```

`make esp32s3` now builds the repo-local ESP-IDF app wrapper and generates a complete flashable image set under `esp32s3-idf/build/`.  
For ESP32-S3 hardware flashing, use `make esp32s3-flash` so the bootloader, partition table, and app stay in sync.

## Debian arm64 host setup notes

On Debian arm64, install baseline tooling:

```bash
sudo apt update
sudo apt install -y build-essential make git python3 python3-pip python3-venv
python3 -m pip install --user --upgrade esptool
```

Install the ESP32-S3 Xtensa toolchain using Espressif's installer (recommended on arm64):

```bash
mkdir -p ~/esp
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

After this, `xtensa-esp32s3-elf-gcc` and `esptool.py` should be available for `make esp32s3` and `make esp32s3-flash`.

## Upstream sync process (repeatable)

```bash
UPSTREAM_COMMIT="$(git ls-remote https://github.com/mit-pdos/xv6-riscv.git HEAD | awk '{print $1}')"
curl -L "https://github.com/mit-pdos/xv6-riscv/archive/${UPSTREAM_COMMIT}.tar.gz" -o /tmp/xv6-riscv.tar.gz
rm -rf xv6-riscv && mkdir -p xv6-riscv
tar -xzf /tmp/xv6-riscv.tar.gz -C xv6-riscv --strip-components=1
```
