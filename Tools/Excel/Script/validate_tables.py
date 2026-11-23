#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据驱动全自动表结构与外键完整性校验引擎 (validate_tables.py)

- 规则判定委托 table_rules.py（单一真源，与 json_to_excel.py 写前校验共用）
- 无硬编码：主键合规、主键唯一性、标量类型校验、受损 xlsx 阻断报错
- 三级外键发现：表头显式声明 / 配置外置外键规则文件 / 智能前缀与语义推导
- 发现 0 条规则或目标表缺失时仅提示不阻断；有错误时以非零退出码阻断管线
"""

import argparse
import json
import os
import sys
import xml.etree.ElementTree as ET
import zipfile
from typing import Any, List, Optional, Tuple

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import cpp_type_utils  # noqa: E402
import excel_parser  # noqa: E402
from naming_utils import strip_dt_prefix, to_pascal_case, to_property_name  # noqa: E402
import table_rules  # noqa: E402
import table_schema  # noqa: E402
import tool_config  # noqa: E402


def read_sheet_data(file_path: str) -> Tuple[str, List[str], List[str], List[str], List[List[Any]]]:
    """
    读取 Excel 工作表名、表头三行契约与全部数据行
    @param file_path xlsx 绝对路径
    @return (sheet_name, chinese_names, field_names, cpp_types, data_rows)
    """
    table_name = os.path.splitext(os.path.basename(file_path))[0]
    sheet_name, grid = excel_parser.read_sheet_grid(file_path, selector=table_name)
    if len(grid) < 3:
        return sheet_name, [], [], [], []
    return (
        sheet_name,
        [str(c) if c is not None else "" for c in grid[0]],
        [str(c) if c is not None else "" for c in grid[1]],
        [str(c) if c is not None else "" for c in grid[2]],
        grid[3:],
    )


def load_external_rules(configured_path: Optional[str] = None) -> List[Any]:
    """
    可选加载外置外键规则文件（路径来自配置 foreign_keys_file；未配置即不加载）
    @param configured_path 外置规则文件路径
    @return 外键规则列表
    """
    if not configured_path:
        return []
    if not os.path.exists(configured_path):
        print(f"\033[33m[警告] 配置的外置外键规则文件不存在，跳过: {configured_path}\033[0m")
        return []
    try:
        with open(configured_path, "r", encoding="utf-8") as f:
            rules = json.load(f)
            print(f"[配置] 成功加载外置外键规则文件: {os.path.basename(configured_path)}")
            return rules if isinstance(rules, list) else []
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as e:
        print(f"\033[33m[警告] 读取外置外键规则文件 {configured_path} 失败: {e}\033[0m")
        return []


def print_issue(issue: table_rules.RuleIssue) -> None:
    """
    按问题类型逐个输出校验问题
    @param issue 规则问题条目
    """
    if issue.code == "MISSING_KEY":
        print(f"\033[31m[主键缺失错误] {issue.file} 第 {issue.row_idx} 行存在数据但主键为空！\033[0m")
    elif issue.code == "INVALID_KEY":
        print(f"\033[31m[主键非法错误] {issue.file} 第 {issue.row_idx} 行主键 [{issue.key}] 包含空格或非法字符！\033[0m")
    elif issue.code == "DUPLICATE_KEY":
        print(f"\033[31m[主键重复错误] {issue.file} 第 {issue.row_idx} 行主键重复: [{issue.key}]\033[0m")
    elif issue.code == "INVALID_SCALAR":
        print(f"\033[31m[数据类型错误] {issue.file} 第 {issue.row_idx} 行 [{issue.key}] 字段 '{issue.field}' "
              f"({issue.type_name}): 值 '{issue.value}' {issue.detail}！\033[0m")
    elif issue.code == "TARGET_TABLE_MISSING":
        target_sheet, _, _ = issue.detail.partition("|")
        print(f"\033[33m[提示] 引用目标表 [{target_sheet}] 当前未导入，跳过当前字段 '{issue.field}' 的外键校验。\033[0m")
    elif issue.code == "DANGLING_FK":
        target_sheet, _, infer_source = issue.detail.partition("|")
        print(f"\033[31m[外键悬空错误] [{infer_source}]\033[0m")
        print(f"  ├── 源文件: {issue.file} 第 {issue.row_idx} 行 (主键: [{issue.key}])")
        print(f"  └── 字段 '{issue.field}' 的引用值 [{issue.value}] 在目标表 [{target_sheet}] 中不存在！")


def validate_directory(
    data_tables_dir: str,
    foreign_keys_file: Optional[str] = None,
    nested_struct_types: Optional[dict] = None,
) -> int:
    """
    执行目录全量数据表完整性与跨表外键约束校验
    @param data_tables_dir Excel 数据表根目录
    @param foreign_keys_file 可选外置外键规则文件路径
    @param nested_struct_types 可选嵌套 USTRUCT 类型映射
    @return 严重错误总数（0 表示全部通过）
    """
    print("==================================================")
    print("正在执行数据表全自动完整性与跨表外键约束校验...")
    print(f"扫描数据表目录: {data_tables_dir}")
    print("==================================================")

    tables_data: dict[str, dict[str, Any]] = {}
    seen_stems: dict[str, str] = {}
    error_count = 0

    for file_path in excel_parser.iter_xlsx_files(data_tables_dir):
        xlsx_name = os.path.basename(file_path)
        try:
            sheet_name, chinese_names, field_names, cpp_types, data_rows = read_sheet_data(file_path)
        except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
            print(f"\033[31m[文件损坏错误] 无法读取数据表 {xlsx_name}: {e}\033[0m")
            error_count += 1
            continue

        if not sheet_name or not field_names:
            continue

        if sheet_name in tables_data:
            prev_file = tables_data[sheet_name]["file"]
            print(f"\033[31m[工作表名冲突错误] {xlsx_name} 与 {prev_file} 包含同名工作表 '{sheet_name}'！\033[0m")
            error_count += 1

        stem = to_pascal_case(sheet_name)
        if stem in seen_stems and seen_stems[stem] != xlsx_name:
            print(f"\033[31m[结构体名冲突错误] {xlsx_name} 与 {seen_stems[stem]} 均映射到结构体 F{stem}Row！\033[0m")
            error_count += 1
        elif stem:
            seen_stems[stem] = xlsx_name

        key_col_name = str(field_names[0] or "").strip()
        if key_col_name not in ("RowName", "Name"):
            print(f"\033[31m[主键列名错误] {xlsx_name} 首列字段名必须为 'RowName' 或 'Name'，当前为 '{key_col_name}'！\033[0m")
            error_count += 1

        reserved_hits = table_schema.find_reserved_field_names(field_names)
        for col_idx, field_name in reserved_hits:
            print(f"\033[31m[字段名禁用错误] {xlsx_name} 第 {col_idx} 列字段名为 '{field_name}'，该字段名被禁止！\033[0m")
        error_count += len(reserved_hits)

        records, row_keys, key_issues = table_rules.build_row_records(data_rows, field_names, xlsx_name, sheet_name)
        for issue in key_issues:
            print_issue(issue)
        error_count += sum(1 for i in key_issues if i.level == table_rules.LEVEL_ERROR)

        prefixes = {k.split("_")[0] + "_" for k in row_keys if "_" in k}

        tables_data[sheet_name] = {
            "file": xlsx_name,
            "chinese_names": chinese_names,
            "field_names": field_names,
            "cpp_types": cpp_types,
            "keys": row_keys,
            "rows": {key: (idx, row_dict) for idx, key, row_dict in records},
            "ordered_rows": records,
            "prefixes": prefixes,
        }

    if not tables_data:
        if error_count == 0:
            print("\033[33m[提示] 数据表目录下未发现有效数据表，跳过校验。\033[0m")
        return error_count

    allowed_enum_types = (
        {f"E{to_pascal_case(s_name)}Key" for s_name in tables_data}
        | {f"E{strip_dt_prefix(s_name)}Key" for s_name in tables_data}
    )
    for sheet_name, t_info in tables_data.items():
        xlsx_name = t_info["file"]
        field_names = t_info["field_names"]
        cpp_types = t_info["cpp_types"]
        seen_props: dict[str, str] = {}
        for col_idx in range(1, len(field_names)):
            fname = str(field_names[col_idx] or "").strip()
            if not fname:
                continue
            raw_t = str(cpp_types[col_idx] or "FString") if col_idx < len(cpp_types) else "FString"
            clean_t = cpp_type_utils.clean_cpp_type(raw_t)
            try:
                cpp_type_utils.validate_field_type(
                    clean_t,
                    nested_struct_types,
                    context=f"{xlsx_name} 字段 [{fname}]",
                    allowed_enum_types=allowed_enum_types,
                )
            except ValueError as type_err:
                print(f"\033[31m[字段类型错误] {type_err}\033[0m")
                error_count += 1
            prop_name = to_property_name(fname, clean_t)
            if prop_name in seen_props and seen_props[prop_name] != fname:
                print(
                    f"\033[31m[属性名冲突错误] {xlsx_name} 字段 '{fname}' 与 '{seen_props[prop_name]}' "
                    f"均映射到同一 C++ 属性名 '{prop_name}'！\033[0m"
                )
                error_count += 1
            elif prop_name:
                seen_props[prop_name] = fname

    prefix_to_table, _ = table_rules.build_table_index(tables_data)
    print(f"[索引构建] 成功索引 {len(tables_data)} 张数据表，识别 {len(prefix_to_table)} 个主键特征前缀。")

    for sheet_name, t_info in tables_data.items():
        scalar_issues = table_rules.check_scalar_values(
            t_info["ordered_rows"], t_info["field_names"], t_info["cpp_types"],
            sheet_name, t_info["file"],
        )
        for issue in scalar_issues:
            print_issue(issue)
        error_count += sum(1 for i in scalar_issues if i.level == table_rules.LEVEL_ERROR)

    external_rules = load_external_rules(foreign_keys_file)
    external_rule_map = {
        (r.get("source_sheet"), r.get("source_field")): r.get("target_sheet")
        for r in external_rules
        if isinstance(r, dict) and r.get("source_sheet") and r.get("source_field") and r.get("target_sheet")
    }
    discovered_rules = table_rules.discover_references(tables_data, external_rule_map)

    print("--------------------------------------------------")
    print(f"全自动外键发现完成：共检测到 {len(discovered_rules)} 处跨表引用关系：")
    for r in discovered_rules:
        print(f"  • [{r['infer_source']:24}] {r['source_sheet']}.{r['source_field']}  ──►  {r['target_sheet']}")
    print("--------------------------------------------------")

    for issue in table_rules.check_references(tables_data, discovered_rules):
        print_issue(issue)
        if issue.level == table_rules.LEVEL_ERROR:
            error_count += 1

    return error_count


def main() -> None:
    """
    命令行入口：加载配置并执行数据表完整性校验，存在错误时以退出码 1 阻断管线
    """
    parser = argparse.ArgumentParser(description="数据表完整性与跨表外键校验工具（配置驱动，无内置默认路径）")
    parser.add_argument("--config", dest="config_path", default=None,
                        help="JSON 配置文件路径（缺省走共享配置发现）")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_VALIDATE)
    error_count = validate_directory(
        cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR],
        cfg.get("foreign_keys_file"),
        cfg.get("nested_struct_types"),
    )

    if error_count > 0:
        print("\033[31m==================================================\033[0m")
        print(f"\033[31m[✗] 数据表校验失败！共发现 {error_count} 处严重数据或约束错误，已阻断管线！\033[0m")
        print("\033[31m==================================================\033[0m")
        sys.exit(1)

    print("\033[32m==================================================\033[0m")
    print("\033[32m[✓] 数据表完整性校验通过！\033[0m")
    print("\033[32m==================================================\033[0m")


if __name__ == "__main__":
    main()
