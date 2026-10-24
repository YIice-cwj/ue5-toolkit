#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
打包冒烟一键验收脚本 (verify_package.py)

执行链：copy 安装全部通用插件与工具链 → 生成静态数据代码 → 编译目标 Editor →
以导出命令行工具无头生成 .bin → BuildCookRun 打包 → 校验包内双数据目录 →
打包产物 .bin / JSON 双通道冷启动对拍并比对内容摘要。

`--client-config=Shipping` 时改为校验启动自检报告（Shipping 裁剪日志，摘要对拍不适用）。

每一步都可单独跳过；任一步失败即记录 FAIL 并在收尾摘要中以非零退出码结束。
"""

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from typing import Dict, List, Optional, Tuple

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR) if os.path.basename(SCRIPT_DIR) == ".scripts" else SCRIPT_DIR
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import install  # noqa: E402
import verify_install  # noqa: E402

INSTALL_SCRIPT = os.path.join(SCRIPT_DIR, "install.py")
COOK_SCRIPT_DIR = os.path.join(REPO_ROOT, "Tools", "Cook", "Script")
BIN_RELATIVE_PATH = os.path.join("Content", "DataTables_Bin", "StaticDataCache.bin")
JSON_DIR_NAME = "DataTables_Json"
DEFAULT_PREFIX = "Game"
PACKAGING_SECTION = "/Script/UnrealEd.ProjectPackagingSettings"
ZEN_STORE_KEY = "bUseZenStore"


def platform_name() -> str:
    """按当前系统返回 UE 目标平台名"""
    system = platform.system()
    if system == "Darwin":
        return "Mac"
    if system == "Windows":
        return "Win64"
    return "Linux"


def load_project_config(project_root: str) -> Optional[Dict[str, object]]:
    """读取目标工程生成器配置；不存在返回 None"""
    config_path = os.path.join(project_root, "Tools", "Excel", "generator_config.json")
    if not os.path.isfile(config_path):
        return None
    try:
        with open(config_path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def commandlet_name(config: Dict[str, object]) -> str:
    """从配置推导 .bin 导出命令行工具名（与生成物保持同一约定）"""
    console_commands = config.get("console_commands")
    if isinstance(console_commands, dict) and console_commands.get("export_commandlet"):
        return str(console_commands["export_commandlet"])
    prefix = str(config.get("project_prefix") or DEFAULT_PREFIX)
    return f"{prefix}StaticDataExport"


def commandlet_editor(engine_root: str, target_platform: str) -> Optional[str]:
    """定位可执行 -run= 命令行工具的无头编辑器"""
    if target_platform == "Win64":
        candidates = [os.path.join(engine_root, "Engine", "Binaries", "Win64", "UnrealEditor-Cmd.exe")]
    elif target_platform == "Mac":
        candidates = [
            os.path.join(engine_root, "Engine", "Binaries", "Mac", "UnrealEditor-Cmd"),
            os.path.join(engine_root, "Engine", "Binaries", "Mac", "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor"),
        ]
    else:
        candidates = [os.path.join(engine_root, "Engine", "Binaries", "Linux", "UnrealEditor")]
    return next((path for path in candidates if os.path.isfile(path)), None)


def run_uat_path(engine_root: str) -> Optional[str]:
    """定位 RunUAT 打包入口"""
    batch_dir = os.path.join(engine_root, "Engine", "Build", "BatchFiles")
    candidates = [os.path.join(batch_dir, "RunUAT.bat"), os.path.join(batch_dir, "RunUAT.sh")]
    return next((path for path in candidates if os.path.isfile(path)), None)


def package_command(uat_path: str, uproject_path: str, archive_dir: str, target_platform: str,
                    client_config: str, minor_version: int = 8) -> List[str]:
    """构建 BuildCookRun 打包命令（Cook + Stage + Package + Archive）

    -UbtArgs 关闭 UBA（UE 5.4+）与 Trace：规避 UBA PCH 竞态与沙箱下 Trace 备份写盘失败。
    """
    ubt_args = "-NoUBA -NoTrace" if minor_version >= 4 else "-NoTrace"
    return [
        uat_path,
        "BuildCookRun",
        f"-project={uproject_path}",
        "-noP4",
        f"-platform={target_platform}",
        f"-clientconfig={client_config}",
        "-build",
        "-cook",
        "-stage",
        "-package",
        "-archive",
        f"-archivedirectory={archive_dir}",
        "-nodebuginfo",
        "-unattended",
        "-utf8output",
        f"-UbtArgs={ubt_args}",
    ]


def find_packaged_app(archive_dir: str, project_name: str) -> Optional[str]:
    """在归档目录中定位打包产物可执行文件"""
    for current_root, dir_names, _ in os.walk(archive_dir):
        for dir_name in dir_names:
            if not dir_name.endswith(".app"):
                continue
            app_dir = os.path.join(current_root, dir_name)
            executable = os.path.join(app_dir, "Contents", "MacOS", os.path.splitext(dir_name)[0])
            if os.path.isfile(executable):
                return executable
    for current_root, _, file_names in os.walk(archive_dir):
        for file_name in file_names:
            if file_name not in (project_name, f"{project_name}.exe"):
                continue
            candidate = os.path.join(current_root, file_name)
            if os.access(candidate, os.X_OK):
                return candidate
    return None


def find_staged_data(archive_dir: str) -> Tuple[Optional[str], Optional[str]]:
    """在归档目录中定位随包分发的 .bin 文件与 JSON 数据目录"""
    bin_path = None
    json_dir = None
    for current_root, dir_names, file_names in os.walk(archive_dir):
        if os.path.basename(current_root) == "DataTables_Bin" and "StaticDataCache.bin" in file_names:
            bin_path = os.path.join(current_root, "StaticDataCache.bin")
        for dir_name in dir_names:
            if dir_name == JSON_DIR_NAME and json_dir is None:
                json_dir = os.path.join(current_root, dir_name)
    return bin_path, json_dir


def update_packaged_paths(config_path: str, uproject_path: str, app_path: str) -> None:
    """把工程文件与打包产物绝对路径回填到生成器配置（供 Cook 工具链复用）"""
    with open(config_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    data["project_file"] = uproject_path
    data["packaged_app_path"] = app_path
    with open(config_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
        f.write("\n")


def ensure_zen_store_disabled(project_root: str) -> None:
    """确保工程显式关闭 Zen 存储

    UE 5.8 引擎默认 bUseZenStore=True，打包产物会变成依赖 Zen 服务的流式客户端，
    无头冒烟在无 Zen 服务的机器上无法启动；工程已显式配置该键时保持原值不覆盖。
    """
    ini_path = os.path.join(project_root, "Config", "DefaultGame.ini")
    text = ""
    if os.path.isfile(ini_path):
        with open(ini_path, "r", encoding="utf-8") as f:
            text = f.read()
    if re.search(rf"^\s*{ZEN_STORE_KEY}\s*=", text, re.MULTILINE):
        print(f"[-] 工程已显式配置 {ZEN_STORE_KEY}，保持原值")
        return
    if install.inject_ini_setting(ini_path, PACKAGING_SECTION, ZEN_STORE_KEY, "False"):
        print(f"[✓] 已为打包冒烟注入 {ZEN_STORE_KEY}=False（非 Zen 本地打包产物）")


def staged_project_dir(staged_bin: str) -> str:
    """由包内 .bin 路径反推暂存工程目录（.../<Project>/Content/DataTables_Bin/StaticDataCache.bin）"""
    return os.path.dirname(os.path.dirname(os.path.dirname(staged_bin)))


def stage_json_for_qa(project_root: str, staged_project_root: str) -> Optional[str]:
    """把工程 JSON 目录拷入打包产物同级目录（QA 对拍用；发布包默认不分发明文 JSON）"""
    source = os.path.join(project_root, JSON_DIR_NAME)
    if not os.path.isdir(source):
        return None
    target = os.path.join(staged_project_root, JSON_DIR_NAME)
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(source, target)
    return target


def run_channel_diff(config_path: str) -> int:
    """调用双通道对拍脚本（打包产物 .bin / JSON 摘要必须完全一致）"""
    if COOK_SCRIPT_DIR not in sys.path:
        sys.path.insert(0, COOK_SCRIPT_DIR)
    import channel_diff

    return channel_diff.run_diff(config_path)


def run_shipping_launch(config_path: str) -> bool:
    """Shipping 产物无头启动自检

    Shipping 构建裁剪全部日志输出（NO_LOGGING=1），macOS 又忽略
    RequestExitWithStatus 的退出码，故以生成物的启动自检报告判定装载结果。
    """
    if COOK_SCRIPT_DIR not in sys.path:
        sys.path.insert(0, COOK_SCRIPT_DIR)
    import packaged_run

    target = packaged_run.load_paths(config_path)
    started_at = time.time()
    exit_ok, _ = packaged_run.run_packaged(target, None, retries=0)
    report = packaged_run.read_verify_report(target, started_at)
    if report is None:
        print(f"[✗] Shipping 启动自检未产出报告（退出正常={exit_ok}）")
        return False
    print(f"[✓] Shipping 自检报告: loaded={int(report['loaded'])} 表数={report['tables']} "
          f"行数={report['rows']} 摘要={report['digest']}")
    return bool(report["loaded"]) and report["tables"] > 0


def main() -> None:
    parser = argparse.ArgumentParser(description="打包冒烟：生成 .bin、Cook 打包、包内双通道对拍")
    parser.add_argument("--project", required=True, help="目标 UE 工程目录（含 *.uproject）")
    parser.add_argument("--engine", default=None, help="UE 引擎根目录（缺省读 UE_ENGINE_DIR 或平台默认路径）")
    parser.add_argument("--archive", default=None, help="打包归档目录（缺省 <工程>/../Packaged/<工程名>）")
    parser.add_argument("--python", default=sys.executable, help="Python 解释器（缺省为当前解释器）")
    parser.add_argument("--client-config", default="Development", help="客户端构建配置（Development / Shipping）")
    parser.add_argument("--timeout", type=int, default=60, help="单步超时分钟数（默认 60）")
    parser.add_argument("--skip-install", action="store_true", help="跳过插件/工具链安装")
    parser.add_argument("--skip-generate", action="store_true", help="跳过静态数据生成")
    parser.add_argument("--skip-build", action="store_true", help="跳过 Editor 编译")
    parser.add_argument("--skip-export", action="store_true", help="跳过 .bin 导出")
    parser.add_argument("--skip-package", action="store_true", help="跳过 Cook 打包")
    parser.add_argument("--skip-verify", action="store_true", help="跳过打包产物双通道对拍")
    parser.add_argument("--keep-zen-store", action="store_true", help="保留工程既有 Zen 存储配置（无 Zen 服务时会启动失败）")
    parser.add_argument("--no-stage-json", action="store_true", help="不向打包产物拷贝 JSON 目录（QA 对拍用）")
    args = parser.parse_args()

    project_root, uproject_path = verify_install.find_uproject(args.project)
    if not project_root or not uproject_path:
        print(f"[错误] 目标路径未找到 *.uproject: {args.project}", file=sys.stderr)
        sys.exit(2)

    engine_root = verify_install.resolve_engine(args.engine, uproject_path)
    if engine_root is None:
        print("[错误] 未找到 UE 引擎，请通过 --engine 或 UE_ENGINE_DIR 指定引擎根目录。", file=sys.stderr)
        sys.exit(2)

    version = verify_install.read_engine_version(engine_root)
    if not verify_install.is_supported_engine_version(version):
        curr_ver = f"{version.get('MajorVersion')}.{version.get('MinorVersion')}" if version else "未知"
        print(
            f"[错误] 本资产库仅支持 UE {verify_install.SUPPORTED_ENGINE_RANGE_DESC}，当前引擎为 {curr_ver}。",
            file=sys.stderr,
        )
        sys.exit(2)

    project_name = os.path.splitext(os.path.basename(uproject_path))[0]
    target_platform = platform_name()
    log_dir = os.path.join(project_root, "Saved", "Logs")
    timeout_seconds = max(60, args.timeout * 60)
    config_path = os.path.join(project_root, "Tools", "Excel", "generator_config.json")
    archive_dir = args.archive or os.path.normpath(
        os.path.join(project_root, os.pardir, "Packaged", f"{project_name}_{args.client_config}"))

    print("=" * 78)
    print(" 打包冒烟验收")
    print(f" 引擎     : {engine_root} ({version.get('MajorVersion')}.{version.get('MinorVersion')}.{version.get('PatchVersion')})")
    print(f" 目标工程 : {project_root} ({os.path.basename(uproject_path)})")
    print(f" 归档目录 : {archive_dir}")
    print("=" * 78)

    results: Dict[str, str] = {}

    if args.skip_install:
        results["install"] = "SKIP"
    else:
        install_command = [args.python, INSTALL_SCRIPT, "--all", "--mode", "copy", project_root]
        ok, _ = verify_install.run_logged(
            "安装插件与工具链", install_command, REPO_ROOT, os.path.join(log_dir, "VerifyPackage_Install.log"),
            timeout_seconds)
        results["install"] = "PASS" if ok else "FAIL"

    if args.skip_generate:
        results["generate"] = "SKIP"
    else:
        pipeline_script = os.path.join(project_root, "Tools", "Excel", "Script", "run_pipeline.py")
        if not os.path.isfile(pipeline_script) or not os.path.isfile(config_path):
            print("[✗] 目标工程缺少 Excel 工具链，无法生成")
            results["generate"] = "FAIL"
        else:
            generate_command = [args.python, pipeline_script, "--config", config_path, "--all"]
            ok, _ = verify_install.run_logged(
                "生成静态数据代码与 JSON", generate_command, project_root,
                os.path.join(log_dir, "VerifyPackage_Generate.log"), timeout_seconds)
            results["generate"] = "PASS" if ok else "FAIL"

    minor_version = int(version.get("MinorVersion", 8))
    if args.skip_build:
        results["build"] = "SKIP"
    else:
        build_command, _ = verify_install.build_commands(
            engine_root, uproject_path, f"{project_name}Editor", minor_version
        )
        ok, _ = verify_install.run_logged(
            "编译目标 Editor", build_command, project_root,
            os.path.join(log_dir, "VerifyPackage_Build.log"), timeout_seconds)
        results["build"] = "PASS" if ok else "FAIL"

    config = load_project_config(project_root) or {}
    export_commandlet = commandlet_name(config)
    bin_path = os.path.join(project_root, BIN_RELATIVE_PATH)

    if args.skip_export:
        results["export"] = "SKIP"
    else:
        editor = commandlet_editor(engine_root, target_platform)
        if editor is None:
            print(f"[✗] 未找到 {target_platform} 无头编辑器，无法导出 .bin")
            results["export"] = "FAIL"
        else:
            if os.path.isfile(bin_path):
                os.remove(bin_path)
            export_command = [
                editor,
                uproject_path,
                f"-run={export_commandlet}",
                "-unattended",
                "-nullrhi",
                "-nosplash",
                "-nosound",
                "-stdout",
            ]
            ok, _ = verify_install.run_logged(
                ".bin 无头导出", export_command, project_root,
                os.path.join(log_dir, "VerifyPackage_Export.log"), timeout_seconds)
            if ok and not os.path.isfile(bin_path):
                print(f"[✗] 导出命令已退出但未生成 .bin: {bin_path}")
                ok = False
            results["export"] = "PASS" if ok else "FAIL"

    app_path: Optional[str] = None
    if args.skip_package:
        results["package"] = "SKIP"
    else:
        uat = run_uat_path(engine_root)
        if uat is None:
            print("[✗] 未找到 RunUAT 打包入口")
            results["package"] = "FAIL"
        else:
            if not args.keep_zen_store:
                ensure_zen_store_disabled(project_root)
            if os.path.isdir(archive_dir):
                shutil.rmtree(archive_dir)
            package_cmd = package_command(
                uat, uproject_path, archive_dir, target_platform, args.client_config, minor_version
            )
            ok, _ = verify_install.run_logged(
                "BuildCookRun 打包", package_cmd, project_root,
                os.path.join(log_dir, "VerifyPackage_Packaging.log"), timeout_seconds)
            if ok:
                app_path = find_packaged_app(archive_dir, project_name)
                if app_path is None:
                    print(f"[✗] 打包成功但未在归档目录找到可执行体: {archive_dir}")
                    ok = False
            results["package"] = "PASS" if ok else "FAIL"

    staged_bin, staged_json = find_staged_data(archive_dir) if os.path.isdir(archive_dir) else (None, None)
    if results.get("package") == "SKIP":
        results["staging"] = "SKIP"
    else:
        if staged_bin is not None and not args.no_stage_json:
            staged_json = stage_json_for_qa(project_root, staged_project_dir(staged_bin)) or staged_json
        if staged_bin is None or staged_json is None:
            print(f"[✗] 包内缺少随包数据：.bin={staged_bin} JSON={staged_json}")
            results["staging"] = "FAIL"
        else:
            print(f"[✓] 包内 .bin : {staged_bin}")
            print(f"[✓] 包内 JSON: {staged_json}")
            results["staging"] = "PASS"

    if args.skip_verify or results.get("package") == "SKIP":
        results["channel_diff"] = "SKIP"
    elif app_path is None:
        results["channel_diff"] = "FAIL"
    else:
        update_packaged_paths(config_path, uproject_path, app_path)
        if args.client_config.lower() == "shipping":
            print("[-] Shipping 构建裁剪日志输出，改用启动自检报告判定装载结果")
            try:
                launch_ok = run_shipping_launch(config_path)
            except (RuntimeError, FileNotFoundError) as error:
                print(f"[✗] Shipping 无头启动失败: {error}")
                launch_ok = False
            results["launch"] = "PASS" if launch_ok else "FAIL"
            results["channel_diff"] = "SKIP"
        else:
            try:
                diff_ok = run_channel_diff(config_path) == 0
            except RuntimeError as error:
                print(f"[✗] 双通道对拍失败: {error}")
                diff_ok = False
            results["channel_diff"] = "PASS" if diff_ok else "FAIL"

    print("\n" + "=" * 78)
    print(" 打包冒烟摘要")
    for step in ("install", "generate", "build", "export", "package", "staging", "channel_diff", "launch"):
        if step not in results:
            continue
        print(f" {step:<14}: {results.get(step, 'SKIP')}")
    if app_path:
        print(f" 打包产物      : {app_path}")
    print(f" 日志目录      : {log_dir}")
    overall_ok = all(value in ("PASS", "SKIP") for value in results.values())
    print(f" 总体结果      : {'PASS' if overall_ok else 'FAIL'}")
    print("=" * 78)
    sys.exit(0 if overall_ok else 1)


if __name__ == "__main__":
    main()
