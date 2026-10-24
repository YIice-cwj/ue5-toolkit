#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表转 JSON 导出工具 (excel_to_json.py)

- 扫描 DataTables 下所有 .xlsx，遵循表头三行契约导出标准 UE5 DataTable JSON
- 首列 RowName 映射为键 "Name"（同时保留 RowName 键），兼容 inlineStr/str 内联字符串
- 零外部依赖：仅用内置 zipfile + xml.etree.ElementTree
"""

import json
import os
import zipfile
import xml.etree.ElementTree as ET
from typing import Any, Dict, List, Optional

import table_schema
import tool_config
from excel_parser import read_sheet_grid
from file_writer import remove_dir_if_empty
from naming_utils import to_property_name


def convert_value_by_type(raw_val: Any, cpp_type: Optional[str], nested_struct_types: Optional[dict] = None) -> Any:
    """
    根据表头第 3 行声明的 C++ 类型进行强类型转换（委托至 table_schema 唯一实现）
    """
    return table_schema.convert_for_json(raw_val, cpp_type, nested_struct_types=nested_struct_types)


def convert_sheet_to_ue5_json(rows: List[List[Any]], nested_struct_types: Optional[dict] = None) -> List[Dict[str, Any]]:
    """
    将三行契约表格数据转为 UE5 DataTable 标准 JSON 格式（List of Objects）

    实现思路：第 2 行为字段名（snake_case）、第 3 行为 C++ 类型、第 4 行起为数据；
    跳过整行或首列为空的行与空字段名；首列 RowName 按 UE5 规则重命名为 "Name"（并保留 RowName 键）；
    每列值按声明类型强转后再落入记录。
    """
    if len(rows) < 3:
        return []

    field_names = rows[1]
    cpp_types = rows[2]

    ue5_json_records: List[Dict[str, Any]] = []

    for row_idx in range(3, len(rows)):
        row = rows[row_idx]

        if not row or not any(str(v).strip() for v in row):
            continue
        if not row[0] or not str(row[0]).strip():
            continue

        record: Dict[str, Any] = {}
        for col_idx, val in enumerate(row):
            if col_idx >= len(field_names):
                continue

            field_name = field_names[col_idx]
            cpp_type = cpp_types[col_idx] if col_idx < len(cpp_types) else "FString"

            if not field_name:
                continue

            prop_key = to_property_name(field_name, cpp_type)
            json_key = "Name" if field_name == "RowName" else prop_key
            converted_val = convert_value_by_type(val, cpp_type, nested_struct_types)

            if field_name == "RowName":
                record["RowName"] = converted_val
            record[json_key] = converted_val

        ue5_json_records.append(record)

    return ue5_json_records


def export_all(input_dir: str, output_dir: str, nested_struct_types: Optional[dict] = None) -> None:
    """遍历 input_dir 下的所有 .xlsx 文件并输出为 .json 文件。

    实现思路：基于 excel_parser 统一读取网格；过滤空表并转为 UE5 DataTable 标准 JSON；
    导出后全量巡检 output_dir，清理已删除表遗留的孤儿 JSON 文件与空分类目录。
    """
    print("==================================================")
    print("开始执行 Excel 转 JSON 批处理导出...")
    print(f"输入源目录: {input_dir}")
    print(f"输出目标目录: {output_dir}")
    print("==================================================")

    success_count = 0
    skip_count = 0
    valid_json_paths = set()

    for root, _, files in os.walk(input_dir):
        for file in files:
            if not file.endswith(".xlsx") or file.startswith("~$") or file.startswith("."):
                continue

            xlsx_path = os.path.join(root, file)

            rel_dir = os.path.relpath(root, input_dir)
            target_sub_dir = os.path.join(output_dir, rel_dir) if rel_dir != "." else output_dir
            os.makedirs(target_sub_dir, exist_ok=True)

            json_filename = os.path.splitext(file)[0] + ".json"
            json_path = os.path.join(target_sub_dir, json_filename)

            try:
                if os.path.getsize(xlsx_path) == 0:
                    print(f"[-] 跳过空文件: {file}")
                    skip_count += 1
                    continue

                _, rows = read_sheet_grid(xlsx_path)
                if len(rows) < 4:
                    print(f"[-] 跳过无有效数据行文件: {file} (行数: {len(rows)})")
                    skip_count += 1
                    continue

                json_records = convert_sheet_to_ue5_json(rows, nested_struct_types)

                with open(json_path, "w", encoding="utf-8") as f:
                    json.dump(json_records, f, ensure_ascii=False, indent=2)

                valid_json_paths.add(os.path.normpath(json_path))
                print(f"[+] 导出成功: {file} -> {os.path.relpath(json_path, output_dir)} (共 {len(json_records)} 条记录)")
                success_count += 1

            except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
                print(f"[!] 导出失败 {file}: {e}")

    if os.path.isdir(output_dir):
        removed_json_count = 0
        for root, _dirs, files in os.walk(output_dir, topdown=False):
            for file in files:
                if file.endswith(".json"):
                    full_p = os.path.normpath(os.path.join(root, file))
                    if full_p not in valid_json_paths:
                        os.remove(full_p)
                        removed_json_count += 1
                        print(f"[×] 表已删除或为空，已移除孤儿 JSON 文件: {os.path.relpath(full_p, output_dir)}")
            remove_dir_if_empty(root)
        if removed_json_count > 0:
            print(f"[×] 孤儿清理：已清除 {removed_json_count} 个已删除表的遗留 JSON 文件。")

    print("==================================================")
    print(f"导出完成！成功: {success_count} 个表，跳过: {skip_count} 个空表。")
    print(f"JSON 产物位置: {output_dir}")
    print("==================================================")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Excel 数据表转 JSON 导出工具（配置驱动，无内置默认路径）")
    parser.add_argument("--config", dest="config_path", default=None,
                        help="JSON 配置文件路径（缺省尝试当前目录 generator_config.json）")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_JSON)
    export_all(cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR], cfg["json_output_dir"], cfg.get("nested_struct_types"))
