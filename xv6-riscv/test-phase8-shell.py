#!/usr/bin/env python3
"""
Phase 8 regression suite for xv6-style shell on ESP32-S3.

Validates a captured serial log against the Phase 8 acceptance criteria:
  1. Boot + scheduler baseline (quiet boot, no per-syscall spam).
  2. Shell prompt loop ($ prompt appears, repeats after each command).
  3. Command launch (echo, ls, cat, wc complete from ROMFS).
  4. Error-path behavior (unknown command, missing ROMFS entry, read-only ops).
  5. Resource stability (free page counts stable across the boot window).

Usage:
    python3 test-phase8-shell.py <log_file>

The log_file is a captured serial output from the device under test.
Capture with:
    make esp32s3-monitor ESP_PORT=/dev/ttyACM0 | tee /tmp/wind-phase8.log
Then run a set of commands at the shell prompt before stopping capture.
"""

import argparse
import pathlib
import re
import sys
from typing import NamedTuple


# ---------------------------------------------------------------------------
# Boot-window checks: messages emitted before the shell becomes interactive.
# ---------------------------------------------------------------------------

BOOT_REQUIRED: dict[str, list[str]] = {
    "allocator_init": [
        r"wind: page allocator ready: \d+ free pages",
        r"wind: allocator selftest PASS",
    ],
    "romfs_init": [
        r"wind: romfs catalog registered count=\d+",
    ],
    "scheduler_bootstrap": [
        r"wind: scheduler bootstrap PASS",
    ],
    "trap_scaffold": [
        r"wind: trap/syscall scaffold ready",
    ],
}

# Patterns that must NOT appear after boot; their presence indicates that
# per-syscall or per-scheduler debug spam was left on (Phase 8 requirement:
# quiet console for interactive shell use).
NOISE_FORBIDDEN: list[str] = [
    r"wind: syscall yield ret_pid=",
    r"wind: syscall sleep_on_chan\(",
    r"wind: syscall wakeup_chan\(",
    r"wind: syscall write\(uoff=",
    r"wind: syscall read\(uoff=",
    r"wind: syscall spawn ret=",
    r"wind: syscall open\(uoff=",
    r"wind: sched spawn child_pid=",
    r"wind: sched zombie pid=",
    r"wind: sched reap parent=",
    r"wind: sched exec pid=",
    r"wind: proc201 step=",
    r"wind: proc202 step=",
    r"wind: proc203 step=",
    r"wind: proc100 wait reaped",
    r"wind: user_init step=3 spawning",
    r"wind: user_init spawned shell pid=",
    r"wind: user_init reaped child=",
    r"wind: shell command pid=",
]

# ---------------------------------------------------------------------------
# Shell readline checks.
# ---------------------------------------------------------------------------

# The shell prompt "$ " must appear at least once.
SHELL_PROMPT_RE = re.compile(r"^\$ ", re.MULTILINE)

# ---------------------------------------------------------------------------
# Command output checks (present when corresponding commands were run).
# ---------------------------------------------------------------------------

class CommandCheck(NamedTuple):
    name: str
    trigger_re: str          # command name in the log (after "$")
    expected_re: str         # expected output pattern
    optional: bool = True    # True = skip check if trigger not found


COMMAND_CHECKS: list[CommandCheck] = [
    CommandCheck(
        name="echo",
        trigger_re=r"^\$ echo ",
        expected_re=r"(?m)^[^\$]",   # any non-prompt output line
    ),
    CommandCheck(
        name="ls_lists_commands",
        trigger_re=r"^\$ ls\b",
        expected_re=r"(?:echo|ls|cat|wc|grep|shell)",
    ),
    CommandCheck(
        name="cat_motd",
        trigger_re=r"^\$ cat /etc/motd",
        expected_re=r"wind: romfs bootstrap",
    ),
    CommandCheck(
        name="wc_motd",
        trigger_re=r"^\$ wc /etc/motd",
        # wc output: "lines words bytes path"
        expected_re=r"^\d+ \d+ \d+ ",
    ),
    CommandCheck(
        name="mkdir_readonly_error",
        trigger_re=r"^\$ mkdir ",
        expected_re=r"read-only filesystem",
    ),
    CommandCheck(
        name="rm_readonly_error",
        trigger_re=r"^\$ rm ",
        expected_re=r"read-only filesystem",
    ),
]

# ---------------------------------------------------------------------------
# ROMFS error-path checks.
# ---------------------------------------------------------------------------

ROMFS_ERROR_CHECKS: dict[str, str] = {
    "cat_missing_file": r"cat: file not found",
    "wc_missing_file": r"wc: file not found",
    "grep_missing_file": r"grep: file not found",
}

# ---------------------------------------------------------------------------
# Resource-stability checks (boot-window tick messages).
# ---------------------------------------------------------------------------

TICK_FREE_PAGES_RE = re.compile(r"wind: tick=\d+ .*free_pages=(\d+)/(\d+)")

# Max acceptable drift in free-page count across captured boot-window ticks.
MAX_PAGE_DRIFT = 2


# ---------------------------------------------------------------------------
# Checker implementation.
# ---------------------------------------------------------------------------

def _check_boot_messages(text: str) -> list[str]:
    failures: list[str] = []
    for section, patterns in BOOT_REQUIRED.items():
        for pat in patterns:
            if not re.search(pat, text, re.MULTILINE):
                failures.append(f"boot/{section}: missing /{pat}/")
    return failures


def _check_noise_absent(text: str) -> list[str]:
    failures: list[str] = []
    for pat in NOISE_FORBIDDEN:
        if re.search(pat, text, re.MULTILINE):
            failures.append(f"console_quiet: forbidden debug pattern found: /{pat}/")
    return failures


def _check_shell_prompt(text: str) -> list[str]:
    if not SHELL_PROMPT_RE.search(text):
        return ["shell_prompt: '$ ' never appeared in log"]
    count = len(SHELL_PROMPT_RE.findall(text))
    if count < 2:
        return [f"shell_prompt_loop: only {count} prompt(s) seen; expected >=2 for loop check"]
    return []


def _check_commands(text: str) -> list[str]:
    """
    For each CommandCheck, if the trigger pattern is found in the log, verify
    that the expected_re also appears somewhere after the trigger line.
    """
    failures: list[str] = []
    lines = text.splitlines()
    for chk in COMMAND_CHECKS:
        trigger_line = None
        for i, line in enumerate(lines):
            if re.search(chk.trigger_re, line):
                trigger_line = i
                break

        if trigger_line is None:
            if not chk.optional:
                failures.append(f"command/{chk.name}: trigger /{chk.trigger_re}/ not found in log")
            continue

        # Search for expected output in the lines following the trigger.
        subsequent = "\n".join(lines[trigger_line + 1: trigger_line + 20])
        if not re.search(chk.expected_re, subsequent, re.MULTILINE):
            failures.append(
                f"command/{chk.name}: expected output /{chk.expected_re}/ "
                f"not found after trigger line {trigger_line + 1}"
            )
    return failures


def _check_error_paths(text: str) -> list[str]:
    """
    Check that error-path messages are present when commands that provoke them
    are visible in the log.  These are optional — only fail if the triggering
    command was run but the error message is missing.
    """
    failures: list[str] = []

    # Any unrecognised command → "sh: command not found"
    # Match a "$ <word>" line where the word is not one of the known commands.
    _known = {"echo", "ls", "cat", "wc", "grep", "mkdir", "rm", "shell"}
    for m in re.finditer(r"^\$ (\S+)", text, re.MULTILINE):
        cmd = m.group(1)
        if cmd not in _known:
            if not re.search(r"sh: command not found", text):
                failures.append(
                    f"error_path/unknown_cmd: ran '{cmd}' but 'sh: command not found' never appeared"
                )
            break  # one check is sufficient

    # "cat /nonexistent" → "cat: file not found"
    if re.search(r"^\$ cat /(?!etc/motd\b)\S+", text, re.MULTILINE):
        if not re.search(ROMFS_ERROR_CHECKS["cat_missing_file"], text):
            failures.append("error_path/cat_missing: 'cat: file not found' not seen after cat of non-existent path")

    # "wc /nonexistent" → "wc: file not found"
    if re.search(r"^\$ wc /(?!etc/motd\b)\S+", text, re.MULTILINE):
        if not re.search(ROMFS_ERROR_CHECKS["wc_missing_file"], text):
            failures.append("error_path/wc_missing: 'wc: file not found' not seen after wc of non-existent path")

    # "grep pat /nonexistent" → "grep: file not found"
    if re.search(r"^\$ grep \S+ /(?!etc/motd\b)\S+", text, re.MULTILINE):
        if not re.search(ROMFS_ERROR_CHECKS["grep_missing_file"], text):
            failures.append("error_path/grep_missing: 'grep: file not found' not seen after grep of non-existent file")

    return failures


def _check_resource_stability(text: str) -> list[str]:
    failures: list[str] = []
    samples = [(int(free), int(total)) for free, total in TICK_FREE_PAGES_RE.findall(text)]

    if not samples:
        # Tick messages are only emitted during the 10-second boot window; not
        # a hard failure if the log was captured after that window.
        return []

    totals = {t for _, t in samples}
    if len(totals) != 1:
        failures.append(
            f"resource/total_pages_changed: total page count changed: {sorted(totals)}"
        )

    free_vals = [f for f, _ in samples]
    drift = max(free_vals) - min(free_vals)
    if drift > MAX_PAGE_DRIFT:
        failures.append(
            f"resource/free_page_drift: drift={drift} exceeds max={MAX_PAGE_DRIFT} "
            f"(min={min(free_vals)}, max={max(free_vals)})"
        )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Phase 8 regression checker for ESP32-S3 xv6-shell logs."
    )
    parser.add_argument("log_file", help="Path to captured serial log output")
    args = parser.parse_args()

    log_path = pathlib.Path(args.log_file)
    if not log_path.exists():
        print(f"FAIL: log file not found: {log_path}")
        return 1

    text = log_path.read_text(encoding="utf-8", errors="ignore")

    failures: list[str] = []
    failures.extend(_check_boot_messages(text))
    failures.extend(_check_noise_absent(text))
    failures.extend(_check_shell_prompt(text))
    failures.extend(_check_commands(text))
    failures.extend(_check_error_paths(text))
    failures.extend(_check_resource_stability(text))

    if failures:
        print("Phase 8 regression: FAIL")
        for f in failures:
            print(f"  - {f}")
        return 1

    print("Phase 8 regression: PASS")
    print("  - boot sequence quiet and correct")
    print("  - no per-syscall/scheduler debug noise on console")
    print("  - shell prompt loop observed")
    print("  - command output and error paths verified")
    print("  - resource stability within bounds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
