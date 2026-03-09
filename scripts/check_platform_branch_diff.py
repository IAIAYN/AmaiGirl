#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple

CPP_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}

PLATFORM_TOKEN: Dict[str, str] = {
    "windows": "Q_OS_WIN32",
    "linux": "Q_OS_LINUX",
    "macos": "Q_OS_MACOS",
}

ANY_PLATFORM_RE = re.compile(r"\bQ_OS_(WIN32|LINUX|MACOS)\b")
DIRECTIVE_RE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(?P<rest>.*)$")
HUNK_RE = re.compile(r"^@@\s+-(\d+)(?:,(\d+))?\s+\+(\d+)(?:,(\d+))?\s+@@")


@dataclass
class Frame:
    relevant: bool
    current_has_target: bool


def run_git(args: List[str]) -> str:
    result = subprocess.run(
        ["git", *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or f"git {' '.join(args)} failed")
    return result.stdout or ""



def changed_cpp_files(base: str, head: str) -> List[str]:
    output = run_git(["diff", "--name-only", f"{base}..{head}"])
    files: List[str] = []
    for line in output.splitlines():
        path = line.strip()
        if not path:
            continue
        if Path(path).suffix.lower() in CPP_EXTS:
            files.append(path)
    return files


def load_allowlist_patterns(allowlist_file: str | None) -> List[str]:
    if not allowlist_file:
        return []

    p = Path(allowlist_file)
    if not p.exists():
        raise RuntimeError(f"allowlist file does not exist: {allowlist_file}")

    patterns: List[str] = []
    for raw in p.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        patterns.append(line)
    return patterns


def is_whitelisted(path: str, patterns: List[str]) -> bool:
    normalized = path.replace("\\", "/")
    return any(fnmatch.fnmatch(normalized, pat) for pat in patterns)


def mentions_target_positive(expr: str, token: str) -> bool:
    return bool(re.search(rf"defined\s*\(\s*{re.escape(token)}\s*\)", expr))


def mentions_target_negative(expr: str, token: str) -> bool:
    return bool(re.search(rf"!\s*defined\s*\(\s*{re.escape(token)}\s*\)", expr))


def parse_directive(directive: str, rest: str, target_token: str) -> Tuple[bool, bool]:
    expr = rest.strip()

    if directive == "ifdef":
        token = expr.split()[0] if expr else ""
        relevant = bool(ANY_PLATFORM_RE.search(token))
        return relevant, token == target_token

    if directive == "ifndef":
        token = expr.split()[0] if expr else ""
        relevant = bool(ANY_PLATFORM_RE.search(token))
        return relevant, False if token == target_token else False

    if directive in ("if", "elif"):
        relevant = bool(ANY_PLATFORM_RE.search(expr))
        pos = mentions_target_positive(expr, target_token)
        neg = mentions_target_negative(expr, target_token)
        has_target = pos and not neg
        return relevant, has_target

    return False, False


def build_allowed_map(lines: List[str], target_token: str) -> List[bool]:
    allowed = [False] * (len(lines) + 1)
    stack: List[Frame] = []

    def in_target_branch() -> bool:
        if not stack:
            return False
        if any(frame.relevant and not frame.current_has_target for frame in stack):
            return False
        return any(frame.current_has_target for frame in stack)

    for line_no, line in enumerate(lines, start=1):
        allowed[line_no] = in_target_branch()

        m = DIRECTIVE_RE.match(line)
        if not m:
            continue

        directive = m.group(1)
        rest = m.group("rest")

        if directive in ("if", "ifdef", "ifndef"):
            relevant, has_target = parse_directive(directive, rest, target_token)
            stack.append(Frame(relevant=relevant, current_has_target=has_target))
            continue

        if directive == "elif":
            if stack:
                relevant, has_target = parse_directive(directive, rest, target_token)
                stack[-1] = Frame(relevant=relevant or stack[-1].relevant, current_has_target=has_target)
            continue

        if directive == "else":
            if stack:
                stack[-1] = Frame(relevant=stack[-1].relevant, current_has_target=False)
            continue

        if directive == "endif":
            if stack:
                stack.pop()

    return allowed


def parse_changed_lines(base: str, head: str, file_path: str) -> Tuple[List[int], List[int], Dict[int, str], Dict[int, str]]:
    diff = run_git(["diff", "--unified=0", f"{base}..{head}", "--", file_path])

    old_changed: List[int] = []
    new_changed: List[int] = []
    old_text: Dict[int, str] = {}
    new_text: Dict[int, str] = {}

    old_line = 0
    new_line = 0

    for raw in diff.splitlines():
        if raw.startswith("@@"):
            m = HUNK_RE.match(raw)
            if not m:
                continue
            old_line = int(m.group(1))
            new_line = int(m.group(3))
            continue

        if raw.startswith("---") or raw.startswith("+++"):
            continue

        if raw.startswith("-"):
            text = raw[1:]
            old_changed.append(old_line)
            old_text[old_line] = text
            old_line += 1
            continue

        if raw.startswith("+"):
            text = raw[1:]
            new_changed.append(new_line)
            new_text[new_line] = text
            new_line += 1
            continue

        if raw.startswith(" "):
            old_line += 1
            new_line += 1

    return old_changed, new_changed, old_text, new_text


def is_ignorable_changed_line(text: str) -> bool:
    stripped = text.strip()
    if not stripped:
        return True

    if stripped.startswith("#"):
        if re.match(r"^#\s*(if|ifdef|ifndef|elif|else|endif)\b", stripped):
            return True

    return False


def load_file_at(rev: str, path: str) -> List[str]:
    data = run_git(["show", f"{rev}:{path}"])
    return data.splitlines()


def main() -> int:
    parser = argparse.ArgumentParser(description="检查平台分支改动是否落在对应平台宏块内。")
    parser.add_argument("--platform", required=True, choices=["windows", "linux", "macos"])
    parser.add_argument("--base", required=True, help="Base commit SHA")
    parser.add_argument("--head", required=True, help="Head commit SHA")
    parser.add_argument(
        "--allow-glob",
        action="append",
        default=[],
        help="白名单路径 glob（可多次传入），命中的 C/C++ 文件将跳过守卫检查",
    )
    parser.add_argument(
        "--allowlist-file",
        default=None,
        help="白名单文件路径（每行一个 glob，支持 # 注释）",
    )
    args = parser.parse_args()

    target_token = PLATFORM_TOKEN[args.platform]
    file_patterns = load_allowlist_patterns(args.allowlist_file)
    allow_patterns = [*file_patterns, *args.allow_glob]

    files = changed_cpp_files(args.base, args.head)
    if not files:
        print("[check_platform_branch_diff] No C/C++ file changes detected.")
        return 0

    violations: List[str] = []

    for file_path in files:
        if is_whitelisted(file_path, allow_patterns):
            print(f"[check_platform_branch_diff] Skipped by allowlist: {file_path}")
            continue

        try:
            old_lines = load_file_at(args.base, file_path)
            new_lines = load_file_at(args.head, file_path)
        except RuntimeError:
            continue

        old_allowed = build_allowed_map(old_lines, target_token)
        new_allowed = build_allowed_map(new_lines, target_token)

        old_changed, new_changed, old_text, new_text = parse_changed_lines(args.base, args.head, file_path)

        for ln in old_changed:
            text = old_text.get(ln, "")
            if is_ignorable_changed_line(text):
                continue
            if ln <= 0 or ln >= len(old_allowed) or not old_allowed[ln]:
                violations.append(f"{file_path}:{ln} [removed] {text.strip()}")

        for ln in new_changed:
            text = new_text.get(ln, "")
            if is_ignorable_changed_line(text):
                continue
            if ln <= 0 or ln >= len(new_allowed) or not new_allowed[ln]:
                violations.append(f"{file_path}:{ln} [added] {text.strip()}")

    if violations:
        print("[check_platform_branch_diff] Detected shared/non-target code changes on platform feature branch:")
        print(f"  platform={args.platform}, required macro={target_token}")
        for item in violations[:200]:
            print(f"  - {item}")
        if len(violations) > 200:
            print(f"  ... and {len(violations) - 200} more")
        return 1

    print(
        f"[check_platform_branch_diff] Passed: all C/C++ changes are scoped to {target_token} guarded regions."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
