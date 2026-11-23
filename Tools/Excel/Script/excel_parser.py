#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel xlsx 解析模块 (excel_parser.py)

- iter_xlsx_files: 统一递归枚举目录下有效 .xlsx 文件（跳过临时文件、隐藏文件与空文件）
- read_sheet_grid: 读取选中工作表名与整张二维网格（zipfile + ElementTree + sharedStrings 唯一实现）
- get_sheet_data: 在网格之上切出表头三行契约与数据行键值
- parse_cell_ref / col_to_index: 单元格引用 (如 "B3") 解析工具
"""

import os
import re
import zipfile
import xml.etree.ElementTree as ET
from typing import Any, List, Optional, Sequence, Tuple

import table_schema

NS_MAIN = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"


def iter_xlsx_files(source_dir: str, include_empty: bool = False) -> List[str]:
    """
    递归扫描目录下全部有效 .xlsx 文件并按路径字典序稳定返回
    @param source_dir 源目录绝对路径
    @param include_empty 是否包含 0 字节文件（默认 False 跳过）
    @return 排序后的 .xlsx 绝对路径列表
    """
    if not source_dir or not os.path.isdir(source_dir):
        return []
    found: List[str] = []
    for root, dirs, files in os.walk(source_dir):
        dirs.sort()
        for filename in sorted(files):
            if not filename.endswith(".xlsx") or filename.startswith("~$") or filename.startswith("."):
                continue
            full_path = os.path.join(root, filename)
            if not include_empty:
                try:
                    if os.path.getsize(full_path) == 0:
                        continue
                except OSError:
                    continue
            found.append(full_path)
    return found


def col_to_index(col_str: str) -> int:
    """
    将列名 (如 "B") 转换为从 0 开始的列下标
    @param col_str Excel 列字母
    @return 0-based 列下标
    """
    idx = 0
    for char in col_str:
        idx = idx * 26 + (ord(char.upper()) - ord("A") + 1)
    return idx - 1


def parse_cell_ref(ref: str) -> Tuple[int, int]:
    """
    解析单元格引用 (如 "B3") 为 (列下标, 行号)，解析失败返回 (0, 0)
    @param ref 单元格引用字符串
    @return (col_idx, row_num)
    """
    m = re.match(r"([A-Za-z]+)(\d+)", ref)
    if m:
        return col_to_index(m.group(1)), int(m.group(2))
    return 0, 0


def _parse_cell_value(cell: ET.Element, strings: Sequence[str]) -> Any:
    """
    解析单个 SpreadsheetML <c> 元素的单元格值
    @param cell <c> 元素
    @param strings 共享字符串池
    @return 单元格值（字符串或布尔值）
    """
    cell_type = cell.get("t")
    if cell_type == "inlineStr":
        is_el = cell.find(f"{NS_MAIN}is")
        if is_el is not None:
            return "".join(x.text or "" for x in is_el.iter(f"{NS_MAIN}t"))

    v_el = cell.find(f"{NS_MAIN}v")
    raw = v_el.text if v_el is not None and v_el.text is not None else ""
    if cell_type == "s" and raw.isdigit():
        idx = int(raw)
        return strings[idx] if 0 <= idx < len(strings) else ""
    if cell_type == "b":
        return raw == "1"
    return raw


def _resolve_cell_col(cell: ET.Element, fallback_col: int) -> int:
    """
    解析单元格 0-based 列下标（优先取 r 属性如 B3，缺失时退回顺序游标）
    @param cell <c> 元素
    @param fallback_col 顺序回退列下标
    @return 0-based 列下标
    """
    ref = cell.get("r", "")
    if ref:
        m = re.match(r"([A-Za-z]+)(\d+)", ref)
        if m:
            return col_to_index(m.group(1))
    return fallback_col


def read_sheet_grid(file_path: str, selector: Optional[str] = None) -> Tuple[str, List[List[Any]]]:
    """
    读取目标工作表名与整张二维网格（所有 xlsx 读取的唯一实现）
    @param file_path xlsx 绝对路径
    @param selector 目标工作表选择键（None 时取文件名词干，与 openpyxl 选表规则对称）
    @return (选中的工作表名, grid)；表头不足 3 行或列宽为 0 时 grid 为空列表
    """
    with zipfile.ZipFile(file_path, "r") as z:
        strings = table_schema.read_zip_shared_strings(z, strict_xml=True)

        table_name = os.path.splitext(os.path.basename(file_path))[0]
        chosen = selector if selector is not None else table_name
        sheet_name, target_sheet_xml = table_schema.resolve_zip_worksheet(z, chosen, strings)
        if target_sheet_xml not in z.namelist():
            return sheet_name, []

        sheet_tree = ET.fromstring(z.read(target_sheet_xml))
        rows = sheet_tree.findall(f".//{NS_MAIN}row")
        if not rows:
            return sheet_name, []

        row_by_num: dict[int, ET.Element] = {}
        has_row_nums = False
        for idx, r_el in enumerate(rows, start=1):
            r_attr = r_el.get("r", "")
            if r_attr.isdigit():
                has_row_nums = True
                row_by_num[int(r_attr)] = r_el
            else:
                row_by_num[idx] = r_el

        if has_row_nums:
            if 2 not in row_by_num or 3 not in row_by_num:
                return sheet_name, []
        elif len(rows) < 3:
            return sheet_name, []

        ordered_rows: List[Optional[ET.Element]] = [
            row_by_num.get(1),
            row_by_num.get(2),
            row_by_num.get(3),
        ]
        for r_num in sorted(k for k in row_by_num if k > 3):
            ordered_rows.append(row_by_num[r_num])

        header_width = 0
        for row in ordered_rows[:3]:
            if row is None:
                continue
            next_col = 0
            for cell in row.findall(f"{NS_MAIN}c"):
                col_idx = _resolve_cell_col(cell, next_col)
                next_col = col_idx + 1
                header_width = max(header_width, col_idx + 1)

        if header_width == 0:
            return sheet_name, []

        grid: List[List[Any]] = []
        for row in ordered_rows:
            r_data: List[Any] = [""] * header_width
            if row is not None:
                next_col = 0
                for cell in row.findall(f"{NS_MAIN}c"):
                    col_idx = _resolve_cell_col(cell, next_col)
                    next_col = col_idx + 1
                    if col_idx < header_width:
                        r_data[col_idx] = _parse_cell_value(cell, strings)
            grid.append(r_data)

        return sheet_name, grid


def get_sheet_data(file_path: str) -> Tuple[str, List[List[str]], List[Tuple[str, str]]]:
    """
    读取 Excel 工作表名称、表头三行契约以及全部数据行键值
    @param file_path xlsx 绝对路径
    @return (sheet_name, headers, row_items)；row_items 为 [(row_key, display_name), ...]
    """
    sheet_name, grid = read_sheet_grid(file_path)
    if not grid:
        return sheet_name, [], []

    headers = [[str(cell) if cell is not None else "" for cell in row] for row in grid[:3]]
    row_items: List[Tuple[str, str]] = []
    for row in grid[3:]:
        if not row or not any(str(v).strip() for v in row if v is not None):
            continue
        key = str(row[0]).strip() if row[0] is not None else ""
        if not key:
            continue
        display = ""
        if len(row) > 1 and row[1] is not None and not isinstance(row[1], bool) and str(row[1]).strip():
            candidate = str(row[1]).strip()
            try:
                float(candidate)
            except ValueError:
                display = candidate
        if not display:
            display = key
        row_items.append((key, display))

    return sheet_name, headers, row_items
