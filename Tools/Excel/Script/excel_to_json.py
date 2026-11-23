#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表转 JSON 导出工具 (excel_to_json.py)

- 扫描 DataTables 下所有 .xlsx，遵循表头三行契约导出标准 UE5 DataTable JSON
- 合法空表（具备完整 3 行表头、0 条数据行）导出为空数组 []，避免 C++ 启动期因缺失 JSON 文件报错
- 原子写盘（.tmp + os.replace），解析/写盘异常时保留既有 JSON 并返回非零退出码
- 零外部依赖：仅用内置 zipfile + xml.etree.ElementTree
"""

import argparse
import json
import os
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile
from typing import Any, Dict, List, Optional

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import table_schema  # noqa: E402
import tool_config  # noqa: E402
from excel_parser import iter_xlsx_files, read_sheet_grid  # noqa: E402
from file_writer import remove_dir_if_empty  # noqa: E402
from naming_utils import to_property_name  # noqa: E402


def convert_value_by_type(raw_val: Any, cpp_type: Optional[str], nested_struct_types: Optional[dict] = None) -> Any:
    """
    根据表头第 3 行声明的 C++ 类型进行强类型转换（委托至 table_schema 唯一实现）
    @param raw_val 原始单元格值
    @param cpp_type C++ 类型声明
    @param nested_struct_types 嵌套 USTRUCT 类型映射
    @return 转换后的 JSON 可序列化值
    """
    return table_schema.convert_for_json(raw_val, cpp_type, nested_struct_types=nested_struct_types)


def convert_sheet_to_ue5_json(rows: List[List[Any]], nested_struct_types: Optional[dict] = None) -> List[Dict[str, Any]]:
    """
    将三行契约表格数据转为 UE5 DataTable 标准 JSON 格式（List of Objects）
    @param rows 表格二维行数据（前 3 行为表头契约，第 4 行起为数据行）
    @param nested_struct_types 嵌套 USTRUCT 类型映射
    @return UE5 DataTable JSON 对象数组
    """
    if len(rows) < 3:
        return []

    field_names = rows[1]
    cpp_types = rows[2]
    ue5_json_records: List[Dict[str, Any]] = []

    for row in rows[3:]:
        if not row or not any(str(v).strip() for v in row if v is not None):
            continue
        if row[0] is None or not str(row[0]).strip():
            continue

        record: Dict[str, Any] = {}
        for col_idx, val in enumerate(row):
            if col_idx >= len(field_names):
                continue

            field_name = str(field_names[col_idx]).strip() if field_names[col_idx] is not None else ""
            if not field_name:
                continue

            cpp_type = str(cpp_types[col_idx]).strip() if col_idx < len(cpp_types) and cpp_types[col_idx] is not None else "FString"
            prop_key = to_property_name(field_name, cpp_type)
            json_key = "Name" if field_name == "RowName" else prop_key
            converted_val = convert_value_by_type(val, cpp_type, nested_struct_types)

            if field_name == "RowName":
                record["RowName"] = converted_val
            record[json_key] = converted_val

        ue5_json_records.append(record)

    return ue5_json_records


def _write_json_atomic(json_path: str, records: List[Dict[str, Any]]) -> None:
    """
    以 UTF-8 / LF 原子落盘 JSON 文件（先写临时文件，成功后再 os.replace）
    @param json_path 目标 JSON 路径
    @param records 待序列化对象列表
    """
    os.makedirs(os.path.dirname(json_path), exist_ok=True)
    tmp_path = f"{json_path}.tmp-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    try:
        with open(tmp_path, "w", encoding="utf-8", newline="\n") as handle:
            json.dump(records, handle, ensure_ascii=False, indent=2, allow_nan=False)
        os.replace(tmp_path, json_path)
    except (OSError, ValueError, TypeError):
        if os.path.isfile(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
        raise


def _cleanup_orphan_json(output_dir: str, valid_json_paths: set[str]) -> int:
    """
    自底向上清理已删除数据表遗留的孤儿 JSON 文件与空分类目录
    @param output_dir JSON 输出根目录
    @param valid_json_paths 本轮有效（含因解析失败被保护的）JSON 路径集合
    @return 移除的孤儿文件数量
    """
    if not os.path.isdir(output_dir):
        return 0
    norm_output_dir = os.path.normpath(output_dir)
    removed_count = 0
    for root, _dirs, files in os.walk(output_dir, topdown=False):
        for filename in files:
            if not filename.endswith(".json"):
                continue
            full_p = os.path.normpath(os.path.join(root, filename))
            if full_p not in valid_json_paths:
                os.remove(full_p)
                removed_count += 1
                print(f"[×] 表已删除或表头无效，已移除孤儿 JSON 文件: {os.path.relpath(full_p, output_dir)}")
        if os.path.normpath(root) != norm_output_dir:
            remove_dir_if_empty(root)
    return removed_count


def export_all(input_dir: str, output_dir: str, nested_struct_types: Optional[dict] = None) -> bool:
    """
    遍历 input_dir 下的所有 .xlsx 文件并输出为 .json 文件
    @param input_dir Excel 数据表根目录
    @param output_dir JSON 输出根目录
    @param nested_struct_types 嵌套 USTRUCT 类型映射
    @return 全部处理无异常返回 True，存在任一解析或写入失败返回 False
    """
    print("==================================================")
    print("开始执行 Excel 转 JSON 批处理导出...")
    print(f"输入源目录: {input_dir}")
    print(f"输出目标目录: {output_dir}")
    print("==================================================")

    success_count = 0
    skip_count = 0
    had_error = False
    valid_json_paths: set[str] = set()

    for xlsx_path in iter_xlsx_files(input_dir, include_empty=True):
        file_name = os.path.basename(xlsx_path)
        table_name = os.path.splitext(file_name)[0]
        rel_dir = os.path.relpath(os.path.dirname(xlsx_path), input_dir)
        target_sub_dir = os.path.join(output_dir, rel_dir) if rel_dir != "." else output_dir
        json_path = os.path.normpath(os.path.join(target_sub_dir, f"{table_name}.json"))

        try:
            if os.path.getsize(xlsx_path) == 0:
                print(f"[-] 跳过空文件: {file_name}")
                skip_count += 1
                continue

            _, rows = read_sheet_grid(xlsx_path, selector=table_name)
            if len(rows) < 3:
                print(f"[-] 跳过表头契约不足 3 行的文件: {file_name} (行数: {len(rows)})")
                skip_count += 1
                continue

            json_records = convert_sheet_to_ue5_json(rows, nested_struct_types)
            _write_json_atomic(json_path, json_records)

            valid_json_paths.add(json_path)
            print(f"[+] 导出成功: {file_name} -> {os.path.relpath(json_path, output_dir)} (共 {len(json_records)} 条记录)")
            success_count += 1
        except (zipfile.BadZipFile, ET.ParseError, OSError, ValueError, TypeError) as e:
            print(f"[!] 导出失败 {file_name}: {e}", file=sys.stderr)
            had_error = True
            if os.path.isfile(json_path):
                valid_json_paths.add(json_path)

    removed_json_count = _cleanup_orphan_json(output_dir, valid_json_paths)
    if removed_json_count > 0:
        print(f"[×] 孤儿清理：已清除 {removed_json_count} 个已删除表的遗留 JSON 文件。")

    print("==================================================")
    print(f"导出完成！成功: {success_count} 个表，跳过: {skip_count} 个无效/空文件。")
    print(f"JSON 产物位置: {output_dir}")
    print("==================================================")
    return not had_error


def main() -> None:
    """
    命令行入口：加载配置并将 Excel 数据表批量导出为 UE5 DataTable JSON
    """
    parser = argparse.ArgumentParser(description="Excel 数据表转 JSON 导出工具（配置驱动，无内置默认路径）")
    parser.add_argument("--config", dest="config_path", default=None,
                        help="JSON 配置文件路径（缺省走共享配置发现）")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_JSON)
    ok = export_all(
        cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR],
        cfg["json_output_dir"],
        cfg.get("nested_struct_types"),
    )
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
