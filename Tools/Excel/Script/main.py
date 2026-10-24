#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表自动生成 C++ USTRUCT 头文件工具 (main.py)

- 零配置全自动化：读取工作表标签名 (如 DT_Item_Definition) 自动推导 FItemDefinitionRow
- 目录分类自动镜像：随 xlsx 子目录 (DataTables/<Category>/) 输出到对应 C++ 目录
- 表头三行契约：中文名 -> UPROPERTY 注释/DisplayName；snake_case 英文名 -> 成员名；C++ 类型 -> 声明类型
- 自动生成 RowStruct 头文件、唯一键枚举、分类/全表汇总头、StaticData 容器与业务接口脚手架
- 管线入口：argparse + build_config + 扫描 xlsx + 按 artifacts 配置调用生成器 + file_writer 写文件
- artifacts 配置开关：config 中可选的 "artifacts" JSON 对象（缺失时全 true），
  按"产物名 -> 生成器模块"注册表遍历启用的产物；某产物关闭则跳过该生成器。
"""

import argparse
import dataclasses
import glob
import os
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from typing import Any, Dict, List, Optional, Set, Tuple

import tool_config
import table_schema

from excel_parser import get_sheet_data
from file_writer import GenerationStaging
from generators import GeneratorContext
from generators import aggregator_generator
from generators import composite_data_generator
from generators import game_registration_generator
from generators import interface_generator
from generators import query_generator
from generators import registration_generator
from generators import row_struct_generator
from generators import serialization_generator
from generators import service_generator
from generators import static_data_generator
from generators import static_data_registration_generator
from generators import static_data_snapshot_generator
from generators import subsystem_generator
from naming_utils import to_pascal_case


@dataclasses.dataclass
class ToolConfig:
    """
    生成器运行配置（全部来自配置文件，无内置默认值）

    输出落层：以 output_module_dir 为模块根，声明落 {module}/Public/Generated、
    实现落 {module}/Private/Generated（声明/实现分开，stale 孤儿文件随管线回收）
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
    log_header: str = ""
    log_macro: str = ""
    log_category_name: str = ""
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
        """RowStruct 行头输出根（{module}/Public/Generated/DataTables）"""
        return os.path.join(self.output_module_dir, "Public", "Generated", "DataTables")

    @property
    def static_data_public_dir(self) -> str:
        """静态数据声明输出根（{module}/Public/Generated/Data/StaticData）"""
        return os.path.join(self.output_module_dir, "Public", "Generated", "Data", "StaticData")

    @property
    def static_data_private_dir(self) -> str:
        """静态数据实现输出根（{module}/Private/Generated/Data/StaticData）"""
        return os.path.join(self.output_module_dir, "Private", "Generated", "Data", "StaticData")

    @property
    def registration_public_dir(self) -> str:
        """注册入口声明输出根（{module}/Public/Generated/Registration）"""
        return os.path.join(self.output_module_dir, "Public", "Generated", "Registration")

    @property
    def registration_private_dir(self) -> str:
        """注册入口实现输出根（{module}/Private/Generated/Registration）"""
        return os.path.join(self.output_module_dir, "Private", "Generated", "Registration")

    def generated_roots(self) -> List[str]:
        """返回全部生成域根目录（stale 回收扫描范围）"""
        return [
            self.cpp_output_base_dir,
            self.static_data_public_dir,
            self.static_data_private_dir,
            self.registration_public_dir,
            self.registration_private_dir,
        ]


def build_config(config_path: Optional[str], overrides: Optional[Dict[str, Any]] = None) -> ToolConfig:
    """
    构建运行配置：配置文件（必填，无默认）→ 命令行覆盖
    @param config_path --config 传入的 JSON 配置文件路径
    @param overrides 可选覆盖字典（键名与 ToolConfig 字段一一对应）
    @return 生效的 ToolConfig
    """
    raw = tool_config.load_config(config_path, tool_config.KEYS_STRUCT)
    field_names = {field.name for field in dataclasses.fields(ToolConfig)}
    merged = {key: value for key, value in raw.items() if key in field_names}
    if overrides:
        for key, value in overrides.items():
            if value is not None and key in merged:
                merged[key] = value
    return ToolConfig(**merged)


def load_artifacts(config_path: Optional[str]) -> Dict[str, bool]:
    """
    读取 artifacts 配置开关（可选的 JSON 对象，缺失时全 true）
    @param config_path --config 传入的 JSON 配置文件路径
    @return 产物名 -> 是否启用的字典
    """
    raw = tool_config.load_config(config_path, tool_config.KEYS_STRUCT)
    artifacts_cfg = raw.get("artifacts", {})
    artifact_names = (
        "row_struct", "composite_data", "static_data", "snapshot_store", "static_data_registration",
        "game_registration",
        "interface", "query", "service", "subsystem", "serialization", "aggregators",
        "registration",
    )
    return {name: artifacts_cfg.get(name, True) for name in artifact_names}


def is_ignorable_xlsx(file_path: str) -> bool:
    """判定是否为应跳过的 Excel 文件（Excel 临时文件「~$」前缀或 0 字节空文件）"""
    return os.path.basename(file_path).startswith("~$") or os.path.getsize(file_path) == 0


def main() -> None:
    """
    执行完整生成管线：解析配置 → 扫描 xlsx → 按 artifacts 开关生成各产物 → 回收 stale 文件

    实现思路：先对全部 xlsx 做表头字段名硬校验（命中禁用字段名即整体阻断、不写任何产物），
    再按 artifacts 开关逐产物生成；注册类与热更监听须先于统一注册入口聚合，保证首轮即并入；
    最后镜像源树分类目录并回收本轮未产出的 stale 孤儿文件。
    """
    parser = argparse.ArgumentParser(description="Excel 数据表通用 C++ 生成器")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parser.add_argument("--source-dir", dest="data_tables_source_dir", default=None, help="Excel 源目录（覆盖配置文件）")
    parser.add_argument("--output-module-dir", dest="output_module_dir", default=None, help="生成产物输出模块根（覆盖配置文件）")
    parser.add_argument("--api-macro", dest="api_macro", default=None, help="生成类型的 API 导出宏（覆盖配置文件）")
    parser.add_argument("--subsystem-header-include", dest="subsystem_header_include", default=None, help="Subsystem 头 include 相对路径（覆盖配置文件）")
    parser.add_argument("--json-root-dir-name", dest="json_root_dir_name", default=None, help="JSON 数据根目录名（覆盖配置文件）")
    parser.add_argument("--print-config", action="store_true", help="打印生效配置后退出")
    parsed_args = parser.parse_args()

    overrides = {key: value for key, value in vars(parsed_args).items()
                 if key not in ("config_path", "print_config") and value is not None}
    cfg = build_config(parsed_args.config_path, overrides)
    artifacts = load_artifacts(parsed_args.config_path)
    processing_failure = False

    def emit(gf: Any, valid_map_names: Optional[Set[str]] = None) -> Tuple[str, bool]:
        """统一产物出口：写入事务暂存区（全部成功后才原子换入磁盘）"""
        return staging.stage_file(gf.path, gf.content, gf.policy, valid_map_names=valid_map_names)

    if parsed_args.print_config:
        for key, value in dataclasses.asdict(cfg).items():
            print(f"{key} = {value}")
        print(f"cpp_output_base_dir = {cfg.cpp_output_base_dir}")
        print(f"static_data_public_dir = {cfg.static_data_public_dir}")
        print(f"static_data_private_dir = {cfg.static_data_private_dir}")
        return

    staging = GenerationStaging(cfg.output_module_dir)

    print("==================================================")
    print("开始依据 Excel 下方工作表标签名全自动扫描生成 C++ RowStruct 与唯一键枚举...")
    print(f"配置文件: {parsed_args.config_path}")
    print(f"Excel 根目录: {cfg.data_tables_source_dir}")
    print(f"C++ 输出根目录 (Public): {cfg.cpp_output_base_dir}")
    print(f"静态数据头文件输出根目录 (Public): {cfg.static_data_public_dir}")
    print(f"静态数据实现输出根目录 (Private): {cfg.static_data_private_dir}")
    print("==================================================")

    category_headers = {}
    category_tables = {}
    ctx = GeneratorContext(cfg=cfg, category_headers=category_headers, category_tables=category_tables)

    pattern = os.path.join(cfg.data_tables_source_dir, "**", "*.xlsx")
    xlsx_files = sorted(glob.glob(pattern, recursive=True))

    reserved_field_violations = []
    for file_path in xlsx_files:
        xlsx_filename = os.path.basename(file_path)
        if is_ignorable_xlsx(file_path):
            continue
        try:
            _, headers, _ = get_sheet_data(file_path)
        except (zipfile.BadZipFile, ET.ParseError) as e:
            print(f"[!] 表头预检解析失败，跳过 {xlsx_filename}: {e}")
            processing_failure = True
            continue
        if len(headers) < 3:
            continue
        for col_idx, field_name in table_schema.find_reserved_field_names(headers[1]):
            reserved_field_violations.append(f"{xlsx_filename} 第 {col_idx} 列字段名为 {field_name!r}（该字段名被禁止）")

    if reserved_field_violations:
        print("\033[31m==================================================\033[0m")
        print("\033[31m[✗] 字段名硬校验失败：表头字段行禁止使用 'name'，已阻断生成（未写入任何产物）！\033[0m")
        for violation in reserved_field_violations:
            print(f"\033[31m  • {violation}\033[0m")
        print("\033[31m==================================================\033[0m")
        sys.exit(1)

    for file_path in xlsx_files:
        xlsx_filename = os.path.basename(file_path)
        if is_ignorable_xlsx(file_path):
            continue

        rel_dir = os.path.relpath(os.path.dirname(file_path), cfg.data_tables_source_dir)
        cat_folder = rel_dir if rel_dir != "." else "Shared"

        try:
            sheet_name, headers, row_items = get_sheet_data(file_path)
            if not sheet_name:
                print(f"[!] 无法读取下方工作表名称，跳过: {xlsx_filename}")
                continue

            if len(headers) < 3:
                print(f"[!] 表头契约不足 3 行，跳过: {xlsx_filename}")
                continue

            stem = to_pascal_case(sheet_name)
            desc_text = os.path.splitext(xlsx_filename)[0].replace("DT_", "") + "数据行"

            if artifacts["row_struct"]:
                for gf in row_struct_generator.generate(ctx, xlsx_filename, stem, desc_text, headers, row_items, cat_folder):
                    emit(gf)
                header_name, struct_name, enum_name = row_struct_generator.table_names(stem, cfg.row_key_mode)
                print(f"[+] 识别工作表 [{sheet_name}] -> 生成: {cat_folder}/{header_name} ({struct_name} / 键模式 {cfg.row_key_mode})")
            else:
                header_name, struct_name, enum_name = row_struct_generator.table_names(stem, cfg.row_key_mode)

            category_headers.setdefault(cat_folder, []).append(header_name)
            category_tables.setdefault(cat_folder, []).append({
                "stem": stem,
                "desc": desc_text,
                "struct_name": struct_name,
                "enum_name": enum_name,
                "header_name": header_name,
                "json_filename": os.path.splitext(xlsx_filename)[0] + ".json",
                "fields": serialization_generator.extract_fields(headers)
            })

        except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
            print(f"[!] 处理异常 {xlsx_filename}: {e}")
            processing_failure = True

    if not category_tables:
        print("[!] 数据表目录下未检测到任何有效 Excel 表格。")
        public_gen = os.path.join(cfg.output_module_dir, "Public", "Generated")
        private_gen = os.path.join(cfg.output_module_dir, "Private", "Generated")
        cleaned_any = False
        for gen_dir in (public_gen, private_gen):
            if os.path.isdir(gen_dir):
                shutil.rmtree(gen_dir)
                cleaned_any = True
                print(f"[×] 已清理生成目录: {os.path.relpath(gen_dir, cfg.output_module_dir)}")
        raw = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_STRUCT)
        reg_sec = raw.get("registration", {}) if isinstance(raw.get("registration"), dict) else {}
        out_header = reg_sec.get("output_header")
        if out_header and os.path.isfile(out_header):
            os.remove(out_header)
            print(f"[×] 已清理注册入口头文件: {out_header}")
        if cleaned_any:
            print("[✓] 由于当前无有效数据表，已完全删除所有 Generated 代码目录与产物。")
        else:
            print("[✓] 当前无有效数据表，且无遗留 Generated 产物。")
        return

    active_categories = set(category_tables.keys())
    for root_base in (cfg.cpp_output_base_dir, cfg.static_data_public_dir, cfg.static_data_private_dir):
        if not os.path.isdir(root_base):
            continue
        for item in os.listdir(root_base):
            item_path = os.path.join(root_base, item)
            if os.path.isdir(item_path) and item not in active_categories:
                shutil.rmtree(item_path)
                print(f"[×] 表分类 [{item}] 下已无有效数据表，已彻底清理对应生成目录: {os.path.relpath(item_path, cfg.output_module_dir)}")

    if artifacts["aggregators"]:
        root_agg_path = os.path.join(cfg.cpp_output_base_dir, "DataTableRowTypes.h")
        static_types_path = os.path.join(cfg.static_data_public_dir, "StaticDataTypes.h")
        for gf in aggregator_generator.generate(ctx):
            emit(gf)
            if gf.path == static_types_path:
                print("[+] 自动生成静态数据主汇总头文件: Public/Data/StaticData/StaticDataTypes.h")
            elif gf.path == root_agg_path:
                print("[+] 自动生成全表主汇总头文件: DataTableRowTypes.h")
            else:
                print(f"[+] 自动生成分类汇总头文件: {os.path.relpath(gf.path, cfg.cpp_output_base_dir)}")

    if artifacts["composite_data"]:
        for gf in composite_data_generator.generate(ctx):
            emit(gf)
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir)
            print(f"[+] 自动生成单体聚合数据容器: Public/Data/StaticData/{rel}")

    if artifacts["interface"]:
        for gf in interface_generator.generate(ctx):
            action, _ = emit(gf, valid_map_names=gf.valid_map_names)
            is_h = gf.path.endswith(".h")
            prefix = "Public/Data/StaticData" if is_h else "Private/Data/StaticData"
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
            prefix = "Public/Data/StaticData" if is_h else "Private/Data/StaticData"
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
                print(f"[-] 保留现有域服务（不覆盖）: Public/Data/StaticData/{rel}")
            else:
                print(f"[+] 首次生成域服务骨架: Public/Data/StaticData/{rel}")

    if artifacts["static_data"]:
        for gf in static_data_generator.generate(ctx):
            emit(gf)
            rel = os.path.relpath(gf.path, cfg.static_data_public_dir)
            print(f"[+] 自动生成静态数据容器: Public/Data/StaticData/{rel}")

    if artifacts["snapshot_store"]:
        for gf in static_data_snapshot_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成静态数据快照/缓存层: {cfg.static_data_public_dir}（快照 / 缓存 / RCU 存储）")

    if artifacts["static_data_registration"]:
        for gf in static_data_registration_generator.generate(ctx):
            emit(gf)
        print("[+] 自动生成热更监听与注册入口: 热更监听器 / FStaticDataRegistration / .bin 导出命令命令行工具")

    if artifacts["game_registration"]:
        for gf in game_registration_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成游戏层注册类: {game_registration_generator.class_name_of(cfg)}")

    if artifacts["serialization"]:
        for gf in serialization_generator.generate(ctx):
            emit(gf)
            print(f"[+] 自动生成生成式行序列化器: {os.path.relpath(gf.path, cfg.output_module_dir)}")

    cat_folders = sorted(category_tables.keys())
    if artifacts["subsystem"] and cat_folders:
        subsystem_h_path = os.path.join(cfg.static_data_public_dir, "UStaticDataSubsystem.h")
        for gf in subsystem_generator.generate(ctx):
            emit(gf)
        print(f"[+] 自动生成全局静态数据子系统: {subsystem_h_path} 及其 .cpp")

    if artifacts["registration"]:
        raw = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_STRUCT)
        for gf in registration_generator.generate(raw):
            emit(gf)
            print(f"[+] 自动生成统一注册入口: {gf.path}")

    if processing_failure:
        staging.abort()
        print("\033[31m[✗] 本轮存在处理失败的表格，已回滚全部暂存产物，磁盘保持原状。\033[0m")
        sys.exit(1)

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
