#!/usr/bin/env python3
"""
check_cpp_standards.py: 现代 C++ 头文件守卫、多行分标签 Doxygen、单文件单类与内存/NRVO 规范检查工具。

零三方依赖，服务于 acs-cpp：
1. 校验头文件包含 #pragma once 或 include guard，禁止头文件出现 using namespace；
2. 校验 Doxygen 文档头块严格多行分标签（/** 与 */ 独占行且包含 @brief）；
3. 提醒头文件/源文件内存在多个顶层具体类定义（R-ARCH-CLASS 单文件单类）；
4. 拦截裸 new/delete、跨行 return std::move(...) 与裸 catch(...)。
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

IGNORE_DIRS: frozenset[str] = frozenset({
    ".git",
    "build",
    "dist",
    "third_party",
    "ThirdParty",
    "node_modules",
    "Intermediate",
    "Binaries",
})

HEADER_EXTS: frozenset[str] = frozenset({".h", ".hpp", ".hh", ".hxx"})
SOURCE_EXTS: frozenset[str] = frozenset({".cc", ".cpp", ".cxx"})
CPP_EXTS: frozenset[str] = HEADER_EXTS | SOURCE_EXTS

USING_NS_RE = re.compile(r"^\s*using\s+namespace\s+[A-Za-z_][\w:]*\s*;")
TOP_LEVEL_CLASS_RE = re.compile(r"^(?:template\s*<[^>]*>\s*)?(?:class|struct)\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\b[^;]*\{")
RAW_NEW_DELETE_RE = re.compile(r"(?<!operator\s)\b(?:new|delete)\b(?!\s*\()")
MULTILINE_NRVO_RE = re.compile(r"\breturn\s+std::move\s*\([^)]+\)\s*;", re.DOTALL)
CATCH_ALL_RE = re.compile(r"\bcatch\s*\(\s*\.\.\.\s*\)")
DOXYGEN_BLOCK_RE = re.compile(r"/\*\*.*?\*/", re.DOTALL)


@dataclass(frozen=True)
class CppIssue:
    """
    单条现代 C++ 规范违规记录。

    Attributes:
        rule_id: 规则标识符。
        file_path: 相对文件路径。
        line_number: 1-based 行号。
        message: 违规提示说明。
    """

    rule_id: str
    file_path: str
    line_number: int
    message: str


def _has_header_guard(content: str) -> bool:
    """
    检查 C++ 头文件是否包含 #pragma once 或标准 #ifndef/#define 守卫。

    Args:
        content: 头文件全文。

    Returns:
        具备头文件守卫返回 True，否则返回 False。
    """
    if re.search(r"^\s*#\s*pragma\s+once\b", content, re.MULTILINE):
        return True
    ifndef_match = re.search(r"^\s*#\s*ifndef\s+([A-Za-z_]\w*)", content, re.MULTILINE)
    if not ifndef_match:
        return False
    guard = re.escape(ifndef_match.group(1))
    return re.search(rf"^\s*#\s*define\s+{guard}\b", content, re.MULTILINE) is not None


def _line_number_at(content: str, offset: int) -> int:
    """
    将字符偏移量转换为 1-based 行号。

    Args:
        content: 源文本。
        offset: 0-based 字符索引。

    Returns:
        对应的 1-based 行号。
    """
    return content.count("\n", 0, offset) + 1


def scan_cpp_file(file_path: Path, root_dir: Path) -> list[CppIssue]:
    """
    扫描单个 C++ 头文件或源文件的结构、Doxygen、NRVO 与 RAII 合规性。

    Args:
        file_path: 目标文件路径。
        root_dir: 扫描根目录路径。

    Returns:
        CppIssue 违规列表。
    """
    rel = str(file_path.relative_to(root_dir))
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        return [CppIssue("CPP-READ-ERR", rel, 1, f"无法读取文件: {exc}")]

    issues: list[CppIssue] = []
    is_header = file_path.suffix.lower() in HEADER_EXTS
    lines = content.splitlines()

    if is_header and not _has_header_guard(content):
        issues.append(CppIssue("CPP-HEADER-GUARD", rel, 1, "头文件缺少 #pragma once 或 #ifndef/#define 守卫"))

    top_level_classes: list[tuple[int, str]] = []
    for line_no, raw in enumerate(lines, start=1):
        stripped = raw.strip()
        if stripped.startswith(("//", "/*", "*")):
            continue

        if is_header and USING_NS_RE.match(raw):
            issues.append(CppIssue("CPP-HEADER-USING-NS", rel, line_no, "头文件严禁使用 using namespace 污染全局命名空间"))

        if not raw.startswith((" ", "\t")):
            cls_match = TOP_LEVEL_CLASS_RE.match(raw)
            if cls_match:
                top_level_classes.append((line_no, cls_match.group(1)))

        if RAW_NEW_DELETE_RE.search(raw) and "placement" not in raw.lower():
            issues.append(CppIssue("CPP-RAW-NEW-DELETE", rel, line_no, "检测到裸 new/delete，请使用 RAII 与 std::make_unique/值语义"))

        if CATCH_ALL_RE.search(raw):
            issues.append(CppIssue("CPP-CATCH-ALL", rel, line_no, "严禁裸 catch(...) 吞异常，请精确捕获具体异常类型"))

    if len(top_level_classes) > 1:
        names = ", ".join(name for _, name in top_level_classes)
        issues.append(
            CppIssue(
                "R-ARCH-CLASS",
                rel,
                top_level_classes[1][0],
                f"检测到单文件定义多个顶层类/结构体 ({names})，请按单文件单类原则拆分",
            )
        )

    for nrvo_match in MULTILINE_NRVO_RE.finditer(content):
        issues.append(
            CppIssue(
                "CPP-NRVO",
                rel,
                _line_number_at(content, nrvo_match.start()),
                "严禁 return std::move(local) 阻断 NRVO 优化",
            )
        )

    for doc_match in DOXYGEN_BLOCK_RE.finditer(content):
        block = doc_match.group(0)
        start_line = _line_number_at(content, doc_match.start())
        block_lines = block.splitlines()
        if len(block_lines) == 1:
            issues.append(CppIssue("B-CMT-DOXYGEN", rel, start_line, "严禁单行压缩 Doxygen 块，/** 与 */ 必须各自独占行"))
            continue
        if block_lines[0].strip() != "/**" or block_lines[-1].strip() != "*/":
            issues.append(CppIssue("B-CMT-DOXYGEN", rel, start_line, "多行 Doxygen 块首行 /** 与尾行 */ 必须各自独占行"))
        if "@brief" not in block and "\\brief" not in block:
            issues.append(CppIssue("B-CMT-DOXYGEN", rel, start_line, "多行 Doxygen 文档头缺少必填 @brief 标签"))

    return issues


def scan_cpp_directory(target_dir: Path) -> tuple[int, list[CppIssue]]:
    """
    递归扫描指定目录中的全部 C++ 源文件与头文件。

    Args:
        target_dir: 扫描根目录路径。

    Returns:
        二元组 (已扫描文件数, CppIssue 列表)。
    """
    resolved = target_dir.resolve()
    scanned = 0
    issues: list[CppIssue] = []

    for current, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            candidate = Path(current) / filename
            if candidate.suffix.lower() not in CPP_EXTS:
                continue
            scanned += 1
            issues.extend(scan_cpp_file(candidate, resolved))

    return scanned, issues


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行现代 C++ 专项规范静态检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        零违规返回 0，否则返回 1。
    """
    parser = argparse.ArgumentParser(description="现代 C++ 头文件、多行 Doxygen、单文件单类与 RAII 检查")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="待扫描目录或单文件")
    parsed = parser.parse_args(argv)

    target = parsed.target.resolve()
    if target.is_file():
        scanned = 1
        issues = scan_cpp_file(target, target.parent)
    else:
        scanned, issues = scan_cpp_directory(target)

    print(f"🔍 已扫描 {scanned} 个 C++ 文件，发现 {len(issues)} 处违规。")
    for issue in issues:
        print(f"  - [{issue.rule_id}] {issue.file_path}:{issue.line_number} {issue.message}")

    if issues:
        return 1

    print("✅ 现代 C++ 专项规范检查全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
