#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Excel 数据表 FGameplayTag 列 → DefaultGameplayTags.ini 生成器 (gameplay_tags_to_ini.py)

- 扫描 data_tables_source_dir 下全部 xlsx，取表头三行契约第 3 行类型声明含 FGameplayTag 的列；
- 收集这些列在数据区（第 4 行起）出现的全部标签值（按逗号拆分、去空白、去空串、去重）；
- 按字典序稳定排序后写入目标 ini 的 [/Script/GameplayTags.GameplayTagsSettings] 段；
- 纯配置驱动：目标 ini 路径取自配置键 gameplay_tags_ini_path，未配置则跳过并明确提示。
"""

import argparse
import os
import sys
from typing import Any, Dict, Set

import tool_config
from excel_parser import read_sheet_grid

INI_SECTION = "[/Script/GameplayTags.GameplayTagsSettings]"

HEADER_LINES = (
    "; GameplayTag 数据表注册（由 Tools/Excel 管线依据 Excel 的 FGameplayTag 列自动生成，请勿手动编辑）",
    "; 仅登记数据表 FGameplayTag 列实际出现的标签；C++ 逻辑所需标签见各模块原生注册。",
)


def is_xlsx_source(filename: str) -> bool:
    """
    判定是否为可处理的 xlsx 源文件（排除临时文件与隐藏文件）
    @param filename 文件名
    @return 可处理返回 true
    """
    return filename.endswith(".xlsx") and not filename.startswith("~$") and not filename.startswith(".")


def collect_tags(input_dir: str) -> Set[str]:
    """
    遍历输入目录下全部 xlsx，收集 FGameplayTag 列出现过的标签值全集
    @param input_dir Excel 源目录绝对路径
    @return 去重后的标签字符串集合
    """
    tags = set()
    for root, _, files in os.walk(input_dir):
        for filename in sorted(files):
            if not is_xlsx_source(filename):
                continue
            file_path = os.path.join(root, filename)
            if os.path.getsize(file_path) == 0:
                continue
            _, rows = read_sheet_grid(file_path)
            if len(rows) < 4:
                continue
            cpp_types = rows[2]
            for col_idx, raw_type in enumerate(cpp_types):
                if raw_type is None or "gameplaytag" not in str(raw_type).lower():
                    continue
                for data_row in rows[3:]:
                    if col_idx >= len(data_row):
                        continue
                    for part in str(data_row[col_idx]).split(","):
                        tag = part.strip()
                        if tag:
                            tags.add(tag)
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


def export_tags_ini(cfg: Dict[str, Any]) -> bool:
    """
    执行一次标签 ini 生成（未配置输出路径时跳过）
    @param cfg 工具配置字典
    @return 始终返回 True（未配置输出路径视为跳过）
    """
    output_path = cfg.get("gameplay_tags_ini_path")
    if not output_path:
        print("[跳过] 未配置 gameplay_tags_ini_path，跳过 GameplayTag 注册 ini 生成。")
        return True

    source_dir = cfg[tool_config.KEY_DATA_TABLES_SOURCE_DIR]
    has_any_xlsx = False
    if os.path.isdir(source_dir):
        for _root, _dirs, files in os.walk(source_dir):
            if any(f.endswith(".xlsx") and not f.startswith("~$") and not f.startswith(".") for f in files):
                has_any_xlsx = True
                break

    if not has_any_xlsx:
        if os.path.isfile(output_path):
            os.remove(output_path)
            print(f"[×] 数据表已全部清空，已删除 GameplayTags 注册文件: {output_path}")
        else:
            print("[跳过] 当前无数据表，跳过 GameplayTags 注册文件生成。")
        return True

    tags = collect_tags(source_dir)
    content = build_ini_content(tags)
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    print(f"[+] 已生成 GameplayTag 注册清单: {output_path}（共 {len(tags)} 条标签）")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description="Excel FGameplayTag 列 → DefaultGameplayTags.ini 生成器（配置驱动）")
    parser.add_argument("--config", dest="config_path", default=None, help="JSON 配置文件路径")
    parsed_args = parser.parse_args()

    cfg = tool_config.load_config(parsed_args.config_path, tool_config.KEYS_TAGS)
    export_tags_ini(cfg)
    sys.exit(0)


if __name__ == "__main__":
    main()
