#!/usr/bin/env python3
"""
check_frontend_standards.py: TypeScript、React、Next.js SSR 水合安全与无障碍专项静态检查工具。

零三方依赖，服务于 acs-web-frontend：
1. 拦截 any 滥用、@ts-ignore/@ts-nocheck 及 as unknown as 类型逃逸；
2. 拦截 eslint-disable 屏蔽 react-hooks/exhaustive-deps 告警；
3. 拦截未净化的 dangerouslySetInnerHTML 及 eval/new Function；
4. 检出 TSX/JSX 渲染主体中裸用浏览器对象（SSR 水合风险）与 <img> 缺 alt（WCAG 2.1 AA）。
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
    "node_modules",
    ".next",
    "dist",
    "build",
    "coverage",
    ".turbo",
})

FRONTEND_EXTS: frozenset[str] = frozenset({".ts", ".tsx", ".js", ".jsx"})
JSX_EXTS: frozenset[str] = frozenset({".tsx", ".jsx"})

TS_ANY_RE = re.compile(r"(:\s*any\b|\bas\s+any\b|<any>|Array<any>)")
TS_BYPASS_RE = re.compile(r"(@ts-ignore\b|@ts-nocheck\b|\bas\s+unknown\s+as\b)")
HOOKS_DISABLE_RE = re.compile(r"eslint-disable(?:-next-line|-line)?[^\n]*react-hooks/exhaustive-deps")
DANGEROUS_HTML_RE = re.compile(r"\bdangerouslySetInnerHTML\b")
SANITIZER_HINT_RE = re.compile(r"\b(?:DOMPurify\.sanitize|sanitizeHtml|purify\.sanitize)\b", re.IGNORECASE)
EVAL_RE = re.compile(r"\b(?:eval|new\s+Function)\s*\(")
SSR_BROWSER_GLOBAL_RE = re.compile(r"\b(?:window|document|localStorage|sessionStorage)\.[A-Za-z_]")
EFFECT_OR_GUARD_RE = re.compile(r"\b(?:useEffect|useLayoutEffect|typeof\s+window|addEventListener|onClick|onChange|onSubmit)\b")
IMG_TAG_RE = re.compile(r"<img\b([^>]*)/?>", re.IGNORECASE)


@dataclass(frozen=True)
class FrontendIssue:
    """
    单条前端规范违规记录。

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


def scan_frontend_file(file_path: Path, root_dir: Path) -> list[FrontendIssue]:
    """
    扫描单个 TS/TSX/JS/JSX 文件中的类型、Hooks、安全、SSR 水合与无障碍违规。

    Args:
        file_path: 目标前端文件路径。
        root_dir: 扫描根目录路径。

    Returns:
        FrontendIssue 违规列表。
    """
    rel = str(file_path.relative_to(root_dir))
    try:
        content = file_path.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        return [FrontendIssue("WEB-READ-ERR", rel, 1, f"无法读取文件: {exc}")]

    issues: list[FrontendIssue] = []
    lines = content.splitlines()
    is_jsx = file_path.suffix.lower() in JSX_EXTS
    has_use_client = any(line.strip() in ('"use client";', "'use client';", '"use client"', "'use client'") for line in lines[:5])

    for line_no, raw in enumerate(lines, start=1):
        stripped = raw.strip()
        if HOOKS_DISABLE_RE.search(raw):
            issues.append(
                FrontendIssue(
                    "WEB-HOOKS-DISABLE",
                    rel,
                    line_no,
                    "严禁使用 eslint-disable 屏蔽 react-hooks/exhaustive-deps 依赖告警",
                )
            )
        if TS_BYPASS_RE.search(raw):
            issues.append(
                FrontendIssue(
                    "WEB-TS-BYPASS",
                    rel,
                    line_no,
                    "严禁使用 @ts-ignore / @ts-nocheck / as unknown as 绕过 TypeScript 类型检查",
                )
            )
        if stripped.startswith(("//", "/*", "*")):
            continue

        if TS_ANY_RE.search(raw):
            issues.append(FrontendIssue("WEB-TS-ANY", rel, line_no, "严禁使用 any，请改用 unknown + 类型守卫或 Schema 校验"))

        if EVAL_RE.search(raw):
            issues.append(FrontendIssue("WEB-EVAL", rel, line_no, "严禁使用 eval / new Function 执行动态代码"))

        if DANGEROUS_HTML_RE.search(raw):
            context_window = "\n".join(lines[max(0, line_no - 2) : min(len(lines), line_no + 2)])
            if not SANITIZER_HINT_RE.search(context_window):
                issues.append(
                    FrontendIssue(
                        "WEB-XSS-HTML",
                        rel,
                        line_no,
                        "dangerouslySetInnerHTML 必须先经 DOMPurify 等白名单净化库处理",
                    )
                )

        if is_jsx and not has_use_client and SSR_BROWSER_GLOBAL_RE.search(raw):
            preceding = "\n".join(lines[max(0, line_no - 6) : line_no])
            if not EFFECT_OR_GUARD_RE.search(preceding):
                issues.append(
                    FrontendIssue(
                        "WEB-SSR-HYDRATION",
                        rel,
                        line_no,
                        "非客户端边界组件中直接访问浏览器全局对象，存在 SSR 崩溃或水合不一致风险",
                    )
                )

        if is_jsx:
            for img_match in IMG_TAG_RE.finditer(raw):
                attrs = img_match.group(1)
                if not re.search(r"\balt\s*=", attrs):
                    issues.append(
                        FrontendIssue(
                            "WEB-A11Y-IMG-ALT",
                            rel,
                            line_no,
                            "<img> 标签缺少 alt 属性（违反 WCAG 2.1 AA 可访问性基线）",
                        )
                    )

    return issues


def scan_frontend_directory(target_dir: Path) -> tuple[int, list[FrontendIssue]]:
    """
    递归扫描指定目录下全部 TS/TSX/JS/JSX 文件。

    Args:
        target_dir: 扫描根目录路径。

    Returns:
        二元组 (已扫描文件数, FrontendIssue 违规列表)。
    """
    resolved = target_dir.resolve()
    scanned = 0
    issues: list[FrontendIssue] = []

    for current, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            candidate = Path(current) / filename
            if candidate.suffix.lower() not in FRONTEND_EXTS:
                continue
            scanned += 1
            issues.extend(scan_frontend_file(candidate, resolved))

    return scanned, issues


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行现代 Web 前端专项规范检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        零违规返回 0，否则返回 1。
    """
    parser = argparse.ArgumentParser(description="TypeScript / React / Next.js 专项规范静态检查")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="待扫描目录或单文件")
    parsed = parser.parse_args(argv)

    target = parsed.target.resolve()
    if target.is_file():
        scanned = 1
        issues = scan_frontend_file(target, target.parent)
    else:
        scanned, issues = scan_frontend_directory(target)

    print(f"🔍 已扫描 {scanned} 个前端文件，发现 {len(issues)} 处违规。")
    for issue in issues:
        print(f"  - [{issue.rule_id}] {issue.file_path}:{issue.line_number} {issue.message}")

    if issues:
        return 1

    print("✅ Web 前端专项规范检查全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
