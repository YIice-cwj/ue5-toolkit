#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
命名转换工具模块 (naming_utils.py)

- to_pascal_case: 下划线命名 -> PascalCase（工作表标签名 -> 词根）
- pascal_to_snake: PascalCase -> snake_case
- to_field_name: 表词根 -> 紧凑规范的成员字段名（PascalCase）
- to_property_name: 字段名 -> 符合 UE5 规范的属性名（PascalCase，布尔加 b 前缀）
"""

import re


def to_pascal_case(name: str) -> str:
    """将下划线命名转换为 PascalCase（如 DT_Item_Definition -> ItemDefinition），仅首字母大写并保留内部大小写

    实现思路：先剥离 DT_ 或 DT 前缀，再按下划线分段，每段仅将首字母大写、保留后续内部大小写（如 ProbeReaction, HitPart 等）
    """
    if name.startswith("DT_"):
        name = name[3:]
    elif name.startswith("DT"):
        name = name[2:]

    parts = name.split("_")
    pascal_parts = []
    for part in parts:
        if not part:
            continue
        pascal_parts.append(part[:1].upper() + part[1:])
    return "".join(pascal_parts)


def pascal_to_snake(name: str) -> str:
    """将 PascalCase 转换为 snake_case"""
    s1 = re.sub(r'(.)([A-Z][a-z]+)', r'\1_\2', name)
    return re.sub(r'([a-z0-9])([A-Z])', r'\1_\2', s1).lower()


def to_field_name(stem: str, cat_folder: str) -> str:
    """将表词根转换为紧凑规范的成员字段名（大驼峰 PascalCase）"""
    if stem.lower().startswith(cat_folder.lower()) and len(stem) > len(cat_folder):
        sub = stem[len(cat_folder):]
    else:
        sub = stem
    sub = sub.lstrip("_")
    return to_pascal_case(sub)


def to_property_name(field_name: str, cpp_type: str = "") -> str:
    """将表头字段名转换为符合 UE5 官方规范的属性名（全域大驼峰 PascalCase，布尔强制 b 驼峰）"""
    if not field_name:
        return ""
    if field_name in ("RowName", "Name"):
        return field_name

    pascal = to_pascal_case(field_name)
    cleaned_type = cpp_type.strip().lower() if cpp_type else ""
    if cleaned_type == "bool":
        if pascal.startswith("b") and len(pascal) > 1 and pascal[1].isupper():
            return pascal
        if pascal.startswith("B") and len(pascal) > 1 and pascal[1].isupper():
            return "b" + pascal[1:]
        return "b" + pascal
    return pascal
