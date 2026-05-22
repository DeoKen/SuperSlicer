#!/usr/bin/env python3
"""
Slice a directory of 3MF projects with two Slic3r_console executables and compare
the produced G-code byte-for-byte.

The utility is meant for regression checks during refactors:

    python tests/slice_regression_compare.py \
        --exe-a C:/old/Slic3r_console.exe \
        --exe-b C:/new/Slic3r_console.exe \
        --input-dir C:/projects/3mf \
        --output-dir C:/tmp/slice-regression

Each .3mf is sliced twice with different output filenames. Mismatches and slicing
failures are appended to a TSV log, while every project also gets a concise status
line on stdout so a long run can be watched live.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


def suppress_child_crash_dialogs() -> None:
    """Keep crashing slicer subprocesses from opening Windows error dialogs."""
    if sys.platform != "win32":
        return

    import ctypes

    # Child processes inherit the process error mode. Without this, a crashing
    # Slic3r_console.exe may open a modal "application error" dialog and stall a
    # long regression run until somebody clicks OK.
    sem_failcriticalerrors = 0x0001
    sem_nogpfault_errorbox = 0x0002
    sem_noopenfile_errorbox = 0x8000
    ctypes.windll.kernel32.SetErrorMode(
        sem_failcriticalerrors | sem_nogpfault_errorbox | sem_noopenfile_errorbox
    )


@dataclass
class SliceRun:
    exe: Path
    gcode: Path
    seconds: float
    returncode: int
    stdout: str
    stderr: str

    @property
    def ok(self) -> bool:
        return self.returncode == 0 and self.gcode.is_file()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Slice every .3mf in a directory with two slicers and compare the G-code outputs."
    )
    parser.add_argument("--exe-a", required=True, type=Path, help="First Slic3r_console.exe path, usually the baseline.")
    parser.add_argument("--exe-b", required=True, type=Path, help="Second Slic3r_console.exe path, usually the reworked build.")
    parser.add_argument("--input-dir", required=True, type=Path, help="Directory containing .3mf projects, recursively.")
    parser.add_argument("--output-dir", required=True, type=Path, help="Directory where generated G-code files are written.")
    parser.add_argument(
        "--log",
        type=Path,
        default=None,
        help="TSV log for mismatches/failures. Default: <output-dir>/slice_regression_errors.tsv.",
    )
    parser.add_argument(
        "--extra-arg",
        action="append",
        default=[],
        help="Extra argument passed to both slicers. Repeat for multiple arguments.",
    )
    parser.add_argument("--limit", type=int, default=0, help="Slice at most N projects. Useful for smoke tests.")
    parser.add_argument("--threads", type=int, default=0, help="Pass --threads N to each slicer when N > 0.")
    return parser.parse_args()


def validate_path(path: Path, label: str, executable: bool = False) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.exists():
        raise SystemExit(f"{label} does not exist: {resolved}")
    if executable and not resolved.is_file():
        raise SystemExit(f"{label} is not a file: {resolved}")
    return resolved


def find_projects(input_dir: Path, limit: int) -> list[Path]:
    projects = sorted(path for path in input_dir.rglob("*.3mf") if path.is_file())
    if limit > 0:
        projects = projects[:limit]
    return projects


def output_paths(project: Path, input_dir: Path, output_dir: Path) -> tuple[Path, Path]:
    rel = project.relative_to(input_dir)
    stem = rel.with_suffix("")
    project_output_dir = output_dir / stem.parent
    project_output_dir.mkdir(parents=True, exist_ok=True)
    return (
        project_output_dir / f"{stem.name}.a.gcode",
        project_output_dir / f"{stem.name}.b.gcode",
    )


def slicer_command(exe: Path, project: Path, output: Path, extra_args: Sequence[str], threads: int) -> list[str]:
    command = [
        str(exe),
        "--export-gcode",
        "--output",
        str(output),
    ]
    if threads > 0:
        command.extend(["--threads", str(threads)])
    command.extend(extra_args)
    command.append(str(project))
    return command


def run_slicer(exe: Path, project: Path, output: Path, extra_args: Sequence[str], threads: int) -> SliceRun:
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    command = slicer_command(exe, project, output, extra_args, threads)
    #print("COMMAND:")
    #print(command);
    start = time.perf_counter()
    completed = subprocess.run(command, text=True, capture_output=True)
    seconds = time.perf_counter() - start
    return SliceRun(
        exe=exe,
        gcode=output,
        seconds=seconds,
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
    )


def one_line(text: str, max_len: int = 500) -> str:
    compact = " ".join(text.split())
    if len(compact) <= max_len:
        return compact
    return compact[: max_len - 3] + "..."


def write_log_header(log_path: Path) -> None:
    if log_path.exists() and log_path.stat().st_size > 0:
        return
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_path.write_text(
        "status\tproject\texe_a_seconds\texe_b_seconds\texe_a_returncode\texe_b_returncode\tgcode_a\tgcode_b\tdetails\n",
        encoding="utf-8",
    )


def append_error(log_path: Path, status: str, project: Path, run_a: SliceRun, run_b: SliceRun, details: str) -> None:
    with log_path.open("a", encoding="utf-8", newline="") as log:
        log.write(
            "\t".join(
                [
                    status,
                    str(project),
                    f"{run_a.seconds:.3f}",
                    f"{run_b.seconds:.3f}",
                    str(run_a.returncode),
                    str(run_b.returncode),
                    str(run_a.gcode),
                    str(run_b.gcode),
                    details.replace("\t", " "),
                ]
            )
            + "\n"
        )


def gcode_files_match(gcode_a: Path, gcode_b: Path) -> bool:
    """Compare generated G-code while ignoring the first line.

    The first line contains the slicing time, so it is expected to differ even
    when the generated toolpaths are otherwise identical.
    """
    with gcode_a.open("rb") as file_a, gcode_b.open("rb") as file_b:
        file_a.readline()
        file_b.readline()

        while True:
            chunk_a = file_a.read(1024 * 1024)
            chunk_b = file_b.read(1024 * 1024)
            if chunk_a != chunk_b:
                return False
            if not chunk_a:
                return True


def compare_project(
    project: Path,
    input_dir: Path,
    output_dir: Path,
    exe_a: Path,
    exe_b: Path,
    extra_args: Sequence[str],
    threads: int,
    log_path: Path,
) -> bool:
    gcode_a, gcode_b = output_paths(project, input_dir, output_dir)
    run_a = run_slicer(exe_a, project, gcode_a, extra_args, threads)
    run_b = run_slicer(exe_b, project, gcode_b, extra_args, threads)

    rel = project.relative_to(input_dir)
    if not run_a.ok or not run_b.ok:
        details = (
            f"exe_a_stderr={one_line(run_a.stderr)}; "
            f"exe_b_stderr={one_line(run_b.stderr)}"
        )
        append_error(log_path, "SLICE_FAILED", project, run_a, run_b, details)
        print(
            f"FAIL slice {rel} "
            f"a={run_a.seconds:.2f}s rc={run_a.returncode} "
            f"b={run_b.seconds:.2f}s rc={run_b.returncode}",
            flush=True,
        )
        return False

    same = gcode_files_match(gcode_a, gcode_b)
    status = "OK" if same else "DIFF"
    print(f"{status} {rel} a={run_a.seconds:.2f}s b={run_b.seconds:.2f}s", flush=True)
    if not same:
        append_error(log_path, "GCODE_DIFF", project, run_a, run_b, "G-code files differ after first line")
    return same


def main() -> int:
    suppress_child_crash_dialogs()

    args = parse_args()
    exe_a = validate_path(args.exe_a, "--exe-a", executable=True)
    exe_b = validate_path(args.exe_b, "--exe-b", executable=True)
    input_dir = validate_path(args.input_dir, "--input-dir")
    output_dir = args.output_dir.expanduser().resolve()
    log_path = (args.log or (output_dir / "slice_regression_errors.tsv")).expanduser().resolve()

    projects = find_projects(input_dir, args.limit)
    output_dir.mkdir(parents=True, exist_ok=True)
    write_log_header(log_path)

    print(f"Found {len(projects)} project(s).", flush=True)
    ok_count = 0
    for idx, project in enumerate(projects, start=1):
        print(f"[{idx}/{len(projects)}] {project}", flush=True)
        if compare_project(project, input_dir, output_dir, exe_a, exe_b, args.extra_arg, args.threads, log_path):
            ok_count += 1

    failed_count = len(projects) - ok_count
    print(f"Done: {ok_count} OK, {failed_count} failed/different. Log: {log_path}", flush=True)
    return 0 if failed_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
