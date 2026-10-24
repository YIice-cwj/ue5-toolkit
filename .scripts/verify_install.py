#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
空工程一键安装验收脚本 (verify_install.py)

执行链：校验 UE 5.1 ~ 5.8 引擎与目标工程 → copy 安装全部通用插件与工具链 →
绑定生成器配置 → 反向生成 C++/JSON/GameplayTag → 编译目标 Editor →
运行 Game.Data 自动化测试 → 输出表数/行数/构建/测试摘要。

自动化测试命令在引擎完成初始化后由 Automation 子系统调度执行，
不使用启动期裸 ExecCmds 执行业务命令，避免子系统尚未就绪的时序问题。
"""

import argparse
import json
import os
import platform
import subprocess
import sys
import time
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR) if os.path.basename(SCRIPT_DIR) == ".scripts" else SCRIPT_DIR
INSTALL_SCRIPT = os.path.join(SCRIPT_DIR, "install.py")
REQUIRED_PLUGINS = ("GameCore", "GameLogging", "GameData", "GameGameplayCore", "GameGameplay")
TEST_PREFIX = "Game.Data"
SUPPORTED_MINOR_MIN = 1
SUPPORTED_MINOR_MAX = 8
SUPPORTED_ENGINE_RANGE_DESC = f"5.{SUPPORTED_MINOR_MIN} ~ 5.{SUPPORTED_MINOR_MAX}"


def is_supported_engine_version(version: Optional[Dict[str, int]]) -> bool:
    """校验 Build.version 是否处于 UE 5.1 ~ 5.8 支持范围内"""
    if not version:
        return False
    major = version.get("MajorVersion")
    minor = version.get("MinorVersion")
    return major == 5 and isinstance(minor, int) and SUPPORTED_MINOR_MIN <= minor <= SUPPORTED_MINOR_MAX


def _read_uproject_association(uproject_path: Optional[str]) -> str:
    """读取 *.uproject 中的 EngineAssociation（失败返回空串）"""
    if not uproject_path or not os.path.isfile(uproject_path):
        return ""
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return str(data.get("EngineAssociation", "")).strip()
    except Exception:
        return ""


def _platform_engine_dirs(version_tag: str) -> List[str]:
    """按当前平台返回指定版本标签（如 5.8）的标准引擎安装目录列表"""
    system = platform.system()
    if system == "Darwin":
        return [
            f"/Users/Shared/Epic Games/UE_{version_tag}",
            f"/Applications/Epic Games/UE_{version_tag}",
        ]
    if system == "Windows":
        return [
            f"C:/Program Files/Epic Games/UE_{version_tag}",
            f"D:/Program Files/Epic Games/UE_{version_tag}",
        ]
    return [
        os.path.expanduser(f"~/UnrealEngine/UE_{version_tag}"),
        f"/opt/UnrealEngine/UE_{version_tag}",
    ]


def resolve_engine(explicit_engine: Optional[str], uproject_path: Optional[str] = None) -> Optional[str]:
    """解析引擎根目录：显式参数（严格校验）> UE_ENGINE_DIR > 目标工程 EngineAssociation > 平台已安装的 UE 5.8~5.1"""
    if explicit_engine:
        build_ver = os.path.join(explicit_engine, "Engine", "Build", "Build.version")
        return os.path.abspath(explicit_engine) if os.path.isfile(build_ver) else None

    candidates: List[str] = []
    env_engine = os.environ.get("UE_ENGINE_DIR")
    if env_engine:
        candidates.append(env_engine)

    assoc = _read_uproject_association(uproject_path)
    if assoc:
        candidates.extend(_platform_engine_dirs(assoc))

    for minor in range(SUPPORTED_MINOR_MAX, SUPPORTED_MINOR_MIN - 1, -1):
        candidates.extend(_platform_engine_dirs(f"5.{minor}"))

    for candidate in candidates:
        if os.path.isfile(os.path.join(candidate, "Engine", "Build", "Build.version")):
            return os.path.abspath(candidate)
    return None


def read_engine_version(engine_root: str) -> Optional[Dict[str, int]]:
    """读取 Engine/Build/Build.version"""
    version_path = os.path.join(engine_root, "Engine", "Build", "Build.version")
    try:
        with open(version_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def find_uproject(project_path: str) -> Tuple[Optional[str], Optional[str]]:
    """查找目标工程根目录与 *.uproject"""
    root = os.path.abspath(project_path)
    if os.path.isfile(root) and root.endswith(".uproject"):
        return os.path.dirname(root), root
    if os.path.isdir(root):
        candidates = [f for f in os.listdir(root) if f.endswith(".uproject")]
        if candidates:
            return root, os.path.join(root, candidates[0])
    return None, None


def run_logged(name: str, command: List[str], cwd: str, log_path: str, timeout_seconds: int) -> Tuple[bool, float]:
    """执行外部命令并把标准输出写入日志，返回 (是否成功, 耗时秒)"""
    print(f"\n[步骤] {name}")
    print(f"       命令: {' '.join(command)}")
    print(f"       日志: {log_path}")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    started = time.time()
    try:
        with open(log_path, "w", encoding="utf-8", errors="replace") as log_file:
            completed = subprocess.run(
                command,
                cwd=cwd,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                timeout=timeout_seconds,
                check=False,
            )
        elapsed = time.time() - started
        if completed.returncode == 0:
            print(f"[✓] {name} 完成 ({elapsed:.1f}s)")
            return True, elapsed
        print(f"[✗] {name} 失败，退出码 {completed.returncode} ({elapsed:.1f}s)")
        print_tail(log_path)
        return False, elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.time() - started
        print(f"[✗] {name} 超时 ({elapsed:.1f}s)")
        print_tail(log_path)
        return False, elapsed


def print_tail(log_path: str) -> None:
    """打印日志末尾若干行，便于定位失败"""
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
        for line in lines[-30:]:
            print(f"       | {line.rstrip()}")
    except OSError:
        pass


def validate_project_plugins(uproject_path: str) -> bool:
    """校验 .uproject 已启用全部通用插件"""
    try:
        with open(uproject_path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return False
    enabled = {
        item.get("Name")
        for item in data.get("Plugins", [])
        if isinstance(item, dict) and item.get("Enabled", True)
    }
    missing = [name for name in REQUIRED_PLUGINS if name not in enabled]
    if missing:
        print(f"[✗] .uproject 缺少插件启用项: {', '.join(missing)}")
        return False
    return True


def summarize_data(project_root: str) -> Tuple[int, int]:
    """统计 DataTables_Json 下的表数与总行数"""
    json_root = os.path.join(project_root, "DataTables_Json")
    table_count = 0
    row_count = 0
    if not os.path.isdir(json_root):
        return table_count, row_count
    for current_root, _, files in os.walk(json_root):
        for file_name in files:
            if not file_name.endswith(".json"):
                continue
            table_count += 1
            try:
                with open(os.path.join(current_root, file_name), "r", encoding="utf-8") as f:
                    content = json.load(f)
                if isinstance(content, list):
                    row_count += len(content)
            except Exception:
                continue
    return table_count, row_count


def parse_test_log(log_path: str) -> Tuple[int, int, bool]:
    """解析自动化测试日志中的成功数、失败数与完成标记"""
    success_count = 0
    fail_count = 0
    completed_ok = False
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                if "Test Completed. Result={Success}" in line:
                    success_count += 1
                elif "Test Completed. Result={Fail}" in line:
                    fail_count += 1
                elif "TEST COMPLETE. EXIT CODE: 0" in line:
                    completed_ok = True
    except OSError:
        return 0, 0, False
    return success_count, fail_count, completed_ok


def build_commands(engine_root: str, uproject_path: str, target_name: str, minor_version: int = 8) -> Tuple[List[str], str]:
    """按平台构建 Editor 编译命令（UE 5.4+ 显式追加 -NoUBA）"""
    system = platform.system()
    extra_flags = ["-waitmutex", "-NoTrace"]
    if minor_version >= 4:
        extra_flags.append("-NoUBA")
    if system == "Darwin":
        build_tool = os.path.join(engine_root, "Engine", "Build", "BatchFiles", "Mac", "Build.sh")
        platform_name = "Mac"
        prefix: List[str] = []
    elif system == "Windows":
        build_tool = os.path.join(engine_root, "Engine", "Build", "BatchFiles", "Build.bat")
        platform_name = "Win64"
        prefix = ["cmd", "/c"]
    else:
        build_tool = os.path.join(engine_root, "Engine", "Build", "BatchFiles", "Linux", "Build.sh")
        platform_name = "Linux"
        prefix = []
    command = [build_tool, target_name, platform_name, "Development", f"-project={uproject_path}", *extra_flags]
    return prefix + command, platform_name


def editor_binary(engine_root: str, platform_name: str) -> Optional[str]:
    """定位目标平台 Editor 可执行文件"""
    if platform_name == "Mac":
        candidates = [
            os.path.join(engine_root, "Engine", "Binaries", "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
            os.path.join(engine_root, "Engine", "Binaries", "Mac", "UnrealEditor"),
        ]
    else:
        candidates = [os.path.join(engine_root, "Engine", "Binaries", platform_name, "UnrealEditor.exe" if platform_name == "Win64" else "UnrealEditor")]
    return next((path for path in candidates if os.path.isfile(path)), None)


def main() -> None:
    parser = argparse.ArgumentParser(description="空工程一键安装、生成、编译与自动化验收")
    parser.add_argument("--project", required=True, help="目标 UE 工程目录（含 *.uproject）")
    parser.add_argument("--engine", default=None, help="UE 引擎根目录（缺省读 UE_ENGINE_DIR 或平台默认路径）")
    parser.add_argument("--python", default=sys.executable, help="Python 解释器（缺省为当前解释器）")
    parser.add_argument("--timeout", type=int, default=30, help="单步超时分钟数（默认 30）")
    parser.add_argument("--skip-install", action="store_true", help="跳过插件/工具链安装")
    parser.add_argument("--skip-generate", action="store_true", help="跳过数据表生成")
    parser.add_argument("--skip-build", action="store_true", help="跳过 Editor 编译")
    parser.add_argument("--skip-tests", action="store_true", help="跳过自动化测试")
    args = parser.parse_args()

    project_root, uproject_path = find_uproject(args.project)
    if not project_root or not uproject_path:
        print(f"[错误] 目标路径未找到 *.uproject: {args.project}", file=sys.stderr)
        sys.exit(2)

    engine_root = resolve_engine(args.engine, uproject_path)
    if engine_root is None:
        print("[错误] 未找到 UE 引擎，请通过 --engine 或 UE_ENGINE_DIR 指定引擎根目录。", file=sys.stderr)
        sys.exit(2)

    version = read_engine_version(engine_root)
    if version is None:
        print(f"[错误] 无法读取引擎版本文件: {engine_root}/Engine/Build/Build.version", file=sys.stderr)
        sys.exit(2)
    if not is_supported_engine_version(version):
        print(
            f"[错误] 本资产库仅支持 UE {SUPPORTED_ENGINE_RANGE_DESC}，当前引擎为 "
            f"{version.get('MajorVersion')}.{version.get('MinorVersion')}。",
            file=sys.stderr,
        )
        sys.exit(2)

    project_name = os.path.splitext(os.path.basename(uproject_path))[0]
    target_name = f"{project_name}Editor"
    log_dir = os.path.join(project_root, "Saved", "Logs")
    timeout_seconds = max(60, args.timeout * 60)

    print("=" * 78)
    print(" 空工程一键安装验收")
    print(f" 引擎     : {engine_root} ({version.get('MajorVersion')}.{version.get('MinorVersion')}.{version.get('PatchVersion')})")
    print(f" 目标工程 : {project_root} ({os.path.basename(uproject_path)})")
    print("=" * 78)

    results: Dict[str, str] = {}

    if args.skip_install:
        results["install"] = "SKIP"
    else:
        install_command = [args.python, INSTALL_SCRIPT, "--all", "--mode", "copy", project_root]
        ok, _ = run_logged("安装插件与工具链", install_command, REPO_ROOT, os.path.join(log_dir, "VerifyInstall_Install.log"), timeout_seconds)
        if ok and not validate_project_plugins(uproject_path):
            ok = False
        results["install"] = "PASS" if ok else "FAIL"

    generator_config = os.path.join(project_root, "Tools", "Excel", "generator_config.json")
    pipeline_script = os.path.join(project_root, "Tools", "Excel", "Script", "run_pipeline.py")
    if args.skip_generate:
        results["generate"] = "SKIP"
    else:
        dependency_check = subprocess.run(
            [args.python, "-c", "import openpyxl"],
            cwd=project_root,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        if dependency_check.returncode != 0:
            print("[✗] 当前 Python 缺少 openpyxl，请执行: pip3 install -r Tools/requirements.txt")
            results["generate"] = "FAIL"
        elif not os.path.isfile(generator_config) or not os.path.isfile(pipeline_script):
            print("[✗] 目标工程缺少 Excel 工具链，无法生成")
            results["generate"] = "FAIL"
        else:
            generate_command = [args.python, pipeline_script, "--config", generator_config, "--all"]
            ok, _ = run_logged("生成静态数据代码与 JSON", generate_command, project_root, os.path.join(log_dir, "VerifyInstall_Generate.log"), timeout_seconds)
            results["generate"] = "PASS" if ok else "FAIL"

    table_count, row_count = summarize_data(project_root)

    if args.skip_build:
        results["build"] = "SKIP"
    else:
        build_command, platform_name = build_commands(
            engine_root, uproject_path, target_name, int(version.get("MinorVersion", 8))
        )
        ok, _ = run_logged("编译目标 Editor", build_command, project_root, os.path.join(log_dir, "VerifyInstall_Build.log"), timeout_seconds)
        results["build"] = "PASS" if ok else "FAIL"
    platform_name = "Mac" if platform.system() == "Darwin" else ("Win64" if platform.system() == "Windows" else "Linux")

    editor = editor_binary(engine_root, platform_name)
    if args.skip_tests:
        results["tests"] = "SKIP"
    elif editor is None:
        print(f"[✗] 未找到 {platform_name} Editor 可执行文件")
        results["tests"] = "FAIL"
    else:
        test_log = os.path.join(log_dir, "VerifyInstall_Tests.log")
        test_command = [
            editor,
            uproject_path,
            f"-ExecCmds=Automation RunTests {TEST_PREFIX}; Quit",
            "-unattended",
            "-nullrhi",
            "-nosplash",
            "-log",
            "-stdout",
            "-FullStdOutLogOutput",
        ]
        ok, _ = run_logged("运行自动化测试", test_command, project_root, test_log, timeout_seconds)
        success_count, fail_count, completed_ok = parse_test_log(test_log)
        tests_ok = ok and completed_ok and fail_count == 0 and success_count > 0
        print(f"       测试通过 {success_count}，失败 {fail_count}")
        results["tests"] = "PASS" if tests_ok else "FAIL"

    print("\n" + "=" * 78)
    print(" 验收摘要")
    for step in ("install", "generate", "build", "tests"):
        print(f" {step:<10}: {results.get(step, 'SKIP')}")
    print(f" 数据表数  : {table_count}")
    print(f" 数据总行数: {row_count}")
    print(f" 日志目录  : {log_dir}")
    overall_ok = all(value in ("PASS", "SKIP") for value in results.values())
    print(f" 总体结果  : {'PASS' if overall_ok else 'FAIL'}")
    print("=" * 78)
    sys.exit(0 if overall_ok else 1)


if __name__ == "__main__":
    main()
