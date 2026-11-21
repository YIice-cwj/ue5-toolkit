#!/usr/bin/env python3
"""
check_diff_scope.py: 代码审查前置 Diff 范围分析、PR 拆分门禁与增量静态合规扫描工具。

零三方依赖，服务于 acs-code-review 五阶审查流水线：
1. 统计变更文件数与净增删行数，校验 PR 拆分阈值（默认 <= 15 文件、<= 400 变更行）；
2. 拦截误入变更的临时探针脚本、敏感凭据文件与超大二进制文件；
3. 仅针对本次 Diff 触及的源码文件与新增行定向执行 lint_rules 静态合规扫描。
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import lint_rules

DEFAULT_MAX_FILES = 15
DEFAULT_MAX_LINES = 400
DEFAULT_MAX_BINARY_BYTES = 1024 * 1024

HUNK_HEADER_RE = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@")
SENSITIVE_NAMES: frozenset[str] = frozenset({
    ".env",
    ".env.local",
    ".env.production",
    ".env.staging",
    "id_rsa",
    "id_ed25519",
    "credentials.json",
})
SENSITIVE_SUFFIXES: frozenset[str] = frozenset({".pem", ".key", ".p12", ".pfx"})
PROBE_PREFIXES: tuple[str, ...] = ("tmp_", "debug_", "probe_", "scratch_")


@dataclass(frozen=True)
class DiffSummary:
    """
    Git Diff 统计与风险汇总结果。

    Attributes:
        changed_files: 变更文件路径列表（相对仓库根）。
        added_lines_by_file: 每个文件对应的新增行号集合。
        total_added: 总新增行数。
        total_deleted: 总删除行数。
        warnings: 非阻断性治理提醒（如超 PR 拆分阈值）。
        blockers: 阻断级违规项（敏感文件、探针残留、新增行合规违规）。
    """

    changed_files: list[str]
    added_lines_by_file: dict[str, set[int]]
    total_added: int
    total_deleted: int
    warnings: list[str]
    blockers: list[str]


def parse_unified_diff(diff_text: str) -> tuple[list[str], dict[str, set[int]], int, int]:
    """
    解析 unified diff 文本，提取变更文件清单、各文件新增行号集合及增删行数。

    Args:
        diff_text: git diff --unified=0 输出的纯文本。

    Returns:
        四元组 (变更文件列表, {相对路径: 新增行号集合}, 总新增行数, 总删除行数)。
    """
    changed_files: list[str] = []
    added_by_file: dict[str, set[int]] = {}
    current_file: str | None = None
    current_line = 0
    total_added = 0
    total_deleted = 0

    for line in diff_text.splitlines():
        if line.startswith("+++ "):
            target = line[4:].strip()
            if target == "/dev/null":
                current_file = None
                continue
            clean = target[2:] if target.startswith("b/") else target
            current_file = clean
            if clean not in added_by_file:
                added_by_file[clean] = set()
                changed_files.append(clean)
            continue

        if line.startswith("--- "):
            continue

        if line.startswith("@@ "):
            match = HUNK_HEADER_RE.match(line)
            if match:
                current_line = int(match.group(1))
            continue

        if line.startswith("-"):
            total_deleted += 1
            continue

        if current_file is None:
            continue

        if line.startswith("+"):
            added_by_file[current_file].add(current_line)
            current_line += 1
            total_added += 1

    return changed_files, added_by_file, total_added, total_deleted


def collect_git_diff(repo_root: Path, staged: bool, base_ref: str | None) -> str:
    """
    通过 git 命令采集目标范围的 unified diff 文本。

    Args:
        repo_root: Git 仓库根目录路径。
        staged: 是否仅检查暂存区变更。
        base_ref: 可选基线分支或提交引用（如 origin/main）。

    Returns:
        git diff 标准输出字符串。

    Raises:
        RuntimeError: 当 git diff 执行失败时抛出。
    """
    cmd = ["git", "diff", "--unified=0", "--no-color"]
    if staged:
        cmd.append("--cached")
    elif base_ref:
        cmd.append(f"{base_ref}...HEAD")

    proc = subprocess.run(
        cmd,
        cwd=str(repo_root),
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"git diff 执行失败: {proc.stderr.strip()}")
    return proc.stdout


def analyze_diff(
    repo_root: Path,
    diff_text: str,
    max_files: int = DEFAULT_MAX_FILES,
    max_lines: int = DEFAULT_MAX_LINES,
    max_binary_bytes: int = DEFAULT_MAX_BINARY_BYTES,
) -> DiffSummary:
    """
    对给定 diff 文本执行 PR 体积检查、高危文件拦截与新增行定向 lint_rules 扫描。

    Args:
        repo_root: 仓库根目录路径。
        diff_text: unified diff 文本。
        max_files: 建议单 PR 最大文件数阈值。
        max_lines: 建议单 PR 最大变更行数（新增+删除）阈值。
        max_binary_bytes: 非文本大文件告警字节阈值。

    Returns:
        结构化 DiffSummary 检查报告。
    """
    resolved_root = repo_root.resolve()
    changed_files, added_by_file, total_added, total_deleted = parse_unified_diff(diff_text)
    warnings: list[str] = []
    blockers: list[str] = []

    if len(changed_files) > max_files:
        warnings.append(f"[PR-SPLIT] 变更文件数 {len(changed_files)} 超过建议阈值 {max_files}，建议拆分为更小 PR")

    churn = total_added + total_deleted
    if churn > max_lines:
        warnings.append(f"[PR-SPLIT] 变更总行数 {churn} (+{total_added}/-{total_deleted}) 超过建议阈值 {max_lines}")

    for rel_str in changed_files:
        rel_path = Path(rel_str)
        name_lower = rel_path.name.lower()
        suffix_lower = rel_path.suffix.lower()

        if name_lower in SENSITIVE_NAMES or suffix_lower in SENSITIVE_SUFFIXES:
            blockers.append(f"[B-SEC-FILE] 禁止提交敏感/密钥文件: {rel_str}")

        if name_lower.startswith(PROBE_PREFIXES) or name_lower == ".ds_store":
            blockers.append(f"[B-SEC-PROBE] 检测到临时探针或系统垃圾文件: {rel_str}")

        abs_path = (resolved_root / rel_path).resolve()
        if not abs_path.is_file():
            continue

        if abs_path.stat().st_size > max_binary_bytes and suffix_lower not in lint_rules.SCANNABLE_EXTS and suffix_lower != ".md":
            warnings.append(f"[LARGE-FILE] 文件体积超过 {max_binary_bytes} bytes，请确认是否应纳入 Git LFS: {rel_str}")

        if suffix_lower in lint_rules.SCANNABLE_EXTS:
            added_lines = added_by_file.get(rel_str, set())
            for issue in lint_rules.scan_file(abs_path):
                if not added_lines or issue.line_number in added_lines:
                    blockers.append(
                        f"[{issue.rule_id}] {rel_str}:{issue.line_number} {issue.name} - {issue.desc}"
                    )

    return DiffSummary(
        changed_files=changed_files,
        added_lines_by_file=added_by_file,
        total_added=total_added,
        total_deleted=total_deleted,
        warnings=warnings,
        blockers=blockers,
    )


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，采集并分析 Git Diff 范围及增量合规风险。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        无阻断违规返回 0，存在阻断违规或执行异常返回 1。
    """
    parser = argparse.ArgumentParser(description="PR/Diff 审查范围分析与增量合规扫描")
    parser.add_argument("repo", nargs="?", type=Path, default=Path.cwd(), help="目标 Git 仓库路径")
    parser.add_argument("--staged", action="store_true", help="仅检查暂存区变更 (git diff --cached)")
    parser.add_argument("--base", type=str, default=None, help="对比基线分支或提交 (如 main)")
    parser.add_argument("--max-files", type=int, default=DEFAULT_MAX_FILES, help="PR 最大建议文件数")
    parser.add_argument("--max-lines", type=int, default=DEFAULT_MAX_LINES, help="PR 最大建议增删行数")
    parsed = parser.parse_args(argv)

    try:
        diff_text = collect_git_diff(parsed.repo, parsed.staged, parsed.base)
    except RuntimeError as exc:
        print(f"❌ {exc}", file=sys.stderr)
        return 1

    summary = analyze_diff(parsed.repo, diff_text, parsed.max_files, parsed.max_lines)
    print(
        f"🔍 Diff 范围统计: {len(summary.changed_files)} 个文件，"
        f"+{summary.total_added} / -{summary.total_deleted} 行。"
    )

    if summary.warnings:
        print(f"\n⚠️  治理提醒 ({len(summary.warnings)}):")
        for warn in summary.warnings:
            print(f"  - {warn}")

    if summary.blockers:
        print(f"\n❌ 阻断违规 ({len(summary.blockers)}):")
        for item in summary.blockers:
            print(f"  - {item}")
        return 1

    print("✅ Diff 增量合规检查通过，零阻断项。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
