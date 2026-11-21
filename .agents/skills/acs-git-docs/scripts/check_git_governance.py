#!/usr/bin/env python3
"""
check_git_governance.py: Conventional Commits、Git LFS 二进制资产与 ADR 不可变治理检查工具。

零三方依赖，服务于 acs-git-docs：
1. 校验单条或区间提交信息严格符合 Conventional Commits 契约；
2. 校验 docs/adr/ 下 ADR 编号、必填元信息与核心章节完整性，防止已 Accepted ADR 被篡改；
3. 检查超阈值二进制资产是否已在 .gitattributes 中声明 Git LFS 跟踪。
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from pathlib import Path

COMMIT_HEADER_RE = re.compile(
    r"^(feat|fix|docs|style|refactor|perf|test|build|ci|chore|revert)"
    r"(?:\([a-z0-9][a-z0-9._/-]*\))?!?:\s+(.+)$"
)
GARBAGE_SUBJECTS: frozenset[str] = frozenset({
    "fix",
    "update",
    "wip",
    "test",
    "temp",
    "tmp",
    "misc",
    "changes",
    "修改",
    "更新",
    "测试",
})

ADR_FILENAME_RE = re.compile(r"^(\d{4})-[a-z0-9][a-z0-9-]*\.md$")
ADR_REQUIRED_SECTIONS: tuple[str, ...] = (
    "## Context",
    "## Decision",
    "## Alternatives Considered",
    "## Consequences",
)
ADR_STATUS_RE = re.compile(r"^\s*-\s*\*\*Status\*\*:\s*`?([A-Za-z]+)`?", re.MULTILINE)
ADR_DATE_RE = re.compile(r"^\s*-\s*\*\*Date\*\*:\s*`?(\d{4}-\d{2}-\d{2})`?", re.MULTILINE)
ADR_VALID_STATUSES: frozenset[str] = frozenset({"Proposed", "Accepted", "Superseded", "Deprecated"})

BINARY_ASSET_EXTS: frozenset[str] = frozenset({
    ".uasset",
    ".umap",
    ".fbx",
    ".obj",
    ".psd",
    ".png",
    ".jpg",
    ".jpeg",
    ".tga",
    ".exr",
    ".mp4",
    ".mov",
    ".wav",
    ".ogg",
    ".mp3",
    ".zip",
    ".7z",
    ".tar",
    ".gz",
    ".bin",
    ".pak",
    ".dll",
    ".so",
    ".dylib",
})


def validate_commit_message(message: str) -> list[str]:
    """
    校验单条提交信息是否符合 Conventional Commits 规范。

    Args:
        message: 完整提交信息文本。

    Returns:
        违规说明列表；合规则返回空列表。
    """
    lines = [line for line in message.splitlines() if not line.startswith("#")]
    if not lines or not lines[0].strip():
        return ["[COMMIT-EMPTY] 提交信息首行不能为空"]

    header = lines[0].strip()
    if header.startswith(("Merge ", "Revert ")):
        return []

    errors: list[str] = []
    match = COMMIT_HEADER_RE.match(header)
    if not match:
        return [f"[COMMIT-FMT] 提交信息不符合 Conventional Commits 格式: '{header}'"]

    subject = match.group(2).strip()
    if len(header) > 72:
        errors.append(f"[COMMIT-LEN] 提交标题长度 {len(header)} 超过 72 字符上限")
    if subject.lower() in GARBAGE_SUBJECTS or len(subject) < 4:
        errors.append(f"[COMMIT-SUBJECT] 提交摘要过于空洞: '{subject}'")
    return errors


def validate_adrs(repo_root: Path) -> tuple[int, list[str]]:
    """
    校验仓库 docs/adr/ 目录下全部 ADR 文件的命名、编号唯一性与必需章节。

    Args:
        repo_root: 仓库根目录路径。

    Returns:
        二元组 (检查的 ADR 文件数, 违规消息列表)。
    """
    adr_dir = repo_root / "docs" / "adr"
    if not adr_dir.is_dir():
        return 0, []

    adr_files = sorted(p for p in adr_dir.glob("*.md") if p.name.lower() != "readme.md")
    errors: list[str] = []
    seen_numbers: dict[str, str] = {}

    for adr_path in adr_files:
        match = ADR_FILENAME_RE.match(adr_path.name)
        if not match:
            errors.append(f"[ADR-NAME] ADR 文件名不符合 NNNN-kebab-title.md: {adr_path.name}")
            continue

        number = match.group(1)
        if number in seen_numbers:
            errors.append(f"[ADR-DUP] ADR 编号 {number} 重复: {seen_numbers[number]} 与 {adr_path.name}")
        else:
            seen_numbers[number] = adr_path.name

        content = adr_path.read_text(encoding="utf-8")
        status_match = ADR_STATUS_RE.search(content)
        if not status_match or status_match.group(1) not in ADR_VALID_STATUSES:
            errors.append(f"[ADR-STATUS] {adr_path.name} 缺少合法 Status（Proposed/Accepted/Superseded/Deprecated）")

        if not ADR_DATE_RE.search(content):
            errors.append(f"[ADR-DATE] {adr_path.name} 缺少 YYYY-MM-DD 格式 Date")

        for section in ADR_REQUIRED_SECTIONS:
            if section not in content:
                errors.append(f"[ADR-SECTION] {adr_path.name} 缺少必需章节 '{section}'")

    return len(adr_files), errors


def check_accepted_adr_immutability(repo_root: Path, base_ref: str) -> list[str]:
    """
    检查相对于 base_ref 是否有已处于 Accepted 状态的 ADR 被修改（R-DOC-ADR-IMMUT）。

    Args:
        repo_root: Git 仓库根目录路径。
        base_ref: 对比基线引用（如 HEAD 或 origin/main）。

    Returns:
        违规篡改列表。
    """
    proc = subprocess.run(
        ["git", "diff", "--name-status", base_ref, "--", "docs/adr"],
        cwd=str(repo_root),
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        return [f"[ADR-GIT-ERR] 无法检查 ADR 历史变更: {proc.stderr.strip()}"]

    errors: list[str] = []
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 2 or not parts[0].startswith("M"):
            continue
        rel_path = parts[1]
        show_proc = subprocess.run(
            ["git", "show", f"{base_ref}:{rel_path}"],
            cwd=str(repo_root),
            capture_output=True,
            text=True,
            check=False,
        )
        if show_proc.returncode == 0:
            status_match = ADR_STATUS_RE.search(show_proc.stdout)
            if status_match and status_match.group(1) == "Accepted":
                errors.append(
                    f"[R-DOC-ADR-IMMUT] 已 Accepted 的 {rel_path} 严禁原地改写，请新建 ADR 并标注 Supersedes"
                )
    return errors


def load_lfs_patterns(repo_root: Path) -> list[str]:
    """
    解析仓库 .gitattributes 中声明 filter=lfs 的路径通配规则列表。

    Args:
        repo_root: 仓库根目录路径。

    Returns:
        配置了 filter=lfs 的匹配模式列表。
    """
    attrs = repo_root / ".gitattributes"
    if not attrs.is_file():
        return []

    patterns: list[str] = []
    for raw in attrs.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) >= 2 and "filter=lfs" in parts[1:]:
            patterns.append(parts[0])
    return patterns


def check_lfs_assets(repo_root: Path, candidate_files: list[Path], max_binary_kb: int = 512) -> list[str]:
    """
    检查候选文件集合中是否存在超阈值且未配置 Git LFS 的二进制大资产。

    Args:
        repo_root: 仓库根目录路径。
        candidate_files: 待检查的文件路径列表。
        max_binary_kb: 二进制大文件告警阈值（KB）。

    Returns:
        漏配 Git LFS 的违规消息列表。
    """
    resolved_root = repo_root.resolve()
    lfs_patterns = load_lfs_patterns(resolved_root)
    threshold_bytes = max_binary_kb * 1024
    errors: list[str] = []

    for path in candidate_files:
        abs_path = path.resolve()
        if not abs_path.is_file():
            continue
        if abs_path.suffix.lower() not in BINARY_ASSET_EXTS:
            continue
        size = abs_path.stat().st_size
        if size <= threshold_bytes:
            continue
        rel_str = str(abs_path.relative_to(resolved_root))
        matched = any(
            fnmatch.fnmatch(rel_str, pat) or fnmatch.fnmatch(abs_path.name, pat)
            for pat in lfs_patterns
        )
        if not matched:
            errors.append(
                f"[GIT-LFS] 二进制资产 {rel_str} ({size // 1024} KB) 超过 {max_binary_kb} KB 且未配置 .gitattributes filter=lfs"
            )
    return errors


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，按参数组合执行提交信息、ADR 规范及 Git LFS 资产治理检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        全部通过返回 0，发现违规返回 1。
    """
    parser = argparse.ArgumentParser(description="Git 提交信息、ADR 与 LFS 治理检查工具")
    parser.add_argument("repo", nargs="?", type=Path, default=Path.cwd(), help="目标仓库根目录")
    parser.add_argument("--commit-msg", type=str, default=None, help="直接校验指定提交信息字符串")
    parser.add_argument("--commit-msg-file", type=Path, default=None, help="校验提交信息文件 (如 commit-msg Hook)")
    parser.add_argument("--check-adr", action="store_true", help="校验 docs/adr/ 下全部 ADR 合规性")
    parser.add_argument("--adr-base", type=str, default=None, help="校验 Accepted ADR 相对指定基线未被改写")
    parser.add_argument("--check-lfs", action="store_true", help="校验当前仓库大二进制文件是否配置 Git LFS")
    parser.add_argument("--max-binary-kb", type=int, default=512, help="二进制资产强制 LFS 阈值 (KB)")
    parsed = parser.parse_args(argv)

    run_default = not (parsed.commit_msg or parsed.commit_msg_file or parsed.check_adr or parsed.adr_base or parsed.check_lfs)
    errors: list[str] = []

    if parsed.commit_msg is not None:
        errors.extend(validate_commit_message(parsed.commit_msg))
    if parsed.commit_msg_file is not None:
        errors.extend(validate_commit_message(parsed.commit_msg_file.read_text(encoding="utf-8")))

    if parsed.check_adr or run_default:
        adr_count, adr_errs = validate_adrs(parsed.repo)
        print(f"🔍 已校验 {adr_count} 份 ADR 文档。")
        errors.extend(adr_errs)

    if parsed.adr_base is not None:
        errors.extend(check_accepted_adr_immutability(parsed.repo, parsed.adr_base))

    if parsed.check_lfs or run_default:
        candidates = [
            p
            for p in parsed.repo.rglob("*")
            if p.is_file() and ".git" not in p.parts
        ]
        errors.extend(check_lfs_assets(parsed.repo, candidates, parsed.max_binary_kb))

    if errors:
        print(f"\n❌ 发现 {len(errors)} 处 Git/文档治理违规:")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("✅ Git 与文档治理检查全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
