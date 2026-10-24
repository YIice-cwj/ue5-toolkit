#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
数据驱动全自动表结构与外键完整性校验引擎 (validate_tables.py)

- 规则判定委托 table_rules.py（单一真源，与 json_to_excel.py 写前校验共用）
- 无硬编码：主键合规、主键唯一性、标量类型 (float/int32/bool) 校验
- 三级外键发现：表头显式声明 / 配置外置外键规则文件 / 智能前缀与语义推导
- 发现 0 条规则或目标表缺失时仅提示不阻断；有错误时以非零退出码阻断管线
"""

import argparse
import os
import sys
import glob
import json
import zipfile
import xml.etree.ElementTree as ET
from typing import Any, List, Optional, Tuple

import excel_parser
import table_rules
import table_schema
import tool_config


def read_sheet_data(file_path: str) -> Tuple[str, List[str], List[str], List[str], List[List[str]]]:
    """
    读取 Excel 工作表名、表头三行契约与全部数据行
    @param file_path xlsx 绝对路径
    @return (sheet_name, chinese_names, field_names, cpp_types, data_rows)

    实现思路：xlsx 解析唯一实现收敛在 excel_parser.read_sheet_grid（不硬编码 sheet1.xml，
    自带共享字符串池与稀疏单元格定位）；此处仅把网格切成校验所需的三行表头与数据行，
    选表键沿用文件名词干以与既有校验语义保持一致。
    """
    table_name = os.path.splitext(os.path.basename(file_path))[0]
    sheet_name, grid = excel_parser.read_sheet_grid(file_path, selector=table_name)
    if len(grid) < 3:
        return sheet_name, [], [], [], []
    return sheet_name, grid[0], grid[1], grid[2], grid[3:]


def load_external_rules(configured_path: Optional[str] = None) -> List[Any]:
    """可选加载外置外键规则文件（路径必须来自配置 foreign_keys_file；未配置即不加载）"""
    if configured_path:
        if not os.path.exists(configured_path):
            print(f"\033[33m[警告] 配置的外置外键规则文件不存在，跳过: {configured_path}\033[0m")
            return []
        try:
            with open(configured_path, "r", encoding="utf-8") as f:
                rules = json.load(f)
                print(f"[配置] 成功加载外置外键规则文件: {os.path.basename(configured_path)}")
                return rules
        except (OSError, json.JSONDecodeError, UnicodeDecodeError) as e:
            print(f"\033[33m[警告] 读取外置外键规则文件 {configured_path} 失败: {e}\033[0m")
    return []


def print_issue(issue: table_rules.RuleIssue) -> None:
    """按问题类型逐个输出校验问题"""
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
        target_sheet, _ = issue.detail.split("|", 1)
        print(f"\033[33m[提示] 引用目标表 [{target_sheet}] 当前未导入，跳过当前字段 '{issue.field}' 的外键校验。\033[0m")
    elif issue.code == "DANGLING_FK":
        target_sheet, infer_source = issue.detail.split("|", 1)
        print(f"\033[31m[外键悬空错误] [{infer_source}]\033[0m")
        print(f"  ├── 源文件: {issue.file} 第 {issue.row_idx} 行 (主键: [{issue.key}])")
        print(f"  └── 字段 '{issue.field}' 的引用值 [{issue.value}] 在目标表 [{target_sheet}] 中不存在！")


def main() -> None:
    """
    执行数据表完整性校验

    实现思路：先扫描全部 xlsx 建立索引、主键集与命名前缀库并完成主键/字段名硬校验，
    再逐表做标量类型防呆校验，随后三级发现跨表外键并校验引用完整性，最后汇总错误数并在有错时阻断。
    """

    parser = argparse.ArgumentParser(description="数据表完整性与跨表外键校验工具（配置驱动，无内置默认路径）")
    parser.add_argument("--config", dest="config_path", default=None,
                        help="JSON 配置文件路径（缺省尝试当前目录 generator_config.json）")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_VALIDATE)
    data_tables_dir = cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR]

    print("==================================================")
    print("正在执行数据表全自动完整性与跨表外键约束校验...")
    print(f"扫描数据表目录: {data_tables_dir}")
    print("==================================================")

    pattern = os.path.join(data_tables_dir, "**", "*.xlsx")
    xlsx_files = sorted(glob.glob(pattern, recursive=True))

    tables_data = {}
    error_count = 0

    for file_path in xlsx_files:
        xlsx_name = os.path.basename(file_path)
        if xlsx_name.startswith("~$") or os.path.getsize(file_path) == 0:
            continue

        try:
            sheet_name, chinese_names, field_names, cpp_types, data_rows = read_sheet_data(file_path)
        except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
            print(f"\033[33m[跳过] 无法读取文件 {xlsx_name}: {e}\033[0m")
            continue

        if not sheet_name or not field_names:
            continue

        reserved_hits = table_schema.find_reserved_field_names(field_names)
        for col_idx, field_name in reserved_hits:
            print(f"\033[31m[字段名禁用错误] {xlsx_name} 第 {col_idx} 列字段名为 '{field_name}'，该字段名被禁止！\033[0m")
        error_count += len(reserved_hits)

        records, row_keys, key_issues = table_rules.build_row_records(data_rows, field_names, xlsx_name, sheet_name)
        for issue in key_issues:
            print_issue(issue)
        error_count += len([i for i in key_issues if i.level == table_rules.LEVEL_ERROR])

        ordered_rows = [(idx, key, row_dict, data_rows[idx - 4]) for idx, key, row_dict in records]

        prefixes = {k.split("_")[0] + "_" for k in row_keys if "_" in k}

        tables_data[sheet_name] = {
            "file": xlsx_name,
            "chinese_names": chinese_names,
            "field_names": field_names,
            "cpp_types": cpp_types,
            "keys": row_keys,
            "rows": {key: (idx, row_dict) for idx, key, row_dict in records},
            "ordered_rows": ordered_rows,
            "prefixes": prefixes,
        }

    if not tables_data:
        print("\033[33m[提示] 数据表目录下未发现有效数据表，跳过校验。\033[0m")
        return

    prefix_to_table, _ = table_rules.build_table_index(tables_data)
    print(f"[索引构建] 成功索引 {len(tables_data)} 张数据表，识别 {len(prefix_to_table)} 个主键特征前缀。")

    for sheet_name, t_info in tables_data.items():
        scalar_issues = table_rules.check_scalar_values(
            t_info["ordered_rows"], t_info["field_names"], t_info["cpp_types"],
            sheet_name, t_info["file"])
        for issue in scalar_issues:
            print_issue(issue)
        error_count += len([i for i in scalar_issues if i.level == table_rules.LEVEL_ERROR])

    external_rules = load_external_rules(cfg.get("foreign_keys_file"))
    external_rule_map = {(r.get("source_sheet"), r.get("source_field")): r.get("target_sheet") for r in external_rules}

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

    if error_count > 0:
        print("\033[31m==================================================\033[0m")
        print(f"\033[31m[✗] 数据表校验失败！共发现 {error_count} 处严重数据或约束错误，已阻断管线！\033[0m")
        print("\033[31m==================================================\033[0m")
        sys.exit(1)

    print("\033[32m==================================================\033[0m")
    print(f"\033[32m[✓] 数据表完整性校验通过！已验证 {len(tables_data)} 张数据表的主键、标量类型与跨表外键引用。\033[0m")
    print("\033[32m==================================================\033[0m")


if __name__ == "__main__":
    main()
