#!/usr/bin/env python3

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from compile_qm import compile_one
from qt_linguist import (
    DEFAULT_SOURCE_DIRS,
    DEFAULT_TS_DIR,
    collect_ts_files,
    find_lrelease,
    find_lupdate,
    resolve_source_dirs,
    run_command,
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Update Qt .ts files with lupdate, then compile them into .qm files."
    )
    parser.add_argument(
        "--source-dir",
        action="append",
        type=Path,
        dest="source_dirs",
        help="Source directory to scan for translatable strings. Repeatable.",
    )
    parser.add_argument(
        "--ts-dir",
        type=Path,
        default=DEFAULT_TS_DIR,
        help=f"Directory containing .ts files. Default: {DEFAULT_TS_DIR}",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory for .qm files. Default: same as --ts-dir",
    )
    parser.add_argument(
        "--keep-obsolete",
        action="store_true",
        help="Keep obsolete entries in .ts files. Default behavior removes them.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print resolved commands before execution.",
    )
    return parser.parse_args()


def run_lupdate(
    lupdate: Path,
    source_dirs: list[Path],
    ts_files: list[Path],
    keep_obsolete: bool,
    verbose: bool,
) -> None:
    command = [str(lupdate)]
    command.extend(str(path) for path in source_dirs)
    command.extend(["-ts", *[str(path) for path in ts_files]])
    if not keep_obsolete:
        command.append("-no-obsolete")

    completed = run_command(command, verbose)
    if completed.returncode != 0:
        if completed.stderr:
            print(completed.stderr.rstrip(), file=sys.stderr)
        raise RuntimeError("lupdate failed")
    if completed.stderr:
        print(completed.stderr.rstrip(), file=sys.stderr)


def main() -> int:
    args = parse_args()
    ts_dir = args.ts_dir.expanduser().resolve()
    out_dir = args.out_dir.expanduser().resolve() if args.out_dir else ts_dir
    source_dirs = resolve_source_dirs(args.source_dirs or DEFAULT_SOURCE_DIRS)

    try:
        lupdate = find_lupdate()
        lrelease = find_lrelease()
        ts_files = collect_ts_files(ts_dir)

        print(f"Using lupdate: {lupdate}")
        print(f"Using lrelease: {lrelease}")
        run_lupdate(lupdate, source_dirs, ts_files, args.keep_obsolete, args.verbose)

        for ts_file in ts_files:
            qm_file = out_dir / f"{ts_file.stem}.qm"
            compile_one(lrelease, ts_file, qm_file, args.verbose)
            print(f"Generated: {qm_file}")
        return 0
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())