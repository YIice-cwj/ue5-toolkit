#!/usr/bin/env python3
"""
lint_rules.py: 针对源码变更执行 AI 规范一票否决项的自动化静态扫描。

覆盖 B-CMT 单行 Doxygen、B4 过度防御、B-SEC 机密泄露、C++ NRVO、UE5 UPROPERTY、
Python 可变默认与裸 except、TS any/eval、SQL 拼接、UE5 原生线程等规则。
"""

from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass
from pathlib import Path

IGNORE_DIRS: frozenset[str] = frozenset({
    ".git",
    ".gemini",
    ".history",
    ".screenshots",
    ".trae",
    ".analysis",
    "node_modules",
    "build",
    "dist",
    "__pycache__",
    ".venv",
    "venv",
    "Intermediate",
    "Binaries",
    "Saved",
    "DerivedDataCache",
})

CPP_EXTS: frozenset[str] = frozenset({".h", ".hpp", ".hh", ".hxx", ".cc", ".cpp", ".cxx", ".c++"})
PY_EXTS: frozenset[str] = frozenset({".py"})
JS_EXTS: frozenset[str] = frozenset({".ts", ".tsx", ".js", ".jsx", ".mjs", ".cjs"})
JVM_EXTS: frozenset[str] = frozenset({".java", ".kt", ".kts"})
GO_EXTS: frozenset[str] = frozenset({".go"})
CONFIG_EXTS: frozenset[str] = frozenset({".json", ".yaml", ".yml", ".env", ".toml"})
SECRET_EXTS: frozenset[str] = CPP_EXTS | PY_EXTS | JS_EXTS | JVM_EXTS | GO_EXTS | CONFIG_EXTS | frozenset({".pem", ".key"})
SCANNABLE_EXTS: frozenset[str] = SECRET_EXTS


@dataclass(frozen=True)
class LintRule:
    """
    静态扫描规则定义。

    Attributes:
        rule_id: 规则唯一标识符。
        name: 规则中文名称。
        pattern: 编译后的单行正则匹配器。
        exts: 适用的小写文件后缀集合。
        desc: 违规提示说明。
    """

    rule_id: str
    name: str
    pattern: re.Pattern[str]
    exts: frozenset[str]
    desc: str


@dataclass(frozen=True)
class LintIssue:
    """
    单条静态扫描违规记录。

    Attributes:
        rule_id: 命中的规则标识符。
        name: 命中的规则名称。
        line_number: 1-based 行号。
        line_text: 去除首尾空白的源码行文本。
        desc: 违规修复提示。
    """

    rule_id: str
    name: str
    line_number: int
    line_text: str
    desc: str


RULES: tuple[LintRule, ...] = (
    LintRule(
        rule_id="SEC-KEY",
        name="B-SEC 机密泄露防护",
        pattern=re.compile(r"['\"][sS][kK]-[a-zA-Z0-9]{20,}['\"]"),
        exts=SECRET_EXTS,
        desc="检测到可能包含真实的 API Secret Key",
    ),
    LintRule(
        rule_id="SEC-PRIVATE-KEY",
        name="B-SEC 私钥泄露防护",
        pattern=re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
        exts=SECRET_EXTS,
        desc="检测到私钥内容，严禁提交到仓库",
    ),
    LintRule(
        rule_id="SEC-AWS-KEY",
        name="B-SEC AWS Access Key 泄露",
        pattern=re.compile(r"AKIA[0-9A-Z]{16}"),
        exts=SECRET_EXTS,
        desc="检测到疑似 AWS Access Key",
    ),
    LintRule(
        rule_id="SEC-GOOGLE-KEY",
        name="B-SEC Google API Key 泄露",
        pattern=re.compile(r"AIza[0-9A-Za-z_-]{35}"),
        exts=SECRET_EXTS,
        desc="检测到疑似 Google API Key",
    ),
    LintRule(
        rule_id="CPP-NRVO",
        name="C++ 破坏 NRVO 优化",
        pattern=re.compile(r"return\s+std::move\s*\([^)]+\)\s*;"),
        exts=CPP_EXTS,
        desc="严禁使用 return std::move(x) 阻断 NRVO 返回值优化",
    ),
    LintRule(
        rule_id="CPP-OVER-DEF",
        name="B4 过度防御: 引用/this 判空",
        pattern=re.compile(r"(&\w+\s*==\s*nullptr|this\s*==\s*nullptr)"),
        exts=CPP_EXTS,
        desc="语法或底层保证不可能为空，严禁对引用或 this 进行判空",
    ),
    LintRule(
        rule_id="CPP-DOC-SINGLELINE",
        name="B-CMT 单行压缩 Doxygen 文档头",
        pattern=re.compile(r"/\*\s*@brief[^\n]*\*/"),
        exts=CPP_EXTS,
        desc="文档头必须使用多行分标签 Doxygen 块，禁止单行压缩",
    ),
    LintRule(
        rule_id="CPP-CATCH-ALL",
        name="C++ 裸 catch(...) 吞异常",
        pattern=re.compile(r"catch\s*\(\s*\.\.\.\s*\)"),
        exts=CPP_EXTS,
        desc="严禁裸 catch(...) 掩盖异常，必须精确捕获具体类型",
    ),
    LintRule(
        rule_id="UE5-NAKED-OBJ",
        name="UE5 裸 UObject* 成员变量",
        pattern=re.compile(r"^\s*U[A-Z]\w*\s*\*\s*\w+\s*;"),
        exts=frozenset({".h"}),
        desc="UE5 成员变量应使用 TObjectPtr<T> 并配合 UPROPERTY() 防 GC 误杀",
    ),
    LintRule(
        rule_id="UE5-STD-THREAD",
        name="UE5 禁止原生 std 线程",
        pattern=re.compile(r"std::(thread|jthread)"),
        exts=CPP_EXTS,
        desc="UE5 必须使用 FAsyncTask / Async / ParallelFor，严禁绕过引擎调度器",
    ),
    LintRule(
        rule_id="PY-MUT-DEFAULT",
        name="Python 可变默认实参",
        pattern=re.compile(r"def\s+\w+\s*\([^)]*=\s*(\[\]|\{\})"),
        exts=PY_EXTS,
        desc="严禁使用 [] / {} 作为默认实参，必须用 None 哨兵并在函数体内初始化",
    ),
    LintRule(
        rule_id="PY-BARE-EXCEPT",
        name="Python 裸 except 吞异常",
        pattern=re.compile(r"except\s*:"),
        exts=PY_EXTS,
        desc="严禁无异常类型的裸 except 语句，必须精确捕获具体异常类型",
    ),
    LintRule(
        rule_id="TS-ANY",
        name="TypeScript 滥用 any",
        pattern=re.compile(r"(:\s*any\b|\bas\s+any\b|<any>|Array<any>)"),
        exts=JS_EXTS,
        desc="严禁 any，必须使用 unknown + 类型守卫收缩",
    ),
    LintRule(
        rule_id="JS-EVAL",
        name="JS/TS eval 或 new Function",
        pattern=re.compile(r"\b(eval|new\s+Function)\s*\("),
        exts=JS_EXTS,
        desc="严禁 eval / new Function 执行不可信输入",
    ),
    LintRule(
        rule_id="SQL-CONCAT",
        name="SQL 拼接注入高危",
        pattern=re.compile(r"(SELECT\s+.*FROM\s+.*[\+\$]|\.format\(.*SELECT\s+)", re.IGNORECASE),
        exts=frozenset({".py", ".ts", ".tsx", ".js", ".jsx", ".go", ".java", ".cc", ".cpp"}),
        desc="严禁使用字符串拼接或格式化生成 SQL，必须使用参数化绑定",
    ),
)


def scan_file(file_path: Path) -> list[LintIssue]:
    """
    扫描单个源码文件并返回命中的规范违规项列表。

    Args:
        file_path: 待扫描的目标文件路径。

    Returns:
        命中的 LintIssue 列表；无违规或无法读取时返回空列表。
    """
    suffix = file_path.suffix.lower()
    if suffix == ".md" or file_path.resolve() == Path(__file__).resolve():
        return []

    issues: list[LintIssue] = []
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return issues

    for rule in RULES:
        if suffix not in rule.exts:
            continue
        for line_number, line in enumerate(content.splitlines(), start=1):
            if rule.pattern.search(line):
                issues.append(
                    LintIssue(
                        rule_id=rule.rule_id,
                        name=rule.name,
                        line_number=line_number,
                        line_text=line.strip(),
                        desc=rule.desc,
                    )
                )
    return issues


def scan_directory(target_dir: Path) -> tuple[int, dict[Path, list[LintIssue]]]:
    """
    递归扫描指定目录中全部受支持后缀的源码文件。

    Args:
        target_dir: 扫描根目录路径。

    Returns:
        二元组 (已扫描文件数, 以相对路径为键的违规字典)。
    """
    total_scanned = 0
    findings: dict[Path, list[LintIssue]] = {}

    for root, dirs, files in os.walk(target_dir):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            file_path = Path(root) / filename
            if file_path.suffix.lower() not in SCANNABLE_EXTS:
                continue
            total_scanned += 1
            issues = scan_file(file_path)
            if issues:
                findings[file_path.relative_to(target_dir)] = issues

    return total_scanned, findings


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，对指定目录或当前工作目录执行规范静态扫描。

    Args:
        argv: 可选命令行参数列表；省略时使用 sys.argv[1:]。

    Returns:
        无违规返回 0，发现违规返回 1。
    """
    args = sys.argv[1:] if argv is None else argv
    target_dir = Path(args[0]) if args else Path.cwd()
    print(f"🛡️  正在对目录执行规范静态合规扫描: {target_dir}")

    total_scanned, findings = scan_directory(target_dir)
    total_issues = sum(len(items) for items in findings.values())

    for rel_path, issues in findings.items():
        print(f"\n❌ [{rel_path}]")
        for issue in issues:
            print(f"   Line {issue.line_number} [{issue.rule_id}] {issue.name}: {issue.desc}")
            print(f"   > {issue.line_text}")

    print(f"\n📊 扫描完成：共检查 {total_scanned} 个文件，发现 {total_issues} 个违规项。")
    return 1 if total_issues > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
