#!/usr/bin/env python3
"""
check_workspace_hygiene.py: B-SEC 工作区洁净度与 B6 .memory/ 归档索引完整性检查工具。

零三方依赖，服务于 acs-core-workflow：
1. 检出遗留的临时排查探针脚本、备份残留与敏感密钥/环境文件；
2. 当工作区存在 .memory/ 目录时，校验 .memory/INDEX.md 存在且索引了全部快照文件。
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

IGNORE_DIRS: frozenset[str] = frozenset({
    ".git",
    ".venv",
    "venv",
    "node_modules",
    "build",
    "dist",
    "__pycache__",
    ".history",
    ".gemini",
    "Intermediate",
    "Binaries",
})

PROBE_PREFIXES: tuple[str, ...] = ("tmp_", "debug_", "probe_", "scratch_")
TEMP_SUFFIXES: frozenset[str] = frozenset({".bak", ".orig", ".swp", ".swo"})
SECRET_FILENAMES: frozenset[str] = frozenset({
    ".env",
    ".env.local",
    ".env.production",
    ".env.staging",
    "id_rsa",
    "id_ed25519",
    "credentials.json",
})
SECRET_SUFFIXES: frozenset[str] = frozenset({".pem", ".key", ".p12", ".pfx"})
MD_LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")


def check_stray_and_secret_files(root_dir: Path) -> list[str]:
    """
    递归扫描工作区中残留的临时探针文件与高危敏感文件。

    Args:
        root_dir: 待检查的工作区根目录路径。

    Returns:
        检出的违规路径说明列表。
    """
    resolved = root_dir.resolve()
    issues: list[str] = []

    for current, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            path = Path(current) / filename
            rel = str(path.relative_to(resolved))
            lower = filename.lower()
            suffix = path.suffix.lower()

            if lower in SECRET_FILENAMES or suffix in SECRET_SUFFIXES:
                issues.append(f"[B-SEC-SECRET] 发现高危密钥/环境文件，严禁遗留于代码工作区: {rel}")
            elif lower.startswith(PROBE_PREFIXES) or suffix in TEMP_SUFFIXES:
                issues.append(f"[B-SEC-HYGIENE] 发现未清理的临时探针或备份残留文件: {rel}")

    return issues


def check_memory_archive(root_dir: Path) -> list[str]:
    """
    校验 .memory/ 归档目录的总索引完整性（不存在 .memory/ 时直接放行）。

    Args:
        root_dir: 工作区根目录路径。

    Returns:
        归档索引违规列表。
    """
    memory_dir = root_dir.resolve() / ".memory"
    if not memory_dir.is_dir():
        return []

    index_file = memory_dir / "INDEX.md"
    if not index_file.is_file():
        return ["[B6-INDEX-MISSING] 存在 .memory/ 目录但缺少总索引文件 .memory/INDEX.md"]

    content = index_file.read_text(encoding="utf-8")
    if not content.strip():
        return ["[B6-INDEX-EMPTY] .memory/INDEX.md 内容为空"]

    issues: list[str] = []
    linked_targets: set[Path] = set()
    for link in MD_LINK_RE.findall(content):
        if link.startswith(("http://", "https://", "#")):
            continue
        clean = link.split("#", 1)[0]
        if not clean:
            continue
        target = (memory_dir / clean).resolve()
        linked_targets.add(target)
        if not target.exists():
            issues.append(f"[B6-BROKEN-LINK] .memory/INDEX.md 指向不存在的快照文件: {link}")

    for snapshot in sorted(memory_dir.rglob("*.md")):
        resolved_snap = snapshot.resolve()
        if resolved_snap == index_file.resolve():
            continue
        if resolved_snap not in linked_targets:
            rel_snap = snapshot.relative_to(memory_dir)
            issues.append(f"[B6-UNINDEXED] 记忆快照未在 .memory/INDEX.md 中登记: {rel_snap}")

    return issues


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行工作区洁净度与记忆归档完整性检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        工作区洁净返回 0，发现违规返回 1。
    """
    parser = argparse.ArgumentParser(description="B-SEC 工作区洁净度与 B6 .memory/ 归档索引检查")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="工作区根目录路径")
    parsed = parser.parse_args(argv)

    issues = check_stray_and_secret_files(parsed.target)
    issues.extend(check_memory_archive(parsed.target))

    if issues:
        print(f"❌ 发现 {len(issues)} 处工作区洁净度或归档问题:")
        for item in issues:
            print(f"  - {item}")
        return 1

    print("✅ 工作区洁净度与归档索引检查通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
