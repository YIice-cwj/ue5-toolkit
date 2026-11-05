#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
自动化测试运行脚本 (run_automation_tests.py)

默认运行 SRP 解耦重构新增的 7 个用例：
  Game.GameplayCore.Stat.EffectiveEvaluator
  Game.GameplayCore.Stat.AcknowledgedDiff
  Game.GameplayCore.Buff.ModifierBridge
  Game.Data.Repository.Codec
  Game.Core.Time.HitStopTable
  Game.Core.Time.DilationMixer
  Game.Gameplay.Bootstrap.RetryGate

用法：
  python3 .scripts/run_automation_tests.py                 # 运行上述 7 个用例
  python3 .scripts/run_automation_tests.py --all           # 全插件前缀回归
  python3 .scripts/run_automation_tests.py --prefix Game.GameplayCore.Stat   # 追加前缀
  python3 .scripts/run_automation_tests.py --case Game.Core.Time.HitStopTable
  python3 .scripts/run_automation_tests.py --dry-run       # 仅打印命令

判定规则：进程退出码为零、日志出现 TEST COMPLETE. EXIT CODE: 0、
每个目标在日志中至少出现一条 Path 记录、且无任何 Fail/Error 记录。
`--all` 全量回归额外启用口径守卫：源码中静态声明的用例路径必须与实际执行路径
完全一致，不一致直接 FAIL 并逐条列出缺失/多出的用例名。
"""

import argparse
import os
import platform
import re
import subprocess
import sys
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR) if os.path.basename(SCRIPT_DIR) == ".scripts" else SCRIPT_DIR

sys.path.insert(0, SCRIPT_DIR)

from verify_install import editor_binary, find_uproject, resolve_engine  # noqa: E402

DEFAULT_CASES = (
    "Game.GameplayCore.Stat.EffectiveEvaluator",
    "Game.GameplayCore.Stat.AcknowledgedDiff",
    "Game.GameplayCore.Buff.ModifierBridge",
    "Game.Data.Repository.Codec",
    "Game.Core.Time.HitStopTable",
    "Game.Core.Time.DilationMixer",
    "Game.Gameplay.Bootstrap.RetryGate",
)

REGRESSION_PREFIXES = (
    "Game.Core",
    "Game.Logging",
    "Game.GameplayCore",
    "Game.Gameplay",
    "Game.Data",
    "Game.Feedback",
    "Game.Net",
)

COMPLETED_LINE = "Test Completed."
EXIT_OK_LINE = "TEST COMPLETE. EXIT CODE: 0"
PATH_PATTERN = re.compile(r"Path=\{([^}]+)\}")
DECLARED_TEST_PATTERN = re.compile(
    r'IMPLEMENT_(?:SIMPLE|COMPLEX|CUSTOM)_AUTOMATION_TEST\s*\(\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*"([^"]+)"'
)
SCAN_EXCLUDED_DIRS = {"Intermediate", "Binaries", "Saved", "DerivedDataCache", ".git"}


def platform_name() -> str:
    """返回 UnrealBuildTool 使用的平台目录名"""
    if platform.system() == "Darwin":
        return "Mac"
    if platform.system() == "Windows":
        return "Win64"
    return "Linux"


def resolve_editor_command(engine_root: str, target_platform: str) -> Optional[str]:
    """优先定位 UnrealEditor-Cmd，回退到常规 Editor 可执行文件"""
    suffix = ".exe" if target_platform == "Win64" else ""
    command_line_editor = os.path.join(
        engine_root, "Engine", "Binaries", target_platform, f"UnrealEditor-Cmd{suffix}"
    )
    if os.path.isfile(command_line_editor):
        return command_line_editor
    return editor_binary(engine_root, target_platform)


def build_targets(args: argparse.Namespace) -> List[str]:
    """按参数组装测试目标列表（精确用例名或前缀）"""
    if args.all:
        return list(REGRESSION_PREFIXES)
    targets = list(args.case) if args.case else list(DEFAULT_CASES)
    for prefix in args.prefix:
        if prefix not in targets:
            targets.append(prefix)
    return targets


def build_command(editor: str, uproject_path: str, targets: List[str]) -> List[str]:
    """构造无头自动化测试命令行

    多个测试目标必须以 `+` 拼接为单条 RunTests：Automation 命令内部按 `;` 拆分后会丢弃
    后续的 `Automation` 前缀，导致第二条起的 RunTests 被判定为未知子命令。

    额外追加三个隔离开关，避免引擎在受限环境（CI 容器 / 沙箱）下向用户目录写入：
    - `-DDC-ForceMemoryCache`：DDC 图改用内存缓存，消除共享 DDC 不可写时
      「no writable nodes available」的致命错误（DerivedDataBackends.cpp）；
    - `-NoZenAutoLaunch`：不自动启动 ZenServer，避免写 Zen 安装目录与 ~/.zen 历史；
    - `-notraceserver`：不启动 UnrealTraceServer，避免写 UnrealTrace 服务端日志。
    """
    exec_commands = [f"Automation RunTests {'+'.join(targets)}", "Quit"]
    return [
        editor,
        uproject_path,
        f"-ExecCmds={'; '.join(exec_commands)}",
        "-unattended",
        "-nullrhi",
        "-nosplash",
        "-DDC-ForceMemoryCache",
        "-NoZenAutoLaunch",
        "-notraceserver",
        "-log",
        "-stdout",
        "-FullStdOutLogOutput",
    ]


def parse_log(log_path: str) -> Tuple[Dict[str, bool], bool]:
    """解析日志中的用例结果与完成标记"""
    results: Dict[str, bool] = {}
    completed_ok = False
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as log_file:
            for line in log_file:
                if EXIT_OK_LINE in line:
                    completed_ok = True
                if COMPLETED_LINE not in line:
                    continue
                match = PATH_PATTERN.search(line)
                if not match:
                    continue
                results[match.group(1)] = "Result={Success}" in line
    except OSError:
        return {}, False
    return results, completed_ok


def is_target_covered(target: str, results: Dict[str, bool]) -> bool:
    """判定目标是否在日志中留下执行记录（精确名或前缀模式）"""
    if target in results:
        return True
    prefix_key = f"{target}."
    return any(path.startswith(prefix_key) for path in results)


def scan_declared_tests(project_root: str) -> Dict[str, str]:
    """扫描源码中静态声明的自动化用例路径

    返回值以用例路径为键、以 `相对路径:行号` 为值，供口径守卫逐条定位缺失用例。
    仅扫描 Plugins 与 Source 下的 *.cpp，跳过编译中间产物目录。
    """
    declared: Dict[str, str] = {}
    for scan_root in ("Plugins", "Source"):
        root_path = os.path.join(project_root, scan_root)
        if not os.path.isdir(root_path):
            continue
        for current_root, dir_names, file_names in os.walk(root_path):
            dir_names[:] = [name for name in dir_names if name not in SCAN_EXCLUDED_DIRS]
            for file_name in file_names:
                if not file_name.endswith(".cpp"):
                    continue
                full_path = os.path.join(current_root, file_name)
                relative_path = os.path.relpath(full_path, project_root)
                try:
                    with open(full_path, "r", encoding="utf-8", errors="replace") as source_file:
                        source_text = source_file.read()
                except OSError:
                    continue
                for match in DECLARED_TEST_PATTERN.finditer(source_text):
                    line_number = source_text.count("\n", 0, match.start()) + 1
                    declared[match.group(1)] = f"{relative_path}:{line_number}"
    return declared


def check_declared_vs_executed(project_root: str, results: Dict[str, bool]) -> bool:
    """校验静态声明用例与实际执行用例完全一致，返回是否一致"""
    declared = scan_declared_tests(project_root)
    executed = set(results)
    missing = sorted(set(declared) - executed)
    extra = sorted(executed - set(declared))

    print("-" * 78)
    print(f" 声明用例数: {len(declared)}    实际执行: {len(executed)}")
    if not missing and not extra:
        print(" 口径守卫  : OK（声明与实际执行完全一致）")
        return True
    for path in missing:
        print(f"  [✗] 声明但未执行: {path}  ({declared[path]})")
    for path in extra:
        print(f"  [✗] 执行但未声明: {path}")
    print(f" 口径守卫  : FAIL（声明未执行 {len(missing)} 条，执行未声明 {len(extra)} 条）")
    return False


def print_case_report(targets: List[str], results: Dict[str, bool]) -> Tuple[int, int, int]:
    """打印逐目标结果，返回 (已执行数, 未执行数, 失败数)"""
    covered = 0
    missing = 0
    failed = 0
    for target in targets:
        matched = [path for path in results if path == target or path.startswith(f"{target}.")]
        if not matched:
            print(f"  [✗] {target}  (未在日志中出现)")
            missing += 1
            continue
        covered += 1
        case_failures = [path for path in matched if not results[path]]
        if case_failures:
            failed += len(case_failures)
            print(f"  [✗] {target}  (失败 {len(case_failures)}/{len(matched)})")
            for path in case_failures:
                print(f"        - {path}")
        else:
            print(f"  [✓] {target}  ({len(matched)} 条通过)")
    return covered, missing, failed


def main() -> None:
    parser = argparse.ArgumentParser(description="运行 SRP 解耦新增用例与插件前缀回归")
    parser.add_argument("--project", default=REPO_ROOT, help="目标 UE 工程目录（缺省为当前仓库）")
    parser.add_argument("--engine", default=None, help="UE 引擎根目录（缺省读 UE_ENGINE_DIR 或平台默认路径）")
    parser.add_argument("--prefix", action="append", default=[], help="追加前缀目标（可重复）")
    parser.add_argument("--case", action="append", default=[], help="自定义精确用例名（可重复，覆盖默认 7 个）")
    parser.add_argument("--all", action="store_true", help="运行全部通用插件前缀回归")
    parser.add_argument("--timeout", type=int, default=20, help="运行超时分钟数（默认 20）")
    parser.add_argument("--dry-run", action="store_true", help="仅打印命令行，不启动引擎")
    args = parser.parse_args()

    project_root, uproject_path = find_uproject(args.project)
    if not project_root or not uproject_path:
        print(f"[错误] 目标路径未找到 *.uproject: {args.project}", file=sys.stderr)
        sys.exit(2)

    engine_root = resolve_engine(args.engine, uproject_path)
    if engine_root is None:
        print("[错误] 未找到 UE 引擎，请通过 --engine 或 UE_ENGINE_DIR 指定引擎根目录。", file=sys.stderr)
        sys.exit(2)

    target_platform = platform_name()
    editor = resolve_editor_command(engine_root, target_platform)
    if editor is None:
        print(f"[错误] 未找到 {target_platform} Editor 可执行文件: {engine_root}", file=sys.stderr)
        sys.exit(2)

    targets = build_targets(args)
    if not targets:
        print("[错误] 测试目标为空", file=sys.stderr)
        sys.exit(2)

    command = build_command(editor, uproject_path, targets)
    log_path = os.path.join(project_root, "Saved", "Logs", "AutomationTests.log")

    print("=" * 78)
    print(" 自动化测试运行")
    print(f" 引擎     : {engine_root}")
    print(f" 目标工程 : {project_root} ({os.path.basename(uproject_path)})")
    print(f" 目标数量 : {len(targets)}")
    print(f" 日志     : {log_path}")
    print("=" * 78)

    if args.dry_run:
        print("[dry-run] 命令:")
        print(" ".join(command))
        sys.exit(0)

    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    timeout_seconds = max(60, args.timeout * 60)
    process_ok = False
    try:
        with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
            completed = subprocess.run(
                command,
                cwd=project_root,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                timeout=timeout_seconds,
                check=False,
            )
        process_ok = completed.returncode == 0
    except subprocess.TimeoutExpired:
        print(f"[✗] 运行超时（{args.timeout} 分钟）")
        print("     若进程长期停留在 DerivedDataCache，请先手动启动一次 Editor 完成缓存预热。")
    except OSError as error:
        print(f"[✗] 无法启动 Editor: {error}", file=sys.stderr)

    if not process_ok:
        print(f"[✗] Editor 进程未正常退出，请检查日志尾部: {log_path}")

    results, completed_ok = parse_log(log_path)
    covered, missing, failed = print_case_report(targets, results)

    declaration_ok = True
    if args.all:
        declaration_ok = check_declared_vs_executed(project_root, results)

    print("-" * 78)
    print(f" 已执行目标: {covered}    未执行: {missing}    失败用例: {failed}")
    overall_ok = process_ok and completed_ok and missing == 0 and failed == 0 and declaration_ok
    print(f" 完成标记  : {'OK' if completed_ok else '缺失'}")
    if args.all:
        print(f" 口径守卫  : {'OK' if declaration_ok else 'FAIL'}")
    print(f" 总体结果  : {'PASS' if overall_ok else 'FAIL'}")
    print("=" * 78)
    sys.exit(0 if overall_ok else 1)


if __name__ == "__main__":
    main()
