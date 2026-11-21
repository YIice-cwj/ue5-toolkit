#!/usr/bin/env python3
"""
check_test_quality.py: B5 真测试契约（反滥竽充数测试）与 95% 覆盖率门禁核验脚本。

零三方依赖，服务于 acs-engineering-baseline：
1. 静态扫描 Python/JS/TS/C++ 测试文件，拦截 sleep 盲等、空洞断言、无断言测试函数与吞异常；
2. 标记 # pragma: no cover 使用位置供人工审查豁免；
3. 解析 Cobertura XML (--cov-xml) 与 LCOV (--lcov) 报告，校验行/分支覆盖率达标（默认 >= 95%）。
"""

from __future__ import annotations

import argparse
import ast
import os
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

IGNORE_DIRS: frozenset[str] = frozenset({
    ".git",
    ".venv",
    "venv",
    "node_modules",
    "build",
    "dist",
    "__pycache__",
    "Intermediate",
    "Binaries",
})

SLEEP_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("TEST-SLEEP-PY", re.compile(r"\b(?:time|asyncio)\.sleep\s*\(")),
    ("TEST-SLEEP-JS", re.compile(r"\bsetTimeout\s*\(")),
    ("TEST-SLEEP-CPP", re.compile(r"\bstd::this_thread::sleep_(?:for|until)\s*\(")),
)

HOLLOW_ASSERT_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    ("TEST-HOLLOW-PY", re.compile(r"^\s*assert\s+(?:True|1\s*==\s*1)\b")),
    ("TEST-HOLLOW-JS", re.compile(r"\bexpect\s*\(\s*true\s*\)\s*\.\s*toBe\s*\(\s*true\s*\)")),
    ("TEST-HOLLOW-CPP", re.compile(r"\b(?:EXPECT|ASSERT)_TRUE\s*\(\s*true\s*\)")),
)

PRAGMA_NO_COVER_RE = re.compile(r"#\s*pragma:\s*no\s+cover\b", re.IGNORECASE)
ASSERTION_CALL_PREFIXES: tuple[str, ...] = ("assert", "fail", "raises", "warns", "expect", "check")


@dataclass(frozen=True)
class TestIssue:
    """
    单条测试质量违规记录。

    Attributes:
        rule_id: 规则标识符。
        file_path: 相对或目标文件路径字符串。
        line_number: 1-based 行号。
        message: 违规说明。
    """

    rule_id: str
    file_path: str
    line_number: int
    message: str


def is_test_file(path: Path) -> bool:
    """
    判断给定路径是否属于常见语言的测试源码文件。

    Args:
        path: 待判定文件路径。

    Returns:
        若文件名符合测试文件命名约定返回 True，否则返回 False。
    """
    name = path.name
    lower = name.lower()
    suffix = path.suffix.lower()
    if suffix == ".py":
        return lower.startswith("test_") or lower.endswith("_test.py")
    if suffix in (".ts", ".tsx", ".js", ".jsx"):
        return ".test." in lower or ".spec." in lower
    if suffix in (".cpp", ".cc", ".cxx", ".h", ".hpp"):
        return lower.endswith(("_test.cpp", "_test.cc", "_tests.cpp", "_tests.cc")) or name.startswith("Test")
    return False


def _node_has_assertion(func_node: ast.FunctionDef | ast.AsyncFunctionDef) -> bool:
    """
    使用 AST 检查 Python 测试函数体内是否包含至少一条有效断言或异常期望。

    Args:
        func_node: 测试函数对应的 AST 节点。

    Returns:
        包含有效断言返回 True，否则返回 False。
    """
    for node in ast.walk(func_node):
        if isinstance(node, ast.Assert):
            return True
        if isinstance(node, ast.Call):
            callee = node.func
            attr_name = ""
            if isinstance(callee, ast.Attribute):
                attr_name = callee.attr
            elif isinstance(callee, ast.Name):
                attr_name = callee.id
            if attr_name.lower().startswith(ASSERTION_CALL_PREFIXES):
                return True
    return False


def _is_swallowing_handler(handler: ast.ExceptHandler) -> bool:
    """
    判断 Python except 处理器是否仅包含 pass 或省略号（吞异常）。

    Args:
        handler: AST ExceptHandler 节点。

    Returns:
        若吞掉异常返回 True，否则返回 False。
    """
    if len(handler.body) != 1:
        return False
    stmt = handler.body[0]
    if isinstance(stmt, ast.Pass):
        return True
    return isinstance(stmt, ast.Expr) and isinstance(stmt.value, ast.Constant) and stmt.value.value is Ellipsis


def scan_python_test_ast(file_path: Path, rel_label: str, content: str) -> list[TestIssue]:
    """
    对 Python 测试文件执行 AST 深度检查（无断言测试、吞异常、空洞 assert True）。

    Args:
        file_path: 源文件绝对路径。
        rel_label: 展示用相对路径标签。
        content: 源文件文本。

    Returns:
        检出的 TestIssue 列表。
    """
    issues: list[TestIssue] = []
    try:
        tree = ast.parse(content, filename=str(file_path))
    except SyntaxError as exc:
        return [TestIssue("TEST-SYNTAX", rel_label, exc.lineno or 1, f"Python 测试文件语法错误: {exc.msg}")]

    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name.startswith("test_"):
            if not _node_has_assertion(node):
                issues.append(
                    TestIssue(
                        "TEST-NO-ASSERT",
                        rel_label,
                        node.lineno,
                        f"测试函数 '{node.name}' 缺少任何断言或异常校验（涉嫌空跑刷覆盖率）",
                    )
                )
            for child in ast.walk(node):
                if isinstance(child, ast.ExceptHandler) and _is_swallowing_handler(child):
                    issues.append(
                        TestIssue(
                            "TEST-SWALLOW-EXC",
                            rel_label,
                            child.lineno,
                            f"测试函数 '{node.name}' 中存在吞异常 except 块",
                        )
                    )
    return issues


def scan_test_file(file_path: Path, root_dir: Path) -> list[TestIssue]:
    """
    扫描单个测试文件中的 B5 反滥竽充数违规项。

    Args:
        file_path: 测试文件路径。
        root_dir: 扫描根目录路径。

    Returns:
        违规记录列表。
    """
    rel_label = str(file_path.relative_to(root_dir))
    try:
        content = file_path.read_text(encoding="utf-8")
    except OSError as exc:
        return [TestIssue("TEST-READ-ERR", rel_label, 1, f"无法读取文件: {exc}")]

    issues: list[TestIssue] = []
    for line_no, line in enumerate(content.splitlines(), start=1):
        for rule_id, pattern in SLEEP_PATTERNS:
            if pattern.search(line):
                issues.append(TestIssue(rule_id, rel_label, line_no, "测试代码严禁使用 sleep/setTimeout 盲等"))
        for rule_id, pattern in HOLLOW_ASSERT_PATTERNS:
            if pattern.search(line):
                issues.append(TestIssue(rule_id, rel_label, line_no, "检测到永真空洞断言（反滥竽充数红线）"))
        if PRAGMA_NO_COVER_RE.search(line):
            issues.append(TestIssue("TEST-NO-COVER", rel_label, line_no, "检测到 pragma: no cover，严禁借此虚抬覆盖率"))

    if file_path.suffix.lower() == ".py":
        issues.extend(scan_python_test_ast(file_path, rel_label, content))

    return issues


def scan_test_directory(target_dir: Path) -> tuple[int, list[TestIssue]]:
    """
    递归扫描目录下全部测试文件并汇总 B5 违规记录。

    Args:
        target_dir: 待扫描根目录路径。

    Returns:
        二元组 (扫描测试文件数, 全部 TestIssue 列表)。
    """
    resolved = target_dir.resolve()
    scanned = 0
    issues: list[TestIssue] = []

    for root, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            candidate = Path(root) / filename
            if not is_test_file(candidate):
                continue
            scanned += 1
            issues.extend(scan_test_file(candidate, resolved))

    return scanned, issues


def parse_cobertura_xml(xml_path: Path) -> tuple[float, float | None]:
    """
    解析 Cobertura XML 覆盖率报告中的行覆盖率与分支覆盖率（百分比 0-100）。

    Args:
        xml_path: Cobertura XML 文件路径。

    Returns:
        二元组 (行覆盖率百分比, 分支覆盖率百分比或 None)。

    Raises:
        ValueError: 当 XML 格式非法或缺少 line-rate 属性时抛出。
    """
    try:
        tree = ET.parse(xml_path)
    except (OSError, ET.ParseError) as exc:
        raise ValueError(f"无法解析 Cobertura XML ({xml_path}): {exc}") from exc

    root = tree.getroot()
    line_rate = root.attrib.get("line-rate")
    if line_rate is None:
        raise ValueError(f"Cobertura XML 缺少 line-rate 属性: {xml_path}")

    branch_rate = root.attrib.get("branch-rate")
    line_pct = float(line_rate) * 100.0
    branch_pct = float(branch_rate) * 100.0 if branch_rate is not None else None
    return line_pct, branch_pct


def parse_lcov_info(lcov_path: Path) -> tuple[float, float | None]:
    """
    解析 LCOV (lcov.info) 报告中的行覆盖率与分支覆盖率（百分比 0-100）。

    Args:
        lcov_path: LCOV 报告文件路径。

    Returns:
        二元组 (行覆盖率百分比, 分支覆盖率百分比或 None)。

    Raises:
        ValueError: 当文件无法读取或无有效 LF 统计项时抛出。
    """
    try:
        text = lcov_path.read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError(f"无法读取 LCOV 报告 ({lcov_path}): {exc}") from exc

    lines_found = 0
    lines_hit = 0
    branches_found = 0
    branches_hit = 0

    for line in text.splitlines():
        if line.startswith("LF:"):
            lines_found += int(line[3:].strip())
        elif line.startswith("LH:"):
            lines_hit += int(line[3:].strip())
        elif line.startswith("BRF:"):
            branches_found += int(line[4:].strip())
        elif line.startswith("BRH:"):
            branches_hit += int(line[4:].strip())

    if lines_found == 0:
        raise ValueError(f"LCOV 报告中未发现有效行统计 (LF): {lcov_path}")

    line_pct = (lines_hit / lines_found) * 100.0
    branch_pct = (branches_hit / branches_found) * 100.0 if branches_found > 0 else None
    return line_pct, branch_pct


def check_coverage_gate(
    line_pct: float,
    branch_pct: float | None,
    min_line: float,
    min_branch: float,
) -> list[str]:
    """
    依据阈值判定覆盖率是否满足 B5 门禁要求。

    Args:
        line_pct: 实际行覆盖率百分比。
        branch_pct: 实际分支覆盖率百分比（无分支统计时为 None）。
        min_line: 行覆盖率最低门禁（默认 95.0）。
        min_branch: 分支覆盖率最低门禁（默认 95.0）。

    Returns:
        未达标报错消息列表；达标返回空列表。
    """
    failures: list[str] = []
    if line_pct < min_line:
        failures.append(f"[COV-LINE] 行覆盖率 {line_pct:.2f}% 低于门禁要求 {min_line:.2f}%")
    if branch_pct is not None and branch_pct < min_branch:
        failures.append(f"[COV-BRANCH] 分支覆盖率 {branch_pct:.2f}% 低于门禁要求 {min_branch:.2f}%")
    return failures


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行 B5 测试质量扫描与可选覆盖率报告门禁核验。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        全部通过返回 0，存在违规或覆盖率未达标返回 1。
    """
    parser = argparse.ArgumentParser(description="B5 真测试契约静态扫描与覆盖率门禁校验")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="待扫描测试根目录")
    parser.add_argument("--cov-xml", type=Path, default=None, help="Cobertura XML 覆盖率报告路径")
    parser.add_argument("--lcov", type=Path, default=None, help="LCOV 覆盖率报告路径")
    parser.add_argument("--min-line", type=float, default=95.0, help="最低行覆盖率百分比")
    parser.add_argument("--min-branch", type=float, default=95.0, help="最低分支覆盖率百分比")
    parsed = parser.parse_args(argv)

    scanned, issues = scan_test_directory(parsed.target)
    cov_failures: list[str] = []

    if parsed.cov_xml is not None:
        try:
            line_pct, branch_pct = parse_cobertura_xml(parsed.cov_xml)
            cov_failures.extend(check_coverage_gate(line_pct, branch_pct, parsed.min_line, parsed.min_branch))
            branch_str = f"{branch_pct:.2f}%" if branch_pct is not None else "N/A"
            print(f"📊 Cobertura 覆盖率: 行 {line_pct:.2f}% | 分支 {branch_str}")
        except ValueError as exc:
            cov_failures.append(str(exc))

    if parsed.lcov is not None:
        try:
            line_pct, branch_pct = parse_lcov_info(parsed.lcov)
            cov_failures.extend(check_coverage_gate(line_pct, branch_pct, parsed.min_line, parsed.min_branch))
            branch_str = f"{branch_pct:.2f}%" if branch_pct is not None else "N/A"
            print(f"📊 LCOV 覆盖率: 行 {line_pct:.2f}% | 分支 {branch_str}")
        except ValueError as exc:
            cov_failures.append(str(exc))

    print(f"🔍 已扫描 {scanned} 个测试文件，检出 {len(issues)} 处测试质量问题。")
    for issue in issues:
        print(f"  - [{issue.rule_id}] {issue.file_path}:{issue.line_number} {issue.message}")
    for failure in cov_failures:
        print(f"  - {failure}")

    if issues or cov_failures:
        return 1

    print("✅ B5 真测试契约与覆盖率门禁全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
