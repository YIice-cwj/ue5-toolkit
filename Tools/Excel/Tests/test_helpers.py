#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Python 自动化测试公共辅助模块 (test_helpers.py)

- 统一配置 Tools/Script 与 Tools/Excel/Script 的导入路径
- 提供基于 openpyxl 的标准三行契约 .xlsx 构造器与临时工程目录夹具
"""

import json
import os
import sys
from typing import Any, Dict, List, Optional, Sequence, Tuple

import openpyxl

TESTS_DIR = os.path.dirname(os.path.abspath(__file__))
EXCEL_DIR = os.path.dirname(TESTS_DIR)
TOOLS_DIR = os.path.dirname(EXCEL_DIR)
EXCEL_SCRIPT_DIR = os.path.join(EXCEL_DIR, "Script")
TOOLS_SCRIPT_DIR = os.path.join(TOOLS_DIR, "Script")

for _p in (EXCEL_SCRIPT_DIR, TOOLS_SCRIPT_DIR):
    if _p not in sys.path:
        sys.path.insert(0, _p)


def create_xlsx(
    path: str,
    sheet_name: str,
    headers: Tuple[Sequence[Any], Sequence[Any], Sequence[Any]],
    rows: Sequence[Sequence[Any]] = (),
    extra_sheets_before: Optional[List[Tuple[str, Sequence[Sequence[Any]]]]] = None,
) -> str:
    """
    创建符合三行契约的 .xlsx 测试文件（支持前置说明页以测试多工作表选表对称性）
    @param path 目标 .xlsx 绝对路径
    @param sheet_name 数据表工作表标签名
    @param headers 三行表头 (中文名, 字段名, C++ 类型)
    @param rows 数据行序列（第 4 行起）
    @param extra_sheets_before 可选前置说明工作表列表 [(sheet_title, rows), ...]
    @return 创建完成的 .xlsx 绝对路径
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    wb = openpyxl.Workbook()
    default_ws = wb.active

    if extra_sheets_before:
        first_title, first_rows = extra_sheets_before[0]
        default_ws.title = first_title
        for r in first_rows:
            default_ws.append(list(r))
        for title, srows in extra_sheets_before[1:]:
            ws_extra = wb.create_sheet(title=title)
            for r in srows:
                ws_extra.append(list(r))
        ws = wb.create_sheet(title=sheet_name)
    else:
        ws = default_ws
        ws.title = sheet_name

    for header_row in headers:
        ws.append(list(header_row))
    for data_row in rows:
        ws.append(list(data_row))

    wb.save(path)
    return path


def create_mini_project(root_dir: str, project_name: str = "DemoProj", module_name: str = "DemoMod") -> Dict[str, str]:
    """
    在临时目录内创建精简 UE 工程布局与配置文件
    @param root_dir 临时工程根目录
    @param project_name 工程名
    @param module_name 输出模块名
    @return 路径字典（root / uproject / data_tables / json_dir / module_dir / tags_ini / config）
    """
    uproject = os.path.join(root_dir, f"{project_name}.uproject")
    with open(uproject, "w", encoding="utf-8") as handle:
        json.dump({"FileVersion": 3, "EngineAssociation": "5.8", "Modules": [{"Name": project_name}]}, handle)

    data_tables = os.path.join(root_dir, "DataTables")
    json_dir = os.path.join(root_dir, "DataTables_Json")
    module_dir = os.path.join(root_dir, "Plugins", module_name, "Source", module_name)
    tags_ini = os.path.join(root_dir, "Config", "DefaultGameplayTags.ini")
    reg_header = os.path.join(root_dir, "Source", project_name, "Public", "Registration", "GameRegistration.h")
    os.makedirs(data_tables, exist_ok=True)
    os.makedirs(module_dir, exist_ok=True)

    config_path = os.path.join(root_dir, "generator_config.json")
    cfg_payload = {
        "data_tables_source_dir": "DataTables",
        "json_output_dir": "DataTables_Json",
        "gameplay_tags_ini_path": "Config/DefaultGameplayTags.ini",
        "output_module_dir": f"Plugins/{module_name}/Source/{module_name}",
        "registration": {
            "output_header": f"Source/{project_name}/Public/Registration/GameRegistration.h",
            "source_roots": ["Source", "Plugins"],
        },
    }
    with open(config_path, "w", encoding="utf-8") as handle:
        json.dump(cfg_payload, handle, indent=2)

    return {
        "root": root_dir,
        "uproject": uproject,
        "data_tables": data_tables,
        "json_dir": json_dir,
        "module_dir": module_dir,
        "tags_ini": tags_ini,
        "reg_header": reg_header,
        "config": config_path,
    }
