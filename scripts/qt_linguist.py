from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TS_DIR = PROJECT_ROOT / "res" / "i18n"
DEFAULT_SOURCE_DIRS = [
    PROJECT_ROOT / "src" / "app",
    PROJECT_ROOT / "src" / "ai",
    PROJECT_ROOT / "src" / "common",
    PROJECT_ROOT / "src" / "engine",
    PROJECT_ROOT / "src" / "ui",
]
COMMON_QT_ENV_KEYS = [
    "QT_HOST_BINS",
    "QTDIR",
    "QT_ROOT",
    "QT_HOST_PATH",
]


def candidate_tool_names(tool_name: str) -> list[str]:
    suffixes = ["", "-qt6", "6"]
    if os.name == "nt":
        return [f"{tool_name}{suffix}.exe" for suffix in suffixes]
    return [f"{tool_name}{suffix}" for suffix in suffixes]


def iter_env_tool_candidates(tool_name: str, env_keys: list[str]) -> list[Path]:
    names = candidate_tool_names(tool_name)
    candidates: list[Path] = []
    for key in env_keys:
        raw = os.environ.get(key, "").strip()
        if not raw:
            continue

        path = Path(raw).expanduser()
        if path.is_file():
            candidates.append(path)
            continue

        for name in names:
            candidates.append(path / name)
            candidates.append(path / "bin" / name)
    return candidates


def iter_common_tool_candidates(tool_name: str) -> list[Path]:
    names = candidate_tool_names(tool_name)
    home = Path.home()
    roots: list[Path] = []

    if sys.platform == "darwin":
        roots.extend([
            home / "Qt",
            Path("/Applications/Qt"),
        ])
    elif os.name == "nt":
        qt_dir = os.environ.get("ProgramFiles", r"C:\Program Files")
        roots.extend([
            home / "Qt",
            Path(qt_dir) / "Qt",
            Path("C:/Qt"),
        ])
    else:
        roots.extend([
            home / "Qt",
            Path("/opt/Qt"),
            Path("/usr/lib/qt6/bin"),
            Path("/usr/lib64/qt6/bin"),
            Path("/usr/local/qt6/bin"),
        ])

    candidates: list[Path] = []
    for root in roots:
        if root.is_file():
            candidates.append(root)
            continue
        if root.name == "bin" and root.exists():
            for name in names:
                candidates.append(root / name)
            continue
        if not root.exists():
            continue

        for name in names:
            candidates.extend(root.glob(f"**/{name}"))
    return candidates


def find_qt_tool(tool_name: str, env_keys: list[str]) -> Path:
    for name in candidate_tool_names(tool_name):
        resolved = shutil.which(name)
        if resolved:
            return Path(resolved)

    seen: set[Path] = set()
    for path in iter_env_tool_candidates(tool_name, env_keys) + iter_common_tool_candidates(tool_name):
        normalized = path.expanduser().resolve(strict=False)
        if normalized in seen:
            continue
        seen.add(normalized)
        if normalized.is_file():
            return normalized

    env_hint = " / ".join(env_keys)
    raise FileNotFoundError(
        f"Unable to find {tool_name}. Install Qt Linguist tools or set {env_hint}."
    )


def find_lrelease() -> Path:
    return find_qt_tool("lrelease", ["AMAI_LRELEASE", "QT_LRELEASE", *COMMON_QT_ENV_KEYS])


def find_lupdate() -> Path:
    return find_qt_tool("lupdate", ["AMAI_LUPDATE", "QT_LUPDATE", *COMMON_QT_ENV_KEYS])


def collect_ts_files(ts_dir: Path) -> list[Path]:
    if not ts_dir.exists():
        raise FileNotFoundError(f"TS directory does not exist: {ts_dir}")

    files = sorted(path for path in ts_dir.glob("*.ts") if path.is_file())
    if not files:
        raise FileNotFoundError(f"No .ts files found in: {ts_dir}")
    return files


def resolve_source_dirs(source_dirs: list[Path]) -> list[Path]:
    resolved = [path.expanduser().resolve() for path in source_dirs]
    missing = [path for path in resolved if not path.exists()]
    if missing:
        missing_text = ", ".join(str(path) for path in missing)
        raise FileNotFoundError(f"Source directories do not exist: {missing_text}")
    return resolved


def run_command(command: list[str], verbose: bool) -> subprocess.CompletedProcess[str]:
    if verbose:
        print("$", " ".join(command))
    completed = subprocess.run(command, capture_output=True, text=True)
    if completed.stdout:
        print(completed.stdout.rstrip())
    return completed
