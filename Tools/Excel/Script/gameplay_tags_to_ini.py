#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表 FGameplayTag 列 → DefaultGameplayTags.ini 生成器 (gameplay_tags_to_ini.py)

- 扫描 data_tables_source_dir 下全部有效 xlsx，取表头第 3 行声明含 FGameplayTag 的列；
- 通过 table_schema 统一解析数组字面量与 (TagName="...") 形态，去空白、去空串、去重；
- 按字典序稳定排序后原子写入目标 ini 的 [/Script/GameplayTags.GameplayTagsSettings] 段；
- 纯配置驱动：目标 ini 路径取自配置键 gameplay_tags_ini_path，未配置则跳过。
"""

import argparse
import os
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile
from typing import Any, Dict, Optional, Sequence, Set, Tuple

TOOLS_SCRIPT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Script")
)
if TOOLS_SCRIPT_DIR not in sys.path:
    sys.path.insert(0, TOOLS_SCRIPT_DIR)

import table_schema  # noqa: E402
import tool_config  # noqa: E402
from excel_parser import iter_xlsx_files, read_sheet_grid  # noqa: E402

INI_SECTION = "[/Script/GameplayTags.GameplayTagsSettings]"

HEADER_LINES = (
    "; GameplayTag 数据表注册（由 Tools/Excel 管线依据 Excel 的 FGameplayTag 列自动生成，请勿手动编辑）",
    "; 仅登记数据表 FGameplayTag 列实际出现的标签；C++ 逻辑所需标签见各模块原生注册。",
)


def _extract_tags_from_cell(raw_val: Any, is_array: bool) -> Set[str]:
    """
    从单个 FGameplayTag / TArray<FGameplayTag> 单元格提取规范化标签名集合
    @param raw_val 原始单元格值
    @param is_array 列声明是否为数组/集合类型
    @return 去重后的非空标签名集合
    """
    if raw_val is None or str(raw_val).strip() == "":
        return set()
    raw_items = table_schema.parse_raw_array_items(raw_val) if is_array else [raw_val]
    extracted: Set[str] = set()
    for item in raw_items:
        unwrapped = table_schema.unwrap_tag_literal(item)
        tag = str(unwrapped).strip() if unwrapped is not None else ""
        if tag and not any(ch in tag for ch in ('"', "'", "\n", "\r", "\t", " ")):
            extracted.add(tag)
    return extracted


def collect_tags_with_status(
    input_dir: str,
    xlsx_files: Optional[Sequence[str]] = None,
) -> Tuple[Set[str], bool]:
    """
    遍历输入目录下全部有效 xlsx，收集 FGameplayTag 列标签全集并报告解析是否全部成功
    @param input_dir Excel 源目录绝对路径
    @param xlsx_files 可选预扫描的 xlsx 文件路径序列（避免重复遍历目录）
    @return (去重后的标签字符串集合, 是否全部解析成功)
    """
    tags: Set[str] = set()
    had_error = False
    files_to_scan = list(xlsx_files) if xlsx_files is not None else iter_xlsx_files(input_dir)

    for file_path in files_to_scan:
        file_name = os.path.basename(file_path)
        table_name = os.path.splitext(file_name)[0]
        try:
            _, rows = read_sheet_grid(file_path, selector=table_name)
        except (zipfile.BadZipFile, ET.ParseError, OSError) as e:
            print(f"[!] 读取 GameplayTag 数据表失败 {file_name}: {e}", file=sys.stderr)
            had_error = True
            continue

        if len(rows) < 4:
            continue

        field_names = rows[1]
        cpp_types = rows[2]
        tag_columns = [
            (col_idx, table_schema.is_array_type(str(raw_type)))
            for col_idx, raw_type in enumerate(cpp_types)
            if (
                col_idx < len(field_names)
                and field_names[col_idx] is not None
                and str(field_names[col_idx]).strip()
                and raw_type is not None
                and table_schema.is_tag_type(str(raw_type))
            )
        ]
        for data_row in rows[3:]:
            if not data_row or not any(str(v).strip() for v in data_row if v is not None):
                continue
            if data_row[0] is None or not str(data_row[0]).strip():
                continue
            for col_idx, is_arr in tag_columns:
                if col_idx < len(data_row):
                    tags.update(_extract_tags_from_cell(data_row[col_idx], is_arr))

    return tags, not had_error


def collect_tags(input_dir: str) -> Set[str]:
    """
    遍历输入目录下全部 xlsx，收集 FGameplayTag 列出现过的标签值全集
    @param input_dir Excel 源目录绝对路径
    @return 去重后的标签字符串集合
    """
    tags, _ = collect_tags_with_status(input_dir)
    return tags


def build_ini_content(tags: Set[str]) -> str:
    """
    依据标签集合构建 ini 文本（字典序稳定输出）
    @param tags 标签集合
    @return ini 文件内容
    """
    lines = list(HEADER_LINES)
    lines.append("")
    lines.append(INI_SECTION)
    for tag in sorted(tags):
        lines.append(f'+GameplayTagList=(Tag="{tag}")')
    return "\n".join(lines) + "\n"


def _write_text_atomic(output_path: str, content: str) -> None:
    """
    以 UTF-8 / LF 原子落盘文本文件
    @param output_path 目标文件路径
    @param content 文件内容
    """
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    tmp_path = f"{output_path}.tmp-{os.getpid()}-{uuid.uuid4().hex[:8]}"
    try:
        with open(tmp_path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(content)
        os.replace(tmp_path, output_path)
    except OSError:
        if os.path.isfile(tmp_path):
            try:
                os.remove(tmp_path)
            except OSError:
                pass
        raise


def export_tags_ini(cfg: Dict[str, Any]) -> bool:
    """
    执行一次标签 ini 生成（未配置输出路径时跳过；解析失败返回 False）
    @param cfg 工具配置字典
    @return 成功或跳过返回 True，解析或写入失败返回 False
    """
    output_path = cfg.get("gameplay_tags_ini_path")
    if not output_path:
        print("[跳过] 未配置 gameplay_tags_ini_path，跳过 GameplayTag 注册 ini 生成。")
        return True

    source_dir = cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR]
    xlsx_files = iter_xlsx_files(source_dir)
    if not xlsx_files:
        if os.path.isfile(output_path):
            os.remove(output_path)
            print(f"[×] 数据表已全部清空，已删除 GameplayTags 注册文件: {output_path}")
        else:
            print("[跳过] 当前无数据表，跳过 GameplayTags 注册文件生成。")
        return True

    tags, ok = collect_tags_with_status(source_dir, xlsx_files=xlsx_files)
    if not ok:
        print("[✗] 存在无法解析的数据表，中止 DefaultGameplayTags.ini 生成以保留既有配置。", file=sys.stderr)
        return False

    content = build_ini_content(tags)
    try:
        _write_text_atomic(output_path, content)
    except OSError as e:
        print(f"[✗] 写入 GameplayTag 注册清单失败 {output_path}: {e}", file=sys.stderr)
        return False

    print(f"[+] 已生成 GameplayTag 注册清单: {output_path}（共 {len(tags)} 条标签）")
    return True


def main() -> None:
    """
    命令行入口：加载配置并生成 DefaultGameplayTags.ini
    """
    parser = argparse.ArgumentParser(description="Excel FGameplayTag 列 → DefaultGameplayTags.ini 生成器（配置驱动）")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_TAGS)
    ok = export_tags_ini(cfg)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
