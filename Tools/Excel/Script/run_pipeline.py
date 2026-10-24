#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据表全流程管线调度器 (run_pipeline.py) —— 跨平台双向单点调度入口

- 依次调用 validate_tables.py / main.py / excel_to_json.py / gameplay_tags_to_ini.py（正向流水线）
- --reverse / --json-to-excel：调用 json_to_excel.py 逆向反写回 Excel（写前快照 + 原子落盘），
  写盘后自动调用 validate_tables.py 后验；后验失败会自动回滚到写前快照
- --rollback：按快照清单把写前文件还原回 Excel（可配合 --snapshot / --table）
- --roundtrip / --verify-roundtrip：调用 roundtrip_check.py 做往返幂等自检（真实 Excel 只读）
- --bin 步骤：调用编辑器控制台命令产出运行期 .bin（Cook 前置）
- 本脚本不内置任何工程路径与默认配置：所有路径与参数来自配置文件（必填）

用法:
  python3 run_pipeline.py [--config <path>] [--all | --validate | --struct | --json | --tags | --bin | --reverse | --rollback | --roundtrip] [额外参数...]
"""

import argparse
import os
import subprocess
import sys
import time
from typing import List

import json_to_excel
import tool_config

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

MODE_CHOICES = ("all", "validate", "struct", "json", "tags", "bin", "reverse", "rollback", "roundtrip")

# 编辑器可执行文件的环境变量回退键（配置项 unreal_editor_cmd 优先）
EDITOR_CMD_ENV_KEY = "UE_EDITOR_CMD"


def run_script(python_bin: str, script_name: str, config_path: str, extra_args: List[str]) -> bool:
    """执行单个工具脚本（stdout/stderr 直通，失败即中止）"""
    script_path = os.path.join(SCRIPT_DIR, script_name)
    if not os.path.isfile(script_path):
        print(f"[错误] 找不到脚本: {script_path}", file=sys.stderr)
        return False

    cmd = [python_bin, script_path, "--config", config_path] + list(extra_args)
    print(f"[执行] {' '.join(cmd)}")
    result = subprocess.run(cmd)
    return result.returncode == 0


def run_export_bin(config_path: str) -> bool:
    """
    执行 .bin 导出（编辑器控制台命令）并校验产物是否更新
    @param config_path 工具配置文件路径
    @return 导出产物已更新返回 true
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
    started_at = time.time()
    cmd = [
        editor_cmd, project_file,
        "-game", "-nullrhi", "-unattended", "-nop4", "-nosplash", "-log",
        f"-ExecCmds={export_bin_command},Quit",
    ]
    print(f"[执行] {' '.join(cmd)}")
    subprocess.run(cmd, check=False)

    if not os.path.isfile(bin_path) or os.path.getmtime(bin_path) < started_at:
        print(f"[错误] 未产出新的 .bin（期望产物: {bin_path}）；导出失败原因见编辑器日志。", file=sys.stderr)
        return False

    print(f"[✓] .bin 导出完成: {bin_path}")
    return True


def print_usage() -> None:
    """打印使用帮助"""
    print("使用方式: python3 run_pipeline.py [--config <配置文件路径>] [选项] [生成器额外参数...]")
    print("  --config <path>         工具配置文件（JSON）；未指定时依次尝试环境变量 UE_TOOL_CONFIG、")
    print("                          UE_STATIC_DATA_CONFIG、当前目录、Tools/Excel 目录")
    print("  --all        (默认) 执行完整正向管线: 校验 + C++ 生成 + JSON 导出 + GameplayTag 注册")
    print("  --validate   仅执行数据表完整性校验")
    print("  --struct     仅生成 C++ RowStruct/容器/子系统/序列化器")
    print("  --json       仅导出 JSON 数据表")
    print("  --tags       仅生成 GameplayTag 注册 ini（依据 Excel 的 FGameplayTag 列）")
    print("  --bin        仅导出运行期 .bin（Cook 前置；需编辑器已编译，且不在 --all 内联执行）")
    print("  --reverse    逆向同步模式: 将 JSON 反写回 Excel（写前快照 + 原子落盘），并自动运行完整性校验")
    print("               校验不通过时会自动回滚到写前快照（等价于 --json-to-excel）")
    print("  --rollback   回滚模式: 按快照清单把 Excel 还原至写前状态（可配合 --snapshot / --table）")
    print("  --snapshot <dir> 回滚时指定快照目录（缺省取最新时间戳快照）")
    print("  --roundtrip  往返幂等自检: 正向 → 反向 → 正向，比较两份 JSON 是否逐字节一致")
    print("               （真实 Excel 只读，全程在临时副本上执行；等价于 --verify-roundtrip）")
    print("  --dry-run    （逆向模式可用）仅预览差异变动，不修改 Excel 文件")
    print("  --table <Name> 仅针对指定单表执行操作（如 DT_Example_Table）")
    print("  --allow-delete （逆向模式可用）允许物理删除 Excel 中存在但 JSON 中缺失的行")
    print("  --force-formula-overwrite （逆向模式可用）强制覆盖计算公式单元格")
    print("  --no-backup  （逆向模式可用）禁用写前安全备份（注意：同时失去自动回滚能力）")
    print("  --strict     （逆向模式可用）存在转换失败时以非零退出码结束（供 CI 门使用）")


def main() -> None:
    """
    按选定模式调度数据表管线

    实现思路：逆向模式先记录写前快照、执行回写并自动后验（失败即回滚到本次快照）；
    回滚 / 往返 / 导出各走独立分支；默认 all 依次执行 validate → struct → json → tags。
    """
    parser = argparse.ArgumentParser(description="数据表全流程管线调度器（跨平台双向单点调度入口）")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parser.add_argument("--mode", choices=MODE_CHOICES, default=None,
                        help="执行模式（默认 --all，等价于 --mode all）")
    parser.add_argument("--all", action="store_true", help="执行完整管线（默认）")
    parser.add_argument("--validate", action="store_true", help="仅数据表校验")
    parser.add_argument("--struct", action="store_true", help="仅生成 C++ 结构")
    parser.add_argument("--json", action="store_true", help="仅导出 JSON")
    parser.add_argument("--tags", action="store_true", help="仅生成 GameplayTag 注册 ini")
    parser.add_argument("--bin", action="store_true", help="仅导出运行期 .bin（Cook 前置）")
    parser.add_argument("--reverse", "--json-to-excel", dest="reverse", action="store_true",
                        help="逆向反写模式：将 JSON 数据回写至 Excel")
    parser.add_argument("--rollback", dest="rollback", action="store_true",
                        help="回滚模式：按写前快照把 Excel 还原（可配合 --snapshot / --table）")
    parser.add_argument("--roundtrip", "--verify-roundtrip", dest="roundtrip", action="store_true",
                        help="往返幂等自检：正向 → 反向 → 正向 比较（真实 Excel 只读，副本上执行）")
    parser.add_argument("--snapshot", dest="snapshot", default=None, help="回滚时指定快照目录（缺省取最新快照）")
    parser.add_argument("--table", dest="table", default=None, help="指定单张数据表名（如 DT_Example_Table）")
    parser.add_argument("--dry-run", dest="dry_run", action="store_true", help="仅预览变更差异，不触碰磁盘")
    parser.add_argument("--allow-delete", dest="allow_delete", action="store_true", help="逆向反写时允许删除行")
    parser.add_argument("--force-formula-overwrite", dest="force_formula", action="store_true", help="逆向反写时强制覆盖公式")
    parser.add_argument("--fill-defaults", dest="fill_defaults", action="store_true", help="逆向反写时空值写入类型默认零值")
    parser.add_argument("--float-epsilon", dest="float_epsilon", type=float, default=None, help="逆向反写浮点容差")
    parser.add_argument("--no-backup", dest="no_backup", action="store_true", help="逆向反写时禁用写前快照备份")
    parser.add_argument("--strict", dest="strict", action="store_true", help="逆向反写时存在转换失败即非零退出（CI 门）")
    parser.add_argument("--help-passthrough", action="store_true", help=argparse.SUPPRESS)
    known, extra_args = parser.parse_known_args()

    if known.mode:
        mode = known.mode
    elif known.reverse:
        mode = "reverse"
    elif known.rollback:
        mode = "rollback"
    elif known.roundtrip:
        mode = "roundtrip"
    elif known.validate:
        mode = "validate"
    elif known.struct:
        mode = "struct"
    elif known.json:
        mode = "json"
    elif known.tags:
        mode = "tags"
    elif known.bin:
        mode = "bin"
    else:
        mode = "all"

    config_path = tool_config.resolve_config_path(known.config_path, (os.path.dirname(SCRIPT_DIR),))
    if config_path is None:
        if known.config_path:
            print(f"[错误] 配置文件不存在: {known.config_path}", file=sys.stderr)
        else:
            print("[错误] 未找到配置文件：未提供 --config、环境变量 UE_TOOL_CONFIG / UE_STATIC_DATA_CONFIG，"
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

    if mode == "reverse":
        reverse_args = []
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
            sys.exit(1)

        if not known.dry_run:
            print("\n[反向同步后验] 自动执行数据表完整性校验...")
            if not run_script(python_bin, "validate_tables.py", config_path, []):
                if known.no_backup:
                    print("\n[反向同步后验] 校验未通过，且本次以 --no-backup 运行，无写前快照可回滚！"
                          "请手动检查 Excel 改动。", file=sys.stderr)
                    sys.exit(1)

                snapshot_after = json_to_excel.resolve_snapshot_dir(backup_dir)
                if snapshot_after is None or snapshot_after == snapshot_before:
                    print("\n[反向同步后验] 本次回写未产生任何变动（无写前快照），无需回滚；"
                          "校验失败可能源于 Excel 既有内容，请人工排查。", file=sys.stderr)
                    sys.exit(1)

                print(f"\n[反向同步后验] 校验未通过 → 自动回滚到本次写前快照 {snapshot_after} ...")
                rollback_args = ["--rollback", "--snapshot", snapshot_after]
                if known.table:
                    rollback_args.extend(["--table", known.table])
                if not run_script(python_bin, "json_to_excel.py", config_path, rollback_args):
                    print("[反向同步后验] 回滚失败！请手动从备份目录恢复。", file=sys.stderr)
                    sys.exit(1)
                print("[反向同步后验] 回滚完成（Excel 已恢复至写前状态）。")
                sys.exit(1)

    if mode == "rollback":
        rollback_args = ["--rollback"]
        if known.snapshot:
            rollback_args.extend(["--snapshot", known.snapshot])
        if known.table:
            rollback_args.extend(["--table", known.table])
        if not run_script(python_bin, "json_to_excel.py", config_path, rollback_args):
            sys.exit(1)

    if mode == "roundtrip":
        roundtrip_args = []
        if known.dry_run:
            roundtrip_args.append("--no-write")
        if not run_script(python_bin, "roundtrip_check.py", config_path, roundtrip_args):
            sys.exit(1)

    if mode in ("all", "validate"):
        if not run_script(python_bin, "validate_tables.py", config_path, []):
            sys.exit(1)
    if mode in ("all", "struct"):
        if not run_script(python_bin, "main.py", config_path, extra_args):
            sys.exit(1)
    if mode in ("all", "json"):
        if not run_script(python_bin, "excel_to_json.py", config_path, []):
            sys.exit(1)
    if mode in ("all", "tags"):
        if not run_script(python_bin, "gameplay_tags_to_ini.py", config_path, []):
            sys.exit(1)
    if mode == "bin":
        if not run_export_bin(config_path):
            sys.exit(1)

    print("=" * 65)
    print("[完成] 数据表管线执行完毕！")
    print("=" * 65)


if __name__ == "__main__":
    main()