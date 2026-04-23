#!/usr/bin/env python3
import argparse
import pathlib
import re
import sys


REQUIRED_PATTERNS = {
    "boot_scheduler_baseline": [
        r"wind: scheduler bootstrap PASS",
        r"wind: trap/syscall scaffold ready",
    ],
    "romfs_init": [
        r"wind: romfs catalog registered count=\d+",
    ],
    "allocator_baseline": [
        r"wind: allocator selftest PASS",
    ],
}

TICK_FREE_PAGES_RE = re.compile(r"wind: tick=\d+ .*free_pages=(\d+)/(\d+)")


def _check_patterns(text: str) -> list[str]:
    failures: list[str] = []
    for check_name, patterns in REQUIRED_PATTERNS.items():
        for pattern in patterns:
            if re.search(pattern, text, re.MULTILINE) is None:
                failures.append(f"{check_name}: missing /{pattern}/")
    return failures


def _check_free_pages(text: str) -> list[str]:
    failures: list[str] = []
    samples = [(int(free), int(total)) for free, total in TICK_FREE_PAGES_RE.findall(text)]
    if not samples:
        return ["resource_stability: missing tick free_pages samples"]

    free_values = [free for free, _ in samples]
    totals = {total for _, total in samples}
    if len(totals) != 1:
        failures.append(f"resource_stability: total page count changed across ticks: {sorted(totals)}")

    drift = max(free_values) - min(free_values)
    if drift > 2:
        failures.append(
            f"resource_stability: free page drift too high across ticks (max-min={drift}, expected <=2)"
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 0 smoke checker for ESP32-S3 shell/scheduler baseline logs."
    )
    parser.add_argument("log_file", help="Path to captured serial log output")
    args = parser.parse_args()

    log_path = pathlib.Path(args.log_file)
    if not log_path.exists():
        print(f"FAIL: log file not found: {log_path}")
        return 1

    text = log_path.read_text(encoding="utf-8", errors="ignore")
    failures = _check_patterns(text)
    failures.extend(_check_free_pages(text))

    if failures:
        print("Phase 0 smoke test: FAIL")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print("Phase 0 smoke test: PASS")
    print(" - scheduler/allocator boot baseline observed")
    print(" - romfs catalog initialized")
    print(" - tick free-page counters stable")
    return 0


if __name__ == "__main__":
    sys.exit(main())
