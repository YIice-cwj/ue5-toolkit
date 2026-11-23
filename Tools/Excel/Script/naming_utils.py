#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
命名转换工具模块 (naming_utils.py)

- strip_dt_prefix: 剥离工作表名开头的 DT_ 或 DT 前缀（不误伤名称中间的 DT 字样）
- to_pascal_case: 下划线命名 -> PascalCase（工作表标签名 -> 词根）
- pascal_to_snake: PascalCase -> snake_case
- to_field_name: 表词根 -> 紧凑规范的成员字段名（PascalCase）
- to_property_name: 字段名 -> 符合 UE5 规范的属性名（PascalCase，布尔加 b 前缀）
"""

import re


def strip_dt_prefix(name: str) -> str:
    """
    剥离数据表名开头的 DT_ 或 DT 前缀（精确前缀匹配，不误伤中间含 DT 的标识）
    @param name 原始表名或文件名词干
    @return 去除前缀后的名称
    """
    cleaned = (name or "").strip()
    if cleaned.startswith("DT_"):
        return cleaned[3:]
    if cleaned.startswith("DT"):
        return cleaned[2:]
    return cleaned


def _to_pascal_words(name: str) -> str:
    """
    将下划线分隔命名转换为 PascalCase（不剥离 DT 前缀，保留每个词段内部大写）
    @param name 原始名称字符串
    @return PascalCase 字符串
    """
    cleaned = (name or "").strip()
    parts = cleaned.split("_")
    pascal_parts = []
    for part in parts:
        if not part:
            continue
        pascal_parts.append(part[:1].upper() + part[1:])
    return "".join(pascal_parts)


def to_pascal_case(name: str) -> str:
    """
    将工作表下划线命名转换为 PascalCase（先剥离 DT_ / DT 前缀，如 DT_Item_Definition -> ItemDefinition）
    @param name 原始下划线命名或工作表名
    @return 仅首字母大写、保留内部大小写的 PascalCase 字符串
    """
    return _to_pascal_words(strip_dt_prefix(name))


def pascal_to_snake(name: str) -> str:
    """
    将 PascalCase 转换为 snake_case
    @param name PascalCase 字符串
    @return snake_case 字符串
    """
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1).lower()


def to_field_name(stem: str, cat_folder: str) -> str:
    """
    将表词根转换为紧凑规范的成员字段名（大驼峰 PascalCase，确保剥离分类前缀后首字符仍为合法 C++ 标识符字母）
    @param stem 表词根
    @param cat_folder 所属分类目录名
    @return 去除冗余分类前缀后的合法 PascalCase 字段名
    """
    if stem.lower().startswith(cat_folder.lower()) and len(stem) > len(cat_folder):
        boundary = stem[len(cat_folder)]
        if boundary == "_" or boundary.isupper():
            candidate = _to_pascal_words(stem[len(cat_folder):].lstrip("_"))
            if candidate and candidate[0].isalpha():
                return candidate
    return _to_pascal_words(stem.lstrip("_"))


def to_property_name(field_name: str, cpp_type: str = "") -> str:
    """
    将表头字段名转换为符合 UE5 官方规范的属性名（全域大驼峰 PascalCase，布尔强制 b 驼峰）
    @param field_name 表头字段名
    @param cpp_type C++ 类型声明
    @return UE5 规范属性名
    """
    raw = (field_name or "").strip()
    if not raw:
        return ""
    if raw in ("RowName", "Name"):
        return raw

    cleaned_type = cpp_type.strip().lower() if cpp_type else ""
    if cleaned_type == "bool":
        if raw.startswith("b") and len(raw) > 1 and raw[1].isupper():
            return "b" + _to_pascal_words(raw[1:])
        if raw.startswith("B") and len(raw) > 2 and raw[1].isupper() and raw[2].islower():
            return "b" + _to_pascal_words(raw[1:])
        if raw.startswith("b_") and len(raw) > 2:
            return "b" + _to_pascal_words(raw[2:])
        return "b" + _to_pascal_words(raw)
    return _to_pascal_words(raw)
