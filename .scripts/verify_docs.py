#!/usr/bin/env python3
"""文档一致性校验器。

校验入库 Markdown 文档与源码事实是否一致，覆盖以下维度：
1. 本地链接与脚本路径可达；
2. 插件 README 版本号与 `.uplugin` 的 `VersionName` 一致；
3. 根 README 覆盖全部插件；
4. 引擎版本区间与安装脚本的取值范围一致；
5. 根 README 声明的自动化测试前缀与 `ci_local.sh` 一致；
6. Doxyfile 收录全部插件的 Public 目录。

校验失败时以非零退出码结束，供本地 CI 门禁调用。
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from typing import Dict, Iterable, List, Sequence, Set, Tuple

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXCLUDED_PREFIXES = (".agents/", ".history/", ".memory/", "Docs/")
LINK_EXTENSIONS = {
    ".md", ".h", ".hpp", ".c", ".cc", ".cpp", ".cs", ".py", ".sh", ".bat", ".ini",
    ".json", ".txt", ".uproject", ".uplugin", ".xlsx", ".png", ".jpg", ".jpeg", ".svg", ".html",
}
CODE_LINK_CHARS = set("<>&*:: ")
VERSION_PATTERNS = (
    re.compile(r"当前版本\s*\*\*(\d+\.\d+\.\d+)\*\*"),
    re.compile(r"[（(]\s*\*\*v(\d+\.\d+\.\d+)\*\*\s*[)）]"),
    re.compile(r"[（(]\s*v(\d+\.\d+\.\d+)\s*[)）]"),
    re.compile(r"插件清单[（(]\s*v(\d+\.\d+\.\d+)\s*[)）]"),
)


class Report:
    """收集并输出校验结果。"""

    def __init__(self) -> None:
        self.errors: List[str] = []
        self.warnings: List[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)

    def warn(self, message: str) -> None:
        self.warnings.append(message)

    def emit(self) -> int:
        for message in self.warnings:
            print(f"[WARN] {message}")
        for message in self.errors:
            print(f"[ERROR] {message}")
        print(f"\n共 {len(self.errors)} 个错误，{len(self.warnings)} 个警告。")
        return 1 if self.errors else 0


def tracked_markdown() -> List[str]:
    """返回除治理与生成目录之外的入库 Markdown 文件列表。"""
    output = subprocess.run(
        ["git", "ls-files", "-z", "*.md"], cwd=ROOT, capture_output=True, check=True
    ).stdout.decode("utf-8")
    files = [path for path in output.split("\0") if path]
    return [path for path in files if not path.startswith(EXCLUDED_PREFIXES)]


def read_text(relative_path: str) -> str:
    """读取仓库内文件文本。"""
    with open(os.path.join(ROOT, relative_path), encoding="utf-8") as handle:
        return handle.read()


def plugin_versions() -> Dict[str, str]:
    """解析全部 `.uplugin` 的插件名与版本号。"""
    output = subprocess.run(
        ["git", "ls-files", "-z", "*.uplugin"], cwd=ROOT, capture_output=True, check=True
    ).stdout.decode("utf-8")
    versions: Dict[str, str] = {}
    for path in [item for item in output.split("\0") if item]:
        descriptor = json.loads(read_text(path))
        name = os.path.basename(path)[: -len(".uplugin")]
        versions[name] = str(descriptor.get("VersionName", ""))
    return versions


def iter_links(text: str) -> Iterable[str]:
    """迭代 Markdown 链接目标。"""
    for match in re.finditer(r"\[[^\]]*\]\(", text):
        index = match.end()
        depth = 1
        while index < len(text) and depth > 0:
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            index += 1
        target = text[match.end(): index - 1].strip()
        if target:
            yield target.split()[0].strip("<>")


def check_links(files: Sequence[str], report: Report) -> None:
    """校验本地链接与仓库内脚本路径可达。"""
    for relative_path in files:
        text = read_text(relative_path)
        base = os.path.dirname(relative_path)
        for target in iter_links(text):
            if target.startswith(("http://", "https://", "mailto:", "#")):
                continue
            if target.startswith("file:///"):
                report.error(f"{relative_path}: 使用本机绝对链接 {target}")
                continue
            clean = target.split("#")[0]
            if not clean:
                continue
            extension = os.path.splitext(clean)[1].lower()
            if extension not in LINK_EXTENSIONS:
                continue
            if any(char in clean for char in CODE_LINK_CHARS):
                continue
            resolved = os.path.normpath(os.path.join(base, clean))
            if not os.path.exists(os.path.join(ROOT, resolved)):
                report.error(f"{relative_path}: 链接目标不存在 {target}")

        for match in re.finditer(r"(?:\.scripts|Tools)/[\w./-]+", text):
            candidate = match.group(0).rstrip(".,;:)")
            if "*" in candidate or candidate.endswith("/"):
                continue
            if not os.path.exists(os.path.join(ROOT, candidate)):
                report.error(f"{relative_path}: 脚本路径不存在 {candidate}")


def check_plugin_versions(versions: Dict[str, str], report: Report) -> None:
    """校验插件 README 声明的版本与 `.uplugin` 一致。"""
    for plugin, expected in versions.items():
        readme = f"Plugins/{plugin}/README.md"
        if not os.path.exists(os.path.join(ROOT, readme)):
            report.error(f"{readme}: 插件缺少 README")
            continue
        text = read_text(readme)
        declared = set()
        for pattern in VERSION_PATTERNS:
            declared.update(pattern.findall(text))
        for item in sorted(declared):
            if item != expected:
                report.error(f"{readme}: 声明版本 {item} 与 .uplugin 的 {expected} 不一致")


def check_root_coverage(versions: Dict[str, str], report: Report) -> None:
    """校验根 README 覆盖全部插件。"""
    text = read_text("README.md")
    for plugin in versions:
        if plugin not in text:
            report.error(f"README.md: 未提及插件 {plugin}")
    tree_match = re.search(r"## (?:项目)?工程结构.*?```text(.*?)```", text, re.S)
    if tree_match:
        tree = tree_match.group(1)
        for plugin in versions:
            if plugin not in tree:
                report.error(f"README.md: 工程结构树缺少插件 {plugin}")


def check_engine_range(report: Report) -> None:
    """校验文档中的引擎版本区间与安装脚本一致。"""
    install_text = read_text(".scripts/install.py")
    minor_min = int(re.search(r"SUPPORTED_MINOR_MIN\s*=\s*(\d+)", install_text).group(1))
    minor_max = int(re.search(r"SUPPORTED_MINOR_MAX\s*=\s*(\d+)", install_text).group(1))
    expected = (minor_min, minor_max)

    for relative_path in tracked_markdown():
        text = read_text(relative_path)
        for match in re.finditer(r"5\.(\d+)\s*[~～-]\s*5\.(\d+)", text):
            claimed = (int(match.group(1)), int(match.group(2)))
            if claimed != expected:
                line = text.count("\n", 0, match.start()) + 1
                report.error(
                    f"{relative_path}:{line}: 引擎区间 5.{claimed[0]}~5.{claimed[1]} "
                    f"与安装脚本 5.{expected[0]}~5.{expected[1]} 不一致"
                )
        for match in re.finditer(r"UE\s*5\.(\d+)\+", text):
            line = text.count("\n", 0, match.start()) + 1
            report.error(f"{relative_path}:{line}: 引擎下限 5.{match.group(1)}+ 与安装脚本不一致")


def parse_prefixed_list(text: str, variable: str) -> List[str]:
    """解析 Python 或 Shell 中形如 `NAME = (...)` / `NAME=(...)` 的前缀列表。"""
    match = re.search(rf"{variable}\s*=\s*[（(](.*?)[)）]", text, re.S)
    if not match:
        return []
    body = match.group(1)
    quoted = re.findall(r'"([^"]+)"', body)
    if quoted:
        return quoted
    return [token.strip("'\"") for token in body.replace(",", " ").split() if token.strip("'\"")]


def check_test_prefixes(report: Report) -> None:
    """校验根 README 声明的测试前缀与本地 CI 一致。"""
    ci_text = read_text(".scripts/ci_local.sh")
    runner_text = read_text(".scripts/run_automation_tests.py")
    ci_prefixes = parse_prefixed_list(ci_text, "TEST_PREFIXES")
    runner_prefixes = parse_prefixed_list(runner_text, "REGRESSION_PREFIXES")
    if not ci_prefixes or not runner_prefixes:
        report.error("无法解析 TEST_PREFIXES 或 REGRESSION_PREFIXES")
        return

    missing = sorted(set(ci_prefixes) - set(runner_prefixes))
    if missing:
        report.error(f"ci_local.sh: 前缀不在 run_automation_tests.py 回归列表中 {missing}")

    readme = read_text("README.md")
    declared_match = re.search(r"自动化覆盖\s*(.*?)\s*前缀", readme)
    if not declared_match:
        report.error("README.md: 未找到自动化测试前缀声明")
        return
    declared = re.findall(r"`(Game\.[A-Za-z]+)`", declared_match.group(1))
    if set(declared) != set(ci_prefixes):
        report.error(
            f"README.md: 声明前缀 {sorted(set(declared))} 与 ci_local.sh {sorted(set(ci_prefixes))} 不一致"
        )


def check_doxyfile(versions: Dict[str, str], report: Report) -> None:
    """校验 Doxyfile 收录全部插件的 Public 目录。"""
    text = read_text("Doxyfile")
    input_match = re.search(r"^INPUT\s*=\s*(.*?)(?=^\w+\s*=|\Z)", text, re.S | re.M)
    if not input_match:
        report.error("Doxyfile: 缺少 INPUT 配置")
        return
    inputs = [item for item in re.split(r"\\\s*\n|\s+", input_match.group(1)) if item]

    for plugin in versions:
        public_dirs = []
        source_root = os.path.join(ROOT, "Plugins", plugin, "Source")
        if not os.path.isdir(source_root):
            continue
        for module in os.listdir(source_root):
            candidate = os.path.join(source_root, module, "Public")
            if os.path.isdir(candidate):
                public_dirs.append(os.path.normpath(os.path.join("Plugins", plugin, "Source", module, "Public")))
        if not public_dirs:
            continue
        recorded = {os.path.normpath(item) for item in inputs}
        for directory in public_dirs:
            if directory not in recorded:
                report.error(f"Doxyfile: 未收录 {directory}")


def check_plugin_qualifiers(versions: Dict[str, str], report: Report) -> None:
    """校验文档未使用不存在的插件命名空间限定。"""
    source_chunks: List[str] = []
    for base, _dirs, files in os.walk(os.path.join(ROOT, "Plugins")):
        if "Intermediate" in base or "Binaries" in base:
            continue
        for name in files:
            if name.endswith((".h", ".cpp")):
                with open(os.path.join(base, name), encoding="utf-8", errors="ignore") as handle:
                    source_chunks.append(handle.read())
    namespaces = set(re.findall(r"\bnamespace\s+([A-Za-z_]\w*)", "\n".join(source_chunks)))
    for plugin in versions:
        if plugin in namespaces:
            continue
        token = f"{plugin}::"
        for relative_path in tracked_markdown():
            text = read_text(relative_path)
            index = text.find(token)
            if index >= 0:
                line = text.count("\n", 0, index) + 1
                report.error(f"{relative_path}:{line}: 使用了不存在的命名空间 {token}")


def main() -> int:
    """执行全部校验并输出结果。"""
    report = Report()
    versions = plugin_versions()
    files = tracked_markdown()

    check_links(files, report)
    check_plugin_versions(versions, report)
    check_root_coverage(versions, report)
    check_engine_range(report)
    check_test_prefixes(report)
    check_doxyfile(versions, report)
    check_plugin_qualifiers(versions, report)

    print(f"校验 Markdown: {len(files)} 篇，插件: {len(versions)} 个")
    return report.emit()


if __name__ == "__main__":
    sys.exit(main())
