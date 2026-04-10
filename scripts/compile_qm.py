#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path

from qt_linguist import DEFAULT_TS_DIR, collect_ts_files, find_lrelease, run_command


def compile_one(lrelease: Path, ts_file: Path, qm_file: Path, verbose: bool) -> None:
    qm_file.parent.mkdir(parents=True, exist_ok=True)
    command = [str(lrelease), str(ts_file), "-qm", str(qm_file)]
    completed = run_command(command, verbose)
    if completed.returncode != 0:
        if completed.stderr:
            print(completed.stderr.rstrip(), file=sys.stderr)
        raise RuntimeError(f"lrelease failed for {ts_file.name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compile Qt .ts files into .qm files.")
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
        "--verbose",
        action="store_true",
        help="Print resolved commands before execution.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    ts_dir = args.ts_dir.expanduser().resolve()
    out_dir = args.out_dir.expanduser().resolve() if args.out_dir else ts_dir

    try:
        lrelease = find_lrelease()
        ts_files = collect_ts_files(ts_dir)
        print(f"Using lrelease: {lrelease}")
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