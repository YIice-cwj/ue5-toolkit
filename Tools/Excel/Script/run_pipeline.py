#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表全流程管线调度器 (run_pipeline.py) —— 跨平台双向单点调度入口

- --all (默认)：依次执行 validate_tables.py / main.py / excel_to_json.py / gameplay_tags_to_ini.py
- --reverse：调用 json_to_excel.py 逆向反写回 Excel（写前快照 + 原子落盘），
  写盘后自动调用 validate_tables.py 后验；后验失败自动回滚到写前快照
- --rollback：按快照清单把写前文件还原回 Excel（可配合 --snapshot / --table）
- --roundtrip：调用 roundtrip_check.py 做往返幂等自检（真实 Excel 只读）
- --bin：调用编辑器控制台命令产出运行期 .bin（Cook 前置）
"""

import argparse
import os
import subprocess
import sys
import time
from typing import List

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import json_to_excel  # noqa: E402
import tool_config  # noqa: E402

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EDITOR_CMD_ENV_KEY = "UE_EDITOR_CMD"


def run_script(python_bin: str, script_name: str, config_path: str, extra_args: List[str]) -> bool:
    """
    执行单个工具脚本（stdout/stderr 直通，失败即中止）
    @param python_bin Python 解释器路径
    @param script_name Script 目录下脚本文件名
    @param config_path 配置文件绝对路径
    @param extra_args 透传给子脚本的额外参数
    @return 子进程返回码为 0 返回 True
    """
    script_path = os.path.join(SCRIPT_DIR, script_name)
    if not os.path.isfile(script_path):
        print(f"[错误] 找不到脚本: {script_path}", file=sys.stderr)
        return False

    cmd = [python_bin, script_path, "--config", config_path] + list(extra_args)
    print(f"[执行] {' '.join(cmd)}")
    result = subprocess.run(cmd, check=False)
    return result.returncode == 0


def run_export_bin(config_path: str) -> bool:
    """
    执行 .bin 导出（编辑器控制台命令）并校验退出码与产物更新
    @param config_path 工具配置文件路径
    @return 编辑器零码退出且产物已更新返回 True
    """
    raw = tool_config.load_config(config_path, ())
    project_file = raw.get("project_file") or ""
    bin_content_path = raw.get("static_data_bin_content_path") or ""
    editor_cmd = raw.get("unreal_editor_cmd") or os.environ.get(EDITOR_CMD_ENV_KEY) or ""
    export_bin_command = raw.get("export_bin_command") or f"{raw.get('project_prefix', 'Game')}.ExportStaticDataBin"

    if not project_file or not bin_content_path:
        print("[错误] 配置缺少 project_file 或 static_data_bin_content_path，无法导出 .bin。", file=sys.stderr)
        return False
    if not editor_cmd or not os.path.isfile(editor_cmd):
        print(f"[错误] 编辑器可执行文件不可用（配置 unreal_editor_cmd 或环境变量 {EDITOR_CMD_ENV_KEY}）: {editor_cmd}",
              file=sys.stderr)
        return False

    bin_path = os.path.join(os.path.dirname(project_file), "Content", *bin_content_path.split("/"))
    prev_mtime = os.path.getmtime(bin_path) if os.path.isfile(bin_path) else 0.0
    started_at = time.time()
    cmd = [
        editor_cmd, project_file,
        "-game", "-nullrhi", "-unattended", "-nop4", "-nosplash", "-log",
        f"-ExecCmds={export_bin_command},Quit",
    ]
    print(f"[执行] {' '.join(cmd)}")
    proc = subprocess.run(cmd, check=False)
    if proc.returncode != 0:
        print(f"[错误] 编辑器进程以非零状态退出（exit={proc.returncode}），.bin 导出失败。", file=sys.stderr)
        return False

    if not os.path.isfile(bin_path):
        print(f"[错误] 未产出 .bin 文件（期望产物: {bin_path}）。", file=sys.stderr)
        return False

    new_mtime = os.path.getmtime(bin_path)
    if new_mtime <= prev_mtime and new_mtime < started_at - 1.0:
        print(f"[错误] .bin 文件未更新（期望产物: {bin_path}）；导出失败原因见编辑器日志。", file=sys.stderr)
        return False

    print(f"[✓] .bin 导出完成: {bin_path}")
    return True


def print_usage() -> None:
    """
    打印数据表全流程管线调度器命令行使用帮助
    """
    print("使用方式: python3 run_pipeline.py [--config <配置文件路径>] [模式] [选项...]")
    print("  --config <path>         工具配置文件（JSON）；未指定时依次尝试环境变量 UE_TOOL_CONFIG、")
    print("                          当前目录、Tools/Excel 目录")
    print("  --all        (默认) 执行完整正向管线: 校验 + C++ 生成 + JSON 导出 + GameplayTag 注册")
    print("  --validate   仅执行数据表完整性校验")
    print("  --struct     仅生成 C++ RowStruct/容器/子系统/序列化器")
    print("  --json       仅导出 JSON 数据表")
    print("  --tags       仅生成 GameplayTag 注册 ini（依据 Excel 的 FGameplayTag 列）")
    print("  --bin        仅导出运行期 .bin（Cook 前置；需编辑器已编译，且不在 --all 内联执行）")
    print("  --reverse    逆向同步模式: 将 JSON 反写回 Excel（写前快照 + 原子落盘），并自动运行完整性校验")
    print("               校验不通过时会自动回滚到写前快照")
    print("  --rollback   回滚模式: 按快照清单把 Excel 还原至写前状态（可配合 --snapshot / --table）")
    print("  --snapshot <dir> 回滚时指定快照目录（缺省取最新时间戳快照）")
    print("  --roundtrip  往返幂等自检: 正向 → 反向 → 正向，比较两份 JSON 是否逐字节一致（真实 Excel 只读）")
    print("  --dry-run    （逆向/往返模式可用）仅预览差异变动或仅测正向确定性，不修改 Excel 文件")
    print("  --table <Name> 仅针对指定单表执行操作（如 DT_Example_Table）")
    print("  --allow-delete （逆向模式可用）允许物理删除 Excel 中存在但 JSON 中缺失的行")
    print("  --force-formula-overwrite （逆向模式可用）强制覆盖计算公式单元格")
    print("  --fill-defaults （逆向模式可用）空值/null 回填对应 C++ 类型默认零值而非保持留空")
    print("  --float-epsilon <eps> （逆向模式可用）指定浮点比较容差（默认 1e-6）")
    print("  --no-backup  （逆向模式可用）禁用写前安全备份（注意：同时失去自动回滚能力）")
    print("  --strict     （逆向模式可用）存在转换失败/规则违规/结构漂移时以非零退出码结束")


def _resolve_mode(known: argparse.Namespace) -> str:
    """
    依据命令行开关解析执行模式（若同时指定多个互斥模式则抛出 ValueError）
    @param known argparse 已解析参数
    @return 模式名字符串
    """
    all_modes = ("all", "reverse", "rollback", "roundtrip", "validate", "struct", "json", "tags", "bin")
    active = [m for m in all_modes if getattr(known, m, False)]
    if len(active) > 1:
        raise ValueError(f"不能同时指定多个互斥执行模式: {', '.join('--' + m for m in active)}")
    if len(active) == 1:
        return active[0]
    return "all"


def _run_reverse_mode(python_bin: str, config_path: str, known: argparse.Namespace) -> bool:
    """
    执行逆向回写模式，并在非 dry-run 时自动执行校验及失败回滚
    @return 成功返回 True，失败返回 False
    """
    reverse_args: List[str] = []
    if known.table:
        reverse_args.extend(["--table", known.table])
    if known.dry_run:
        reverse_args.append("--dry-run")
    if known.allow_delete:
        reverse_args.append("--allow-delete")
    if known.force_formula:
        reverse_args.append("--force-formula-overwrite")
    if known.fill_defaults:
        reverse_args.append("--fill-defaults")
    if known.float_epsilon is not None:
        reverse_args.extend(["--float-epsilon", str(known.float_epsilon)])
    if known.no_backup:
        reverse_args.append("--no-backup")
    if known.strict:
        reverse_args.append("--strict")

    backup_dir = ""
    if not known.no_backup:
        backup_cfg = tool_config.load_config(config_path, (tool_config.KEY_DATA_TABLES_SOURCE_DIR,))
        backup_dir = json_to_excel.resolve_backup_dir(backup_cfg, config_path)
    snapshot_before = json_to_excel.resolve_snapshot_dir(backup_dir)

    if not run_script(python_bin, "json_to_excel.py", config_path, reverse_args):
        if not known.no_backup and not known.dry_run:
            snapshot_after_fail = json_to_excel.resolve_snapshot_dir(backup_dir)
            if snapshot_after_fail is not None and snapshot_after_fail != snapshot_before:
                print(f"\n[反向同步异常恢复] 回写过程中止，正在从写前快照回滚已修改的表: {snapshot_after_fail} ...")
                fail_rb_args = ["--rollback", "--snapshot", snapshot_after_fail]
                if known.table:
                    fail_rb_args.extend(["--table", known.table])
                run_script(python_bin, "json_to_excel.py", config_path, fail_rb_args)
        return False
    if known.dry_run:
        return True

    print("\n[反向同步后验] 自动执行数据表完整性校验...")
    if run_script(python_bin, "validate_tables.py", config_path, []):
        return True

    if known.no_backup:
        print("\n[反向同步后验] 校验未通过，且本次以 --no-backup 运行，无写前快照可回滚！请手动检查 Excel 改动。", file=sys.stderr)
        return False

    snapshot_after = json_to_excel.resolve_snapshot_dir(backup_dir)
    if snapshot_after is None or snapshot_after == snapshot_before:
        print("\n[反向同步后验] 本次回写未产生任何变动（无写前快照），无需回滚；校验失败可能源于 Excel 既有内容。", file=sys.stderr)
        return False

    print(f"\n[反向同步后验] 校验未通过 → 自动回滚到本次写前快照 {snapshot_after} ...")
    rollback_args = ["--rollback", "--snapshot", snapshot_after]
    if known.table:
        rollback_args.extend(["--table", known.table])
    if not run_script(python_bin, "json_to_excel.py", config_path, rollback_args):
        print("[反向同步后验] 回滚失败！请手动从备份目录恢复。", file=sys.stderr)
        return False

    print("[反向同步后验] 回滚完成（Excel 已恢复至写前状态）。")
    return False


def _dispatch_mode(mode: str, python_bin: str, config_path: str, known: argparse.Namespace, extra_args: List[str]) -> bool:
    """
    按选定模式分派具体管线步骤
    @return 全部步骤成功返回 True
    """
    if extra_args and mode not in ("all", "struct", "roundtrip"):
        print(f"[错误] 模式 --{mode} 不支持未知参数: {' '.join(extra_args)}", file=sys.stderr)
        return False
    if mode == "reverse":
        return _run_reverse_mode(python_bin, config_path, known)
    if mode == "rollback":
        rollback_args = ["--rollback"]
        if known.snapshot:
            rollback_args.extend(["--snapshot", known.snapshot])
        if known.table:
            rollback_args.extend(["--table", known.table])
        return run_script(python_bin, "json_to_excel.py", config_path, rollback_args)
    if mode == "roundtrip":
        roundtrip_args = (["--no-write"] if known.dry_run else []) + list(extra_args)
        return run_script(python_bin, "roundtrip_check.py", config_path, roundtrip_args)
    if mode == "bin":
        return run_export_bin(config_path)

    steps = []
    if mode in ("all", "validate"):
        steps.append(("validate_tables.py", []))
    if mode in ("all", "struct"):
        steps.append(("main.py", extra_args))
    if mode in ("all", "json"):
        steps.append(("excel_to_json.py", []))
    if mode in ("all", "tags"):
        steps.append(("gameplay_tags_to_ini.py", []))

    for script_name, script_args in steps:
        if not run_script(python_bin, script_name, config_path, script_args):
            return False
    return True


def main() -> None:
    """
    按选定模式调度数据表管线（正向 / 逆向 / 回滚 / 往返 / .bin 导出）
    """
    parser = argparse.ArgumentParser(description="数据表全流程管线调度器（跨平台双向单点调度入口）")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parser.add_argument("--all", action="store_true", help="执行完整管线（默认）")
    parser.add_argument("--validate", action="store_true", help="仅数据表校验")
    parser.add_argument("--struct", action="store_true", help="仅生成 C++ 结构")
    parser.add_argument("--json", action="store_true", help="仅导出 JSON")
    parser.add_argument("--tags", action="store_true", help="仅生成 GameplayTag 注册 ini")
    parser.add_argument("--bin", action="store_true", help="仅导出运行期 .bin（Cook 前置）")
    parser.add_argument("--reverse", dest="reverse", action="store_true", help="逆向反写模式：将 JSON 数据回写至 Excel")
    parser.add_argument("--rollback", dest="rollback", action="store_true", help="回滚模式：按写前快照把 Excel 还原")
    parser.add_argument("--roundtrip", dest="roundtrip", action="store_true", help="往返幂等自检：正向 → 反向 → 正向 比较")
    parser.add_argument("--snapshot", dest="snapshot", default=None, help="回滚时指定快照目录（缺省取最新快照）")
    parser.add_argument("--table", dest="table", default=None, help="指定单张数据表名（如 DT_Example_Table）")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true", help="仅预览变更差异，不触碰磁盘")
    parser.add_argument("--allow-delete", dest="allow_delete", action="store_true", help="逆向反写时允许删除行")
    parser.add_argument("--force-formula-overwrite", dest="force_formula", action="store_true", help="逆向反写时强制覆盖公式")
    parser.add_argument("--fill-defaults", dest="fill_defaults", action="store_true", help="逆向反写时空值写入类型默认零值")
    parser.add_argument("--float-epsilon", dest="float_epsilon", type=float, default=None, help="逆向反写浮点容差")
    parser.add_argument("--no-backup", dest="no_backup", action="store_true", help="逆向反写时禁用写前快照备份")
    parser.add_argument("--strict", dest="strict", action="store_true", help="逆向反写时存在违规即非零退出（CI 门）")
    known, extra_args = parser.parse_known_args()

    try:
        mode = _resolve_mode(known)
    except ValueError as mode_err:
        print(f"[错误] {mode_err}", file=sys.stderr)
        print_usage()
        sys.exit(1)
    config_path = tool_config.resolve_config_path(known.config_path, (os.path.dirname(SCRIPT_DIR),))
    if config_path is None:
        if known.config_path:
            print(f"[错误] 配置文件不存在: {known.config_path}", file=sys.stderr)
        else:
            print("[错误] 未找到配置文件：未提供 --config、环境变量 UE_TOOL_CONFIG，"
                  "且当前目录与 Tools/Excel 目录下均不存在 generator_config.json。", file=sys.stderr)
        print_usage()
        sys.exit(1)

    python_bin = sys.executable

    print("=" * 65)
    print(" 数据表管线启动（配置驱动 / 跨平台双向调度）")
    print(f" 配置文件   : {config_path}")
    print(f" 执行模式   : {mode}")
    print(f" Python     : {python_bin}")
    print("=" * 65)
    print("")

    if not _dispatch_mode(mode, python_bin, config_path, known, extra_args):
        sys.exit(1)

    print("=" * 65)
    print("[完成] 数据表管线执行完毕！")
    print("=" * 65)


if __name__ == "__main__":
    main()
