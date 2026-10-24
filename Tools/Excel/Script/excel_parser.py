#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel xlsx 解析模块 (excel_parser.py)

- read_sheet_grid: 读取工作表名与整张二维网格（zipfile + ElementTree + sharedStrings 的唯一实现）
- get_sheet_data: 在网格之上切出表头三行契约与数据行键值
- parse_cell_ref / col_to_index: 单元格引用 (如 "B3") 解析工具（本模块为唯一实现）

实现思路：解析 workbook.xml 取工作表标签名，解析 sharedStrings.xml 建共享字符串池，
经 table_schema.resolve_zip_worksheet_path 定位目标工作表 XML（不硬编码 sheet1.xml），
扫描前三行确定有效列宽后逐单元格重建二维数据；inlineStr/str 内联字符串取自 <is><t>，
t="s" 共享字符串取自字符串池。
"""

import os
import re
import zipfile
import xml.etree.ElementTree as ET
from typing import Any, List, Optional, Tuple

import table_schema

# SpreadsheetML 主命名空间（全工具唯一常量，不再各处内联重复字面量）
NS_MAIN = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"


def col_to_index(col_str: str) -> int:
    """将列名 (如 "B") 转换为从 0 开始的列下标"""
    idx = 0
    for char in col_str:
        idx = idx * 26 + (ord(char.upper()) - ord("A") + 1)
    return idx - 1


def parse_cell_ref(ref: str) -> Tuple[int, int]:
    """解析单元格引用 (如 "B3") 为 (列下标, 行号)，解析失败返回 (0, 0)"""
    m = re.match(r"([A-Za-z]+)(\d+)", ref)
    if m:
        return col_to_index(m.group(1)), int(m.group(2))
    return 0, 0


def read_sheet_grid(file_path: str, selector: Optional[str] = None) -> Tuple[str, List[List[str]]]:
    """
    读取工作表名与整张二维网格（所有 xlsx 读取的唯一实现）
    @param file_path xlsx 绝对路径
    @param selector 目标工作表选择键（None 时优先取工作表标签名，否则取文件名词干）
    @return (sheet_name, grid)；表头不足 3 行或列宽为 0 时 grid 为空列表
    """
    with zipfile.ZipFile(file_path, "r") as z:
        sheet_name = ""
        if "xl/workbook.xml" in z.namelist():
            wb_tree = ET.fromstring(z.read("xl/workbook.xml"))
            sheet_elem = wb_tree.find(f".//{NS_MAIN}sheet")
            if sheet_elem is not None:
                sheet_name = sheet_elem.attrib.get("name", "")

        strings: List[str] = []
        if "xl/sharedStrings.xml" in z.namelist():
            tree = ET.fromstring(z.read("xl/sharedStrings.xml"))
            for si in tree.findall(f"{NS_MAIN}si"):
                text = "".join(t.text for t in si.iter(f"{NS_MAIN}t") if t.text)
                strings.append(text)

        table_name = os.path.splitext(os.path.basename(file_path))[0]
        chosen = selector if selector is not None else (sheet_name or table_name)
        target_sheet_xml = table_schema.resolve_zip_worksheet_path(z, chosen)
        if target_sheet_xml not in z.namelist():
            return sheet_name, []
        sheet_tree = ET.fromstring(z.read(target_sheet_xml))
        rows = sheet_tree.findall(f".//{NS_MAIN}row")
        if len(rows) < 3:
            return sheet_name, []

        header_width = 0
        for r in rows[:3]:
            for c in r.findall(f"{NS_MAIN}c"):
                col_idx, _ = parse_cell_ref(c.get("r", "A1"))
                header_width = max(header_width, col_idx + 1)

        if header_width == 0:
            return sheet_name, []

        grid: List[List[Any]] = []
        for row in rows:
            r_data: List[Any] = [""] * header_width
            for c in row.findall(f"{NS_MAIN}c"):
                col_idx, _ = parse_cell_ref(c.get("r", "A1"))
                if col_idx < header_width:
                    t = c.get("t")
                    v = c.find(f"{NS_MAIN}v")
                    val = v.text if v is not None else ""
                    if t in ("inlineStr", "str"):
                        is_el = c.find(f"{NS_MAIN}is")
                        if is_el is not None:
                            val = "".join(x.text or "" for x in is_el.iter(f"{NS_MAIN}t"))
                    elif t == "s" and val.isdigit():
                        val = strings[int(val)]
                    elif t == "b":
                        val = True if val == "1" else False
                    r_data[col_idx] = val
            grid.append(r_data)

        return sheet_name, grid


def get_sheet_data(file_path: str) -> Tuple[str, List[List[str]], List[Tuple[str, str]]]:
    """
    读取 Excel 工作表名称、表头三行契约以及全部数据行键值
    @param file_path xlsx 绝对路径
    @return (sheet_name, headers, row_items)；row_items 为 [(row_key, display_name), ...]

    实现思路：解析收敛在 read_sheet_grid；此处仅切出前 3 行表头，并把数据行首列作主键、
    次列作展示名（次列若为纯数字则回落为主键本身）。
    """
    sheet_name, grid = read_sheet_grid(file_path)
    if not grid:
        return sheet_name, [], []

    headers = grid[:3]
    row_items: List[Tuple[str, str]] = []
    for row in grid[3:]:
        if not row or not any(v.strip() for v in row):
            continue
        key = row[0].strip()
        if not key:
            continue
        display = ""
        if len(row) > 1 and row[1] and str(row[1]).strip():
            candidate = str(row[1]).strip()
            try:
                float(candidate)
            except ValueError:
                display = candidate
        if not display:
            display = key
        row_items.append((key, display))

    return sheet_name, headers, row_items
