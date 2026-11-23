#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表自动生成 C++ USTRUCT 头文件工具 (main.py)

- 零配置全自动化：读取工作表标签名 (如 DT_Item_Definition) 自动推导 FItemDefinitionRow
- 目录分类自动镜像：随 xlsx 子目录 (DataTables/<Category>/) 输出到对应 C++ 目录
- 表头三行契约：中文名 -> UPROPERTY 注释/DisplayName；snake_case 英文名 -> 成员名；C++ 类型 -> 声明类型
- 事务化原子换入：全部产物落入暂存树后由 GenerationStaging.commit() 原子替换磁盘，失败零污染
- artifacts 配置开关：config 中可选的 "artifacts" JSON 对象（缺失时全 true），按需开关各产物生成器
"""

import argparse
import dataclasses
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from typing import Any, Callable, Dict, List, Optional, Set, Tuple

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import table_schema  # noqa: E402
import tool_config  # noqa: E402
from excel_parser import get_sheet_data, iter_xlsx_files  # noqa: E402
from file_writer import GenerationStaging  # noqa: E402
from generators import (  # noqa: E402
    GeneratorContext,
    aggregator_generator,
    composite_data_generator,
    game_registration_generator,
    interface_generator,
    query_generator,
    registration_generator,
    row_struct_generator,
    serialization_generator,
    service_generator,
    static_data_generator,
    static_data_registration_generator,
    static_data_snapshot_generator,
    subsystem_generator,
)
from naming_utils import strip_dt_prefix, to_pascal_case  # noqa: E402

ARTIFACT_NAMES = (
    "row_struct", "composite_data", "static_data", "snapshot_store",
    "static_data_registration", "game_registration",
    "interface", "query", "service", "subsystem", "serialization",
    "aggregators", "registration",
)

_VALID_CATEGORY_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


@dataclasses.dataclass
class ToolConfig:
    """
    生成器运行配置（全部来自配置文件，无内置硬编码路径）

    输出落层：以 output_module_dir 为模块根，声明落 {module}/Public/Generated、
    实现落 {module}/Private/Generated（声明/实现分开，stale 孤儿文件随事务替换自动回收）
    """
    data_tables_source_dir: str
    output_module_dir: str
    api_macro: str
    subsystem_header_include: str
    json_root_dir_name: str
    static_data_bin_content_path: str
    row_types_include_prefix: str = "Generated/DataTables"
    static_data_include_prefix: str = "Generated/Data/StaticData"
    project_prefix: str = "Game"
    blueprint_category: str = "Game|StaticData"
    log_header: str = "Logging/GameLogCategories.h"
    log_macro: str = "GAME_LOG"
    log_category_name: str = "LogGame"
    export_bin_command: str = ""
    report_integrity_command: str = ""
    static_data_channel_cmd_arg: str = ""
    verify_launch_switch: str = ""
    export_commandlet: str = ""
    registration_class_name: str = ""
    game_instance_class_name: str = ""
    row_serializer_register_func: str = ""
    game_config_defaults: dict = dataclasses.field(default_factory=dict)
    row_key_mode: str = "fname"
    nested_struct_types: dict = dataclasses.field(default_factory=dict)

    @property
    def cpp_output_base_dir(self) -> str:
        """
        获取 RowStruct 行头输出根目录（{module}/Public/Generated/DataTables）
        @return RowStruct 输出根路径
        """
        return os.path.join(self.output_module_dir, "Public", "Generated", "DataTables")

    @property
    def static_data_public_dir(self) -> str:
        """
        获取静态数据声明输出根目录（{module}/Public/Generated/Data/StaticData）
        @return 静态数据 Public 输出根路径
        """
        return os.path.join(self.output_module_dir, "Public", "Generated", "Data", "StaticData")

    @property
    def static_data_private_dir(self) -> str:
        """
        获取静态数据实现输出根目录（{module}/Private/Generated/Data/StaticData）
        @return 静态数据 Private 输出根路径
        """
        return os.path.join(self.output_module_dir, "Private", "Generated", "Data", "StaticData")

    @property
    def registration_public_dir(self) -> str:
        """
        获取注册入口声明输出根目录（{module}/Public/Generated/Registration）
        @return 注册入口 Public 输出根路径
        """
        return os.path.join(self.output_module_dir, "Public", "Generated", "Registration")

    @property
    def registration_private_dir(self) -> str:
        """
        获取注册入口实现输出根目录（{module}/Private/Generated/Registration）
        @return 注册入口 Private 输出根路径
        """
        return os.path.join(self.output_module_dir, "Private", "Generated", "Registration")

    def generated_roots(self) -> List[str]:
        """
        获取全部生成域根目录列表
        @return 生成域根目录列表
        """
        return [
            self.cpp_output_base_dir,
            self.static_data_public_dir,
            self.static_data_private_dir,
            self.registration_public_dir,
            self.registration_private_dir,
        ]


def build_config_from_raw(raw: Dict[str, Any], overrides: Optional[Dict[str, Any]] = None) -> ToolConfig:
    """
    从已加载配置字典构建 ToolConfig 并应用命令行覆盖
    @param raw tool_config.load_config 返回的原始配置字典
    @param overrides 可选覆盖字典（键名与 ToolConfig 字段一一对应）
    @return 生效的 ToolConfig
    """
    field_names = {field.name for field in dataclasses.fields(ToolConfig)}
    merged = {key: value for key, value in raw.items() if key in field_names}
    if overrides:
        for key, value in overrides.items():
            if value is not None and key in field_names:
                merged[key] = value
    return ToolConfig(**merged)


def build_config(config_path: Optional[str], overrides: Optional[Dict[str, Any]] = None) -> ToolConfig:
    """
    构建运行配置：配置文件（必填）→ 命令行覆盖
    @param config_path --config 传入的 JSON 配置文件路径
    @param overrides 可选覆盖字典
    @return 生效的 ToolConfig
    """
    raw = tool_config.load_config(config_path, tool_config.KEYS_STRUCT)
    return build_config_from_raw(raw, overrides)


def load_artifacts_from_raw(raw: Dict[str, Any]) -> Dict[str, bool]:
    """
    从已加载配置字典读取 artifacts 开关（缺省全 True）
    @param raw 原始配置字典
    @return 产物名 -> 是否启用的字典
    """
    artifacts_cfg = raw.get("artifacts", {})
    if not isinstance(artifacts_cfg, dict):
        artifacts_cfg = {}
    return {name: bool(artifacts_cfg.get(name, True)) for name in ARTIFACT_NAMES}


def load_artifacts(config_path: Optional[str]) -> Dict[str, bool]:
    """
    读取 artifacts 配置开关（可选的 JSON 对象，缺失时全 true）
    @param config_path --config 传入的 JSON 配置文件路径
    @return 产物名 -> 是否启用的字典
    """
    raw = tool_config.load_config(config_path, tool_config.KEYS_STRUCT)
    return load_artifacts_from_raw(raw)


def _parse_args() -> argparse.Namespace:
    """
    解析命令行参数
    @return 解析后的命令行参数命名空间
    """
    parser = argparse.ArgumentParser(description="Excel 数据表通用 C++ 生成器")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parser.add_argument("--source-dir", dest="data_tables_source_dir", default=None, help="Excel 源目录（覆盖配置文件）")
    parser.add_argument("--output-module-dir", dest="output_module_dir", default=None, help="生成产物输出模块根（覆盖配置文件）")
    parser.add_argument("--api-macro", dest="api_macro", default=None, help="生成类型的 API 导出宏（覆盖配置文件）")
    parser.add_argument("--subsystem-header-include", dest="subsystem_header_include", default=None, help="Subsystem 头 include 相对路径（覆盖配置文件）")
    parser.add_argument("--json-root-dir-name", dest="json_root_dir_name", default=None, help="JSON 数据根目录名（覆盖配置文件）")
    parser.add_argument("--print-config", action="store_true", help="打印生效配置后退出")
    return parser.parse_args()


def _scan_xlsx_tables(
    source_dir: str,
) -> Tuple[List[Tuple[str, str, str, str, List[List[str]], List[Tuple[str, str]]]], List[str], bool]:
    """
    单次遍历扫描并解析目录下全部有效 .xlsx 文件，同步完成禁用字段名、分类目录名与表名冲突预检
    @param source_dir Excel 数据表根目录
    @return (已解析表条目列表, 禁用字段违规清单, 是否发生解析异常)
    """
    parsed_tables: List[Tuple[str, str, str, str, List[List[str]], List[Tuple[str, str]]]] = []
    reserved_violations: List[str] = []
    has_failure = False
    seen_stems: Dict[str, str] = {}

    for file_path in iter_xlsx_files(source_dir):
        xlsx_filename = os.path.basename(file_path)
        rel_dir = os.path.relpath(os.path.dirname(file_path), source_dir)
        json_rel_dir = "" if rel_dir == "." else rel_dir.replace("\\", "/")
        cat_folder = rel_dir if rel_dir != "." else "Shared"

        if not _VALID_CATEGORY_RE.match(cat_folder):
            print(f"[!] 分类目录名非法（仅支持单级合法 C++ 标识符目录）{xlsx_filename}: {rel_dir}", file=sys.stderr)
            has_failure = True
            continue

        try:
            sheet_name, headers, row_items = get_sheet_data(file_path)
        except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
            print(f"[!] 解析失败 {xlsx_filename}: {e}")
            has_failure = True
            continue

        if not sheet_name:
            print(f"[!] 无法读取下方工作表名称，跳过: {xlsx_filename}")
            continue
        if len(headers) < 3:
            print(f"[!] 表头契约不足 3 行，跳过: {xlsx_filename}")
            continue

        stem = to_pascal_case(sheet_name)
        if not stem or not _VALID_CATEGORY_RE.match(stem):
            print(f"[!] 工作表名无法推导合法 C++ 结构体名 {xlsx_filename}: {sheet_name}", file=sys.stderr)
            has_failure = True
            continue
        if stem in seen_stems:
            print(f"[!] 工作表名冲突：{xlsx_filename} 与 {seen_stems[stem]} 均映射到结构体 F{stem}Row", file=sys.stderr)
            has_failure = True
            continue
        seen_stems[stem] = xlsx_filename

        for col_idx, field_name in table_schema.find_reserved_field_names(headers[1]):
            reserved_violations.append(f"{xlsx_filename} 第 {col_idx} 列字段名为 {field_name!r}（该字段名被禁止）")

        parsed_tables.append((xlsx_filename, cat_folder, json_rel_dir, sheet_name, headers, row_items))

    return parsed_tables, reserved_violations, has_failure


def _cleanup_when_no_tables(cfg: ToolConfig, raw: Dict[str, Any]) -> None:
    """
    当目录下无任何有效数据表且无解析失败时，清理遗留的 Generated 目录与统一注册头
    @param cfg 生成器运行配置
    @param raw 原始配置字典
    """
    print("[!] 数据表目录下未检测到任何有效 Excel 表格。")
    public_gen = os.path.join(cfg.output_module_dir, "Public", "Generated")
    private_gen = os.path.join(cfg.output_module_dir, "Private", "Generated")
    cleaned_any = False
    for gen_dir in (public_gen, private_gen):
        if os.path.isdir(gen_dir):
            shutil.rmtree(gen_dir)
            cleaned_any = True
            print(f"[×] 已清理生成目录: {os.path.relpath(gen_dir, cfg.output_module_dir)}")

    reg_sec = raw.get("registration", {}) if isinstance(raw.get("registration"), dict) else {}
    out_header = reg_sec.get("output_header")
    if out_header and os.path.isfile(out_header):
        os.remove(out_header)
        print(f"[×] 已清理注册入口头文件: {out_header}")

    if cleaned_any:
        print("[✓] 由于当前无有效数据表，已完全删除所有 Generated 代码目录与产物。")
    else:
        print("[✓] 当前无有效数据表，且无遗留 Generated 产物。")


def _emit_all_artifacts(
    ctx: GeneratorContext,
    raw: Dict[str, Any],
    artifacts: Dict[str, bool],
    staging: GenerationStaging,
    emit: Callable[..., Tuple[str, bool]],
) -> None:
    """
    按依赖拓扑顺序执行全部已启用的生成器并将产物写入暂存区
    @param ctx 生成器运行上下文
    @param raw 原始配置字典
    @param artifacts 产物开关映射
    @param staging 事务化暂存区
    @param emit 统一写入回调
    """
    cfg = ctx.cfg

    if artifacts["aggregators"]:
        root_agg_path = os.path.join(cfg.cpp_output_base_dir, "DataTableRowTypes.h")
        static_types_path = os.path.join(cfg.static_data_public_dir, "StaticDataTypes.h")
        for gf in aggregator_generator.generate(ctx):
            emit(gf)
            if gf.path == static_types_path:
                print("[+] 自动生成静态数据主汇总头文件: Public/Generated/Data/StaticData/StaticDataTypes.h")
            elif gf.path == root_agg_path:
                print("[+] 自动生成全表主汇总头文件: Public/Generated/DataTables/DataTableRowTypes.h")
            else:
                print(f"[+] 自动生成分类汇总头文件: Public/Generated/DataTables/{os.path.relpath(gf.path, cfg.cpp_output_base_dir)}")

    if artifacts["composite_data"]:
        for gf in composite_data_generator.generate(ctx):
            emit(gf)
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir)
            print(f"[+] 自动生成单体聚合数据容器: Public/Generated/Data/StaticData/{rel}")

    if artifacts["interface"]:
        for gf in interface_generator.generate(ctx):
            action, _ = emit(gf, valid_map_names=gf.valid_map_names)
            is_h = gf.path.endswith(".h")
            prefix = "Public/Generated/Data/StaticData" if is_h else "Private/Generated/Data/StaticData"
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir if is_h else cfg.static_data_private_dir)
            if action == "preserved":
                label = "保留现有业务接口" if is_h else "保留现有业务接口实现"
                print(f"[-] {label}（不覆盖）: {prefix}/{rel}")
            else:
                action_desc = "自动修复" if action == "repaired" else "首次生成"
                label = "业务接口脚手架" if is_h else "业务接口实现脚手架"
                print(f"[+] {action_desc}{label}: {prefix}/{rel}")

    if artifacts["query"]:
        for gf in query_generator.generate(ctx):
            action, _ = emit(gf)
            is_h = gf.path.endswith(".h")
            prefix = "Public/Generated/Data/StaticData" if is_h else "Private/Generated/Data/StaticData"
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir if is_h else cfg.static_data_private_dir)
            if action == "preserved":
                label = "保留现有表查询" if is_h else "保留现有表查询实现"
                print(f"[-] {label}（不覆盖）: {prefix}/{rel}")
            else:
                label = "首次生成表查询骨架" if is_h else "首次生成表查询实现"
                print(f"[+] {label}: {prefix}/{rel}")

    if artifacts["service"]:
        for gf in service_generator.generate(ctx):
            action, _ = emit(gf)
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir)
            if action == "preserved":
                print(f"[-] 保留现有域服务（不覆盖）: Public/Generated/Data/StaticData/{rel}")
            else:
                print(f"[+] 首次生成域服务骨架: Public/Generated/Data/StaticData/{rel}")

    if artifacts["static_data"]:
        for gf in static_data_generator.generate(ctx):
            emit(gf)
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir)
            print(f"[+] 自动生成静态数据容器: Public/Generated/Data/StaticData/{rel}")

    if artifacts["snapshot_store"]:
        for gf in static_data_snapshot_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成静态数据快照/缓存层: {cfg.static_data_public_dir}（快照 / 缓存 / RCU 类型别名）")

    if artifacts["static_data_registration"]:
        for gf in static_data_registration_generator.generate(ctx):
            emit(gf)
        print("[+] 自动生成热更监听与注册入口: 热更监听器 / FStaticDataRegistration / .bin 导出命令行工具")

    if artifacts["game_registration"]:
        for gf in game_registration_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成游戏层注册类: {game_registration_generator.class_name_of(cfg)}")

    if artifacts["serialization"]:
        for gf in serialization_generator.generate(ctx):
            emit(gf)
            print(f"[+] 自动生成生成式行序列化器: {os.path.relpath(gf.path, cfg.output_module_dir)}")

    if artifacts["subsystem"] and ctx.category_tables:
        subsystem_h_path = os.path.join(cfg.static_data_public_dir, "UStaticDataSubsystem.h")
        for gf in subsystem_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成全局静态数据子系统: {subsystem_h_path} 及其 .cpp")

    if artifacts["registration"]:
        for gf in registration_generator.generate(raw, extra_roots=[staging.staging_root]):
            emit(gf)
            print(f"[+] 自动生成统一注册入口: {gf.path}")


def main() -> None:
    """
    执行完整生成管线：解析配置 → 单次扫描并校验 xlsx → 按 artifacts 暂存产物 → 原子换入磁盘
    """
    parsed_args = _parse_args()
    raw = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_STRUCT)
    overrides = {
        key: value for key, value in vars(parsed_args).items()
        if key not in ("config_path", "print_config") and value is not None
    }
    cfg = build_config_from_raw(raw, overrides)
    raw["output_module_dir"] = cfg.output_module_dir
    artifacts = load_artifacts_from_raw(raw)

    if parsed_args.print_config:
        for key, value in dataclasses.asdict(cfg).items():
            print(f"{key} = {value}")
        print(f"cpp_output_base_dir = {cfg.cpp_output_base_dir}")
        print(f"static_data_public_dir = {cfg.static_data_public_dir}")
        print(f"static_data_private_dir = {cfg.static_data_private_dir}")
        return

    print("==================================================")
    print("开始依据 Excel 下方工作表标签名全自动扫描生成 C++ RowStruct 与静态数据层...")
    print(f"配置文件: {parsed_args.config_path}")
    print(f"Excel 根目录: {cfg.data_tables_source_dir}")
    print(f"C++ 输出根目录 (Public): {cfg.cpp_output_base_dir}")
    print(f"静态数据头文件输出根目录 (Public): {cfg.static_data_public_dir}")
    print(f"静态数据实现输出根目录 (Private): {cfg.static_data_private_dir}")
    print("==================================================")

    parsed_tables, reserved_violations, processing_failure = _scan_xlsx_tables(cfg.data_tables_source_dir)
    if reserved_violations:
        print("\033[31m==================================================\033[0m")
        print("\033[31m[✗] 字段名硬校验失败：表头字段行禁止使用 'name'，已阻断生成（未写入任何产物）！\033[0m")
        for violation in reserved_violations:
            print(f"\033[31m  • {violation}\033[0m")
        print("\033[31m==================================================\033[0m")
        sys.exit(1)

    if not parsed_tables:
        if processing_failure:
            print("\033[31m[✗] 全部数据表均解析失败，已中止生成并保留磁盘原状。\033[0m", file=sys.stderr)
            sys.exit(1)
        _cleanup_when_no_tables(cfg, raw)
        return

    if processing_failure:
        print("\033[31m[✗] 本轮存在处理失败的表格，已回滚全部暂存产物，磁盘保持原状。\033[0m")
        sys.exit(1)

    staging = GenerationStaging(cfg.output_module_dir)

    def emit(gf: Any, valid_map_names: Optional[Set[str]] = None) -> Tuple[str, bool]:
        """
        统一产物出口：将单个生成文件写入事务暂存区
        @param gf 生成文件描述对象
        @param valid_map_names 可选有效 Map 名称集合
        @return (写入动作, 目标是否已存在)
        """
        return staging.stage_file(
            gf.path,
            gf.content,
            gf.policy,
            valid_map_names=valid_map_names,
            log_marker=cfg.log_macro,
        )

    category_headers: Dict[str, List[str]] = {}
    category_tables: Dict[str, List[Dict[str, Any]]] = {}
    ctx = GeneratorContext(cfg=cfg, category_headers=category_headers, category_tables=category_tables)

    try:
        for xlsx_filename, cat_folder, json_rel_dir, sheet_name, headers, row_items in parsed_tables:
            stem = to_pascal_case(sheet_name)
            desc_text = strip_dt_prefix(os.path.splitext(xlsx_filename)[0]) + "数据行"
            header_name, struct_name, enum_name = row_struct_generator.table_names(stem, cfg.row_key_mode)

            if artifacts["row_struct"]:
                for gf in row_struct_generator.generate(ctx, xlsx_filename, stem, desc_text, headers, row_items, cat_folder):
                    emit(gf)
                print(f"[+] 识别工作表 [{sheet_name}] -> 生成: {cat_folder}/{header_name} ({struct_name} / 键模式 {cfg.row_key_mode})")

            category_headers.setdefault(cat_folder, []).append(header_name)
            category_tables.setdefault(cat_folder, []).append({
                "stem": stem,
                "desc": desc_text,
                "struct_name": struct_name,
                "enum_name": enum_name,
                "header_name": header_name,
                "json_filename": os.path.splitext(xlsx_filename)[0] + ".json",
                "json_rel_dir": json_rel_dir,
                "fields": serialization_generator.extract_fields(headers),
            })

        _emit_all_artifacts(ctx, raw, artifacts, staging, emit)
    except Exception:
        staging.abort()
        raise

    if staging.staged_count == 0:
        staging.abort()
        print("[!] 本轮未产出任何生成文件。")
    else:
        try:
            staging.commit()
        except OSError as commit_error:
            print(f"\033[31m[✗] 生成产物换入失败，已回滚：{commit_error}\033[0m", file=sys.stderr)
            sys.exit(1)
        print("[✓] 生成产物已事务化换入（Public/Generated、Private/Generated 原子替换）。")

    print("==================================================")
    print("全自动零配置数据表代码生成管线执行完毕！")
    print("==================================================")


if __name__ == "__main__":
    main()
