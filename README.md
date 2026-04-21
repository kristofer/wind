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

## Upstream sync process (repeatable)

```bash
UPSTREAM_COMMIT="$(git ls-remote https://github.com/mit-pdos/xv6-riscv.git HEAD | awk '{print $1}')"
curl -L "https://github.com/mit-pdos/xv6-riscv/archive/${UPSTREAM_COMMIT}.tar.gz" -o /tmp/xv6-riscv.tar.gz
rm -rf xv6-riscv && mkdir -p xv6-riscv
tar -xzf /tmp/xv6-riscv.tar.gz -C xv6-riscv --strip-components=1
```
