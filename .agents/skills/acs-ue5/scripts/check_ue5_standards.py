#!/usr/bin/env python3
"""
check_ue5_standards.py: Unreal Engine 5 C++ 跨行 UHT 反射、GC 安全、Epic 命名与生态合规检查工具。

零三方依赖，服务于 acs-ue5：
1. 校验 UCLASS/USTRUCT 内 TObjectPtr<T> / UObject* 成员紧邻上方存在 UPROPERTY()；
2. 校验 *.generated.h 必须位于头文件最后一条 #include；
3. 校验 Epic 命名规范（U/A/F/E 前缀、禁 _t 后缀）；
4. 拦截 UE5 业务代码违规引入 std 容器/字符串/线程及主线程同步硬加载。
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
    "Intermediate",
    "Binaries",
    "Saved",
    "DerivedDataCache",
    "ThirdParty",
    "node_modules",
    "build",
})

CPP_EXTS: frozenset[str] = frozenset({".h", ".hpp", ".cpp", ".cc"})
HEADER_EXTS: frozenset[str] = frozenset({".h", ".hpp"})

INCLUDE_RE = re.compile(r'^\s*#include\s+[<"]([^>"]+)[>"]')
UCLASS_DECL_RE = re.compile(r"^\s*class\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\s*(?::|\{|$)")
USTRUCT_DECL_RE = re.compile(r"^\s*struct\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\s*(?::|\{|$)")
UENUM_DECL_RE = re.compile(r"^\s*enum\s+(?:class\s+)?([A-Za-z_]\w*)\s*(?::|\{|$)")
UOBJECT_MEMBER_RE = re.compile(
    r"^\s*(?:TObjectPtr\s*<\s*(?:class\s+)?U[A-Z]\w*\s*>|(?:class\s+)?U[A-Z]\w*\s*\*)\s+[A-Za-z_]\w*\s*(?:=|;)"
)
RAW_UOBJECT_MEMBER_RE = re.compile(r"^\s*(?:class\s+)?U[A-Z]\w*\s*\*\s+[A-Za-z_]\w*\s*(?:=|;)")
FORBIDDEN_STD_RE = re.compile(
    r"\bstd::(?:string|wstring|vector|map|unordered_map|set|unordered_set|thread|jthread|shared_ptr|unique_ptr|mutex)\b"
)
SYNC_LOAD_RE = re.compile(r"\b(?:LoadObject\s*<|StaticLoadObject\s*\(|TryLoad\s*\()")


@dataclass(frozen=True)
class Ue5Issue:
    """
    单条 UE5 规范违规记录。

    Attributes:
        rule_id: 规则标识符。
        file_path: 相对文件路径。
        line_number: 1-based 行号。
        message: 违规修复提示。
    """

    rule_id: str
    file_path: str
    line_number: int
    message: str


def _has_preceding_macro(lines: list[str], line_idx: int, macro_prefix: str) -> bool:
    """
    向上回溯跳过空行与注释行，检查紧邻上方是否存在指定 UE 反射宏。

    Args:
        lines: 文件全量行列表。
        line_idx: 当前声明所在 0-based 行索引。
        macro_prefix: 期望的宏前缀（如 UPROPERTY、UCLASS）。

    Returns:
        紧邻上方存在该宏返回 True，否则返回 False。
    """
    idx = line_idx - 1
    while idx >= 0:
        stripped = lines[idx].strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
            idx -= 1
            continue
        return stripped.startswith(macro_prefix)
    return False


def scan_ue5_file(file_path: Path, root_dir: Path) -> list[Ue5Issue]:
    """
    扫描单个 UE5 C++ 头文件或源文件中的反射、GC、命名与生态合规问题。

    Args:
        file_path: 目标源文件路径。
        root_dir: 扫描根目录路径。

    Returns:
        Ue5Issue 违规列表。
    """
    rel = str(file_path.relative_to(root_dir))
    try:
        lines = file_path.read_text(encoding="utf-8", errors="ignore").splitlines()
    except OSError as exc:
        return [Ue5Issue("UE5-READ-ERR", rel, 1, f"无法读取文件: {exc}")]

    issues: list[Ue5Issue] = []
    is_header = file_path.suffix.lower() in HEADER_EXTS
    include_lines: list[tuple[int, str]] = []
    in_uobject_scope = False

    for idx, raw in enumerate(lines):
        line_no = idx + 1
        stripped = raw.strip()
        if stripped.startswith("//"):
            continue

        inc_match = INCLUDE_RE.match(raw)
        if inc_match and is_header:
            include_lines.append((line_no, inc_match.group(1)))

        if stripped.startswith(("UCLASS(", "USTRUCT(")):
            in_uobject_scope = True

        if is_header and _has_preceding_macro(lines, idx, "UCLASS"):
            cls_match = UCLASS_DECL_RE.match(raw)
            if cls_match:
                cls_name = cls_match.group(1)
                if not cls_name.startswith(("U", "A")):
                    issues.append(Ue5Issue("UE5-EPIC-PREFIX", rel, line_no, f"UCLASS '{cls_name}' 必须以 U 或 A 前缀命名"))
                if cls_name.endswith("_t"):
                    issues.append(Ue5Issue("UE5-NO-T-SUFFIX", rel, line_no, f"UE5 严禁使用 _t 后缀: '{cls_name}'"))

        if is_header and _has_preceding_macro(lines, idx, "USTRUCT"):
            st_match = USTRUCT_DECL_RE.match(raw)
            if st_match:
                st_name = st_match.group(1)
                if not st_name.startswith("F"):
                    issues.append(Ue5Issue("UE5-EPIC-PREFIX", rel, line_no, f"USTRUCT '{st_name}' 必须以 F 前缀命名"))
                if st_name.endswith("_t"):
                    issues.append(Ue5Issue("UE5-NO-T-SUFFIX", rel, line_no, f"UE5 严禁使用 _t 后缀: '{st_name}'"))

        if is_header and _has_preceding_macro(lines, idx, "UENUM"):
            en_match = UENUM_DECL_RE.match(raw)
            if en_match:
                en_name = en_match.group(1)
                if not en_name.startswith("E"):
                    issues.append(Ue5Issue("UE5-EPIC-PREFIX", rel, line_no, f"UENUM '{en_name}' 必须以 E 前缀命名"))

        if is_header and in_uobject_scope and "(" not in stripped and UOBJECT_MEMBER_RE.match(raw):
            if not _has_preceding_macro(lines, idx, "UPROPERTY"):
                issues.append(
                    Ue5Issue(
                        "UE5-UPROPERTY-GC",
                        rel,
                        line_no,
                        "UCLASS/USTRUCT 中的 UObject 成员上方缺少 UPROPERTY()，存在 GC 悬空指针风险",
                    )
                )
            if RAW_UOBJECT_MEMBER_RE.match(raw):
                issues.append(
                    Ue5Issue(
                        "UE5-TOBJECTPTR",
                        rel,
                        line_no,
                        "UE5 头文件 UObject 成员变量应优先使用 TObjectPtr<T> 替代裸指针",
                    )
                )

        std_match = FORBIDDEN_STD_RE.search(raw)
        if std_match:
            issues.append(
                Ue5Issue(
                    "UE5-NO-STD",
                    rel,
                    line_no,
                    f"UE5 业务代码严禁使用 {std_match.group(0)}，请改用 UE 原生容器/字符串/任务系统",
                )
            )

        if SYNC_LOAD_RE.search(raw):
            issues.append(
                Ue5Issue(
                    "UE5-SYNC-LOAD",
                    rel,
                    line_no,
                    "检测到同步硬加载调用，运行时大资源请改用 TSoftObjectPtr + FStreamableManager 异步加载",
                )
            )

    if is_header and include_lines:
        gen_indices = [i for i, (_, hdr) in enumerate(include_lines) if hdr.endswith(".generated.h")]
        for gen_idx in gen_indices:
            if gen_idx != len(include_lines) - 1:
                gen_line, gen_hdr = include_lines[gen_idx]
                issues.append(
                    Ue5Issue(
                        "UE5-GEN-INCLUDE-ORDER",
                        rel,
                        gen_line,
                        f"#include \"{gen_hdr}\" 必须作为头文件最后一条 #include 语句",
                    )
                )

    return issues


def scan_ue5_directory(target_dir: Path) -> tuple[int, list[Ue5Issue]]:
    """
    递归扫描指定目录中全部 UE5 C++ 文件。

    Args:
        target_dir: 待扫描根目录路径。

    Returns:
        二元组 (已扫描文件数, Ue5Issue 列表)。
    """
    resolved = target_dir.resolve()
    scanned = 0
    issues: list[Ue5Issue] = []

    for current, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            candidate = Path(current) / filename
            if candidate.suffix.lower() not in CPP_EXTS:
                continue
            scanned += 1
            issues.extend(scan_ue5_file(candidate, resolved))

    return scanned, issues


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行 UE5 C++ 专项静态合规检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        零违规返回 0，否则返回 1。
    """
    parser = argparse.ArgumentParser(description="UE5 C++ 反射、GC 安全、命名与生态合规检查")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="UE5 Source 目录路径")
    parsed = parser.parse_args(argv)

    scanned, issues = scan_ue5_directory(parsed.target)
    print(f"🔍 已扫描 {scanned} 个 UE5 C++ 文件，发现 {len(issues)} 处违规。")
    for issue in issues:
        print(f"  - [{issue.rule_id}] {issue.file_path}:{issue.line_number} {issue.message}")

    if issues:
        return 1

    print("✅ UE5 C++ 专项规范检查全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
