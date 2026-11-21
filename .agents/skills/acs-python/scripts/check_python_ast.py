#!/usr/bin/env python3
"""
check_python_ast.py: 基于 Python stdlib ast/tokenize 的零误报规范合规检查工具。

零三方依赖，服务于 acs-python：
1. 校验公开函数/方法类型标注与多行 Docstring（B-CMT）；
2. 拦截函数体内业务解释性行内注释（仅放行 noqa/type/pylint/pyright/fmt/isort/pragma 指令）；
3. 精准检出可变默认实参、裸 except、eval/exec/pickle.loads；
4. 检出 async def 内同步阻塞调用与吞掉 asyncio.CancelledError。
"""

from __future__ import annotations

import argparse
import ast
import io
import os
import sys
import tokenize
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
})

ALLOWED_COMMENT_PREFIXES: tuple[str, ...] = (
    "# noqa",
    "# type:",
    "# pylint:",
    "# pyright:",
    "# fmt:",
    "# isort:",
    "# pragma:",
    "# nosec",
)

UNSAFE_CALL_NAMES: frozenset[str] = frozenset({"eval", "exec"})
UNSAFE_MODULE_CALLS: frozenset[tuple[str, str]] = frozenset({("pickle", "loads"), ("pickle", "load")})
ASYNC_BLOCKING_CALLS: frozenset[tuple[str, str]] = frozenset({
    ("time", "sleep"),
    ("requests", "get"),
    ("requests", "post"),
    ("requests", "put"),
    ("requests", "delete"),
    ("requests", "patch"),
    ("requests", "request"),
    ("urllib", "urlopen"),
})


@dataclass(frozen=True)
class PyAstIssue:
    """
    单条 Python AST 静态合规违规记录。

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


def _extract_attr_chain(node: ast.AST) -> list[str]:
    """
    提取属性访问链名称列表（如 requests.get -> ['requests', 'get']）。

    Args:
        node: 函数调用目标 AST 节点。

    Returns:
        自左向右的名称片段列表。
    """
    parts: list[str] = []
    current: ast.AST = node
    while isinstance(current, ast.Attribute):
        parts.append(current.attr)
        current = current.value
    if isinstance(current, ast.Name):
        parts.append(current.id)
    parts.reverse()
    return parts


def _handler_catches_cancelled_error(handler: ast.ExceptHandler) -> bool:
    """
    判断 except 处理器是否捕获 asyncio.CancelledError。

    Args:
        handler: AST ExceptHandler 节点。

    Returns:
        若捕获 CancelledError 返回 True，否则返回 False。
    """
    exc_type = handler.type
    if exc_type is None:
        return False
    targets = exc_type.elts if isinstance(exc_type, ast.Tuple) else [exc_type]
    for item in targets:
        chain = _extract_attr_chain(item)
        if chain and chain[-1] == "CancelledError":
            return True
    return False


def _handler_reraises(handler: ast.ExceptHandler) -> bool:
    """
    检查 except 块体内是否包含向上传播异常的 raise 语句。

    Args:
        handler: AST ExceptHandler 节点。

    Returns:
        包含 raise 语句返回 True，否则返回 False。
    """
    return any(isinstance(node, ast.Raise) for node in ast.walk(handler))


def _check_function_inline_comments(
    rel: str,
    content: str,
    func_ranges: list[tuple[int, int]],
) -> list[PyAstIssue]:
    """
    使用 tokenize 检出函数体内的业务解释性行内注释（放行标准静态分析指令）。

    Args:
        rel: 展示用相对路径。
        content: Python 源码全文。
        func_ranges: 函数体 (起始行, 结束行) 范围列表。

    Returns:
        行内注释违规列表。
    """
    issues: list[PyAstIssue] = []
    try:
        tokens = tokenize.generate_tokens(io.StringIO(content).readline)
        for tok in tokens:
            if tok.type != tokenize.COMMENT:
                continue
            line_no = tok.start[0]
            comment_lower = tok.string.strip().lower()
            if any(comment_lower.startswith(prefix) for prefix in ALLOWED_COMMENT_PREFIXES):
                continue
            if any(start < line_no <= end for start, end in func_ranges):
                issues.append(
                    PyAstIssue(
                        "B-CMT-INLINE",
                        rel,
                        line_no,
                        "函数体内严禁业务解释性行内注释，请收敛至函数上方多行 Docstring",
                    )
                )
    except tokenize.TokenError:
        return issues
    return issues


def scan_python_file(file_path: Path, root_dir: Path) -> list[PyAstIssue]:
    """
    对单个 Python 文件执行 AST 规范检查与函数体行内注释检查。

    Args:
        file_path: 目标 Python 文件路径。
        root_dir: 扫描根目录路径。

    Returns:
        PyAstIssue 违规列表。
    """
    rel = str(file_path.relative_to(root_dir))
    try:
        content = file_path.read_text(encoding="utf-8")
    except OSError as exc:
        return [PyAstIssue("PY-READ-ERR", rel, 1, f"无法读取文件: {exc}")]

    try:
        tree = ast.parse(content, filename=str(file_path))
    except SyntaxError as exc:
        return [PyAstIssue("PY-SYNTAX", rel, exc.lineno or 1, f"Python 语法错误: {exc.msg}")]

    issues: list[PyAstIssue] = []
    func_ranges: list[tuple[int, int]] = []

    for node in ast.walk(tree):
        if isinstance(node, ast.ClassDef):
            if not node.name.startswith("_") and ast.get_docstring(node) is None:
                issues.append(PyAstIssue("B-CMT-DOCSTRING", rel, node.lineno, f"公开类 '{node.name}' 缺少多行 Docstring"))

        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            end_lineno = getattr(node, "end_lineno", node.lineno) or node.lineno
            func_ranges.append((node.lineno, end_lineno))

            is_public = not node.name.startswith("_")
            if is_public:
                if ast.get_docstring(node) is None:
                    issues.append(
                        PyAstIssue("B-CMT-DOCSTRING", rel, node.lineno, f"公开函数 '{node.name}' 缺少多行 Docstring")
                    )
                all_args = list(node.args.posonlyargs) + list(node.args.args) + list(node.args.kwonlyargs)
                for arg in all_args:
                    if arg.arg in ("self", "cls"):
                        continue
                    if arg.annotation is None:
                        issues.append(
                            PyAstIssue(
                                "R-PY-TYPE",
                                rel,
                                arg.lineno,
                                f"公开函数 '{node.name}' 形参 '{arg.arg}' 缺少类型标注",
                            )
                        )
                if node.returns is None:
                    issues.append(
                        PyAstIssue("R-PY-TYPE", rel, node.lineno, f"公开函数 '{node.name}' 缺少返回值类型标注")
                    )

            defaults = list(node.args.defaults) + [d for d in node.args.kw_defaults if d is not None]
            for default_node in defaults:
                if isinstance(default_node, (ast.List, ast.Dict, ast.Set)):
                    issues.append(
                        PyAstIssue(
                            "R-PY-MUT-ARG",
                            rel,
                            default_node.lineno,
                            f"函数 '{node.name}' 严禁使用可变对象作为默认实参，请改用 None 哨兵",
                        )
                    )

            if isinstance(node, ast.AsyncFunctionDef):
                for sub in ast.walk(node):
                    if isinstance(sub, ast.Call):
                        chain = _extract_attr_chain(sub.func)
                        if len(chain) >= 2 and (chain[0], chain[-1]) in ASYNC_BLOCKING_CALLS:
                            issues.append(
                                PyAstIssue(
                                    "R-PY-ASYNC-BLOCK",
                                    rel,
                                    sub.lineno,
                                    f"异步函数 '{node.name}' 内严禁直接调用同步阻塞接口 {'.'.join(chain)}",
                                )
                            )
                    elif isinstance(sub, ast.ExceptHandler):
                        if _handler_catches_cancelled_error(sub) and not _handler_reraises(sub):
                            issues.append(
                                PyAstIssue(
                                    "R-PY-ASYNC-CANCEL",
                                    rel,
                                    sub.lineno,
                                    f"异步函数 '{node.name}' 捕获 CancelledError 后必须重新 raise 传播取消信号",
                                )
                            )

        elif isinstance(node, ast.ExceptHandler) and node.type is None:
            issues.append(PyAstIssue("R-PY-EXC", rel, node.lineno, "严禁无异常类型的裸 except 语句"))

        elif isinstance(node, ast.Call):
            chain = _extract_attr_chain(node.func)
            if len(chain) == 1 and chain[0] in UNSAFE_CALL_NAMES:
                issues.append(PyAstIssue("R-PY-SEC", rel, node.lineno, f"严禁对不可信输入使用 {chain[0]}()"))
            elif len(chain) >= 2 and (chain[0], chain[-1]) in UNSAFE_MODULE_CALLS:
                issues.append(PyAstIssue("R-PY-SEC", rel, node.lineno, f"严禁使用不安全反序列化 {'.'.join(chain)}()"))

    issues.extend(_check_function_inline_comments(rel, content, func_ranges))
    return issues


def scan_python_directory(target_dir: Path) -> tuple[int, list[PyAstIssue]]:
    """
    递归扫描指定目录下全部 Python 文件。

    Args:
        target_dir: 扫描根目录路径。

    Returns:
        二元组 (已扫描文件数, PyAstIssue 违规列表)。
    """
    resolved = target_dir.resolve()
    scanned = 0
    issues: list[PyAstIssue] = []

    for current, dirs, files in os.walk(resolved):
        dirs[:] = sorted(d for d in dirs if d not in IGNORE_DIRS)
        for filename in sorted(files):
            if not filename.endswith(".py"):
                continue
            scanned += 1
            issues.extend(scan_python_file(Path(current) / filename, resolved))

    return scanned, issues


def main(argv: list[str] | None = None) -> int:
    """
    命令行入口，执行 Python AST 深度规范检查。

    Args:
        argv: 可选命令行参数列表；省略时读取 sys.argv[1:]。

    Returns:
        零违规返回 0，否则返回 1。
    """
    parser = argparse.ArgumentParser(description="Python AST 深度规范合规检查工具")
    parser.add_argument("target", nargs="?", type=Path, default=Path.cwd(), help="待扫描目录或单个 .py 文件")
    parsed = parser.parse_args(argv)

    target = parsed.target.resolve()
    if target.is_file():
        scanned = 1
        issues = scan_python_file(target, target.parent)
    else:
        scanned, issues = scan_python_directory(target)

    print(f"🔍 已扫描 {scanned} 个 Python 文件，发现 {len(issues)} 处违规。")
    for issue in issues:
        print(f"  - [{issue.rule_id}] {issue.file_path}:{issue.line_number} {issue.message}")

    if issues:
        return 1

    print("✅ Python AST 规范检查全部通过。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
