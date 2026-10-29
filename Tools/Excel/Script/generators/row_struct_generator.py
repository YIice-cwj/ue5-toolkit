#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RowStruct 行结构生成器 (row_struct_generator.py)

生成每张数据表的 RowStruct 头文件：
- 键模式 fname（默认）：不生成枚举，DataTable 行键直接使用 FName
- 键模式 enum（仅小表）：生成 E{stem}Key 唯一键枚举 + ToRowName / ToString 内联辅助函数
- USTRUCT F{stem}Row : public FTableRowBase
字段类型统一经 cpp_type_utils.validate_field_type 校验（与序列化生成共用白名单）。
"""

import os
import re
from typing import Any, Mapping, Optional

from generators import GeneratedFile, GeneratorContext
from cpp_type_utils import clean_cpp_type, get_default_init_expr, validate_field_type
from naming_utils import to_property_name

KEY_MODE_FNAME = "fname"
KEY_MODE_ENUM = "enum"


def table_names(stem: str, key_mode: str = KEY_MODE_FNAME) -> tuple:
    """由表词根推导 (header_name, struct_name, enum_name)，与 generate_struct_header 内部命名一致"""
    enum_name = f"E{stem}Key" if key_mode == KEY_MODE_ENUM else ""
    return f"{stem}Row.h", f"F{stem}Row", enum_name


def _build_enum_items(row_items: list) -> list:
    """
    构建键枚举条目（去重并清洗为合法 C++ 标识符）
    @param row_items [(原始键, 显示名), ...]
    @return [(清洗后标识符, 原始键, 显示名), ...]
    """
    seen_ids = set(["None"])
    valid_items = []
    for raw_key, raw_display in row_items:
        clean_id = re.sub(r'[^a-zA-Z0-9_]', '_', raw_key.strip())
        if clean_id and clean_id[0].isdigit():
            clean_id = "_" + clean_id
        if not clean_id or clean_id in seen_ids:
            continue
        seen_ids.add(clean_id)
        clean_display = raw_display.replace('"', '').replace('\\', '').strip()
        valid_items.append((clean_id, raw_key, clean_display))
    return valid_items


def generate_struct_header(
    xlsx_filename: str,
    stem: str,
    desc: str,
    headers: list,
    row_items: list,
    key_mode: str = KEY_MODE_FNAME,
    nested_struct_types: Optional[Mapping[str, Any]] = None,
) -> tuple:
    """根据表头前三行与数据行键值生成标准 UE5 C++ RowStruct 头文件代码

    实现思路：按 GameplayTag 字段补头、按嵌套 USTRUCT 配置补头；字段遍历自第 2 列起
    （第 1 列 RowName 由 FTableRowBase 自带，故跳过）；字段类型先经统一白名单校验。
    """
    chinese_names = headers[0]
    field_names = headers[1]
    cpp_types = headers[2]

    struct_name = f"F{stem}Row"
    enum_name = f"E{stem}Key" if key_mode == KEY_MODE_ENUM else ""
    header_name = f"{stem}Row.h"
    category = stem
    generated_header = f"{stem}Row.generated.h"
    nested_struct_types = nested_struct_types or {}

    has_gameplay_tag = any("gameplaytag" in t.lower() for t in cpp_types)

    forward_decls = []
    if any("UNiagaraSystem" in t for t in cpp_types):
        forward_decls.append("class UNiagaraSystem;")

    validated_fields = []
    for col_idx in range(1, len(field_names)):
        field_name = field_names[col_idx]
        if not field_name:
            continue
        chinese_name = chinese_names[col_idx] if col_idx < len(chinese_names) else field_name
        cpp_type = clean_cpp_type(cpp_types[col_idx] if col_idx < len(cpp_types) else "FString")
        validate_field_type(cpp_type, nested_struct_types, context=f"{xlsx_filename} 字段 [{field_name}]")
        prop_name = to_property_name(field_name, cpp_type)
        validated_fields.append((chinese_name, cpp_type, prop_name))

    used_nested_types = sorted({cpp_type for _, cpp_type, _ in validated_fields if cpp_type in nested_struct_types})

    lines = []
    lines.append("/**")
    lines.append(f" * @file {header_name}")
    lines.append(f" * @brief 数据表行数据结构（生成自 {xlsx_filename}）")
    lines.append(" */")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "CoreMinimal.h"')
    lines.append('#include "Engine/DataTable.h"')
    if has_gameplay_tag:
        lines.append('#include "GameplayTagContainer.h"')
    for type_name in used_nested_types:
        lines.append(f'#include "{nested_struct_types[type_name]}"')
    lines.append(f'#include "{generated_header}"')
    lines.append("")

    if forward_decls:
        for decl in forward_decls:
            lines.append(decl)
        lines.append("")

    if key_mode == KEY_MODE_ENUM:
        valid_items = _build_enum_items(row_items)
        if len(valid_items) + 1 > 255:
            raise ValueError(
                f"{xlsx_filename}: 枚举键模式最多支持 255 个键（含 None），当前 {len(valid_items) + 1} 个；"
                f"请将 row_key_mode 设为 fname 以解除行数限制"
            )

        lines.append("/**")
        lines.append(f" * @brief {desc} 唯一键枚举（消除魔术字符串，支持类型安全与自动补全）")
        lines.append(" */")
        lines.append("UENUM(BlueprintType)")
        lines.append(f"enum class {enum_name} : uint8 {{")
        lines.append('    None = 0 UMETA(DisplayName = "无"),')
        for clean_id, _raw_key, clean_display in valid_items:
            lines.append(f'    {clean_id} UMETA(DisplayName = "{clean_display}"),')
        lines.append("};")
        lines.append("")

        lines.append("/**")
        lines.append(f" * @brief 将 {enum_name} 转换为 DataTable RowName")
        lines.append(" * @param key 唯一键枚举值")
        lines.append(" * @return 对应的 DataTable RowName")
        lines.append(" */")
        lines.append(f"inline FName ToRowName({enum_name} key) noexcept {{")
        lines.append("    switch (key) {")
        for clean_id, raw_key, _clean_display in valid_items:
            lines.append(f'        case {enum_name}::{clean_id}: return TEXT("{raw_key}");')
        lines.append("        default: return NAME_None;")
        lines.append("    }")
        lines.append("}")
        lines.append("")

        lines.append("/**")
        lines.append(f" * @brief 将 {enum_name} 转换为字符串表示")
        lines.append(" * @param key 唯一键枚举值")
        lines.append(" * @return 静态字符串表示")
        lines.append(" */")
        lines.append(f"inline const TCHAR* ToString({enum_name} key) noexcept {{")
        lines.append("    switch (key) {")
        for clean_id, _raw_key, _clean_display in valid_items:
            lines.append(f'        case {enum_name}::{clean_id}: return TEXT("{clean_id}");')
        lines.append('        default: return TEXT("None");')
        lines.append("    }")
        lines.append("}")
        lines.append("")

    lines.append("/**")
    lines.append(f" * @brief {desc}")
    lines.append(" */")
    lines.append("USTRUCT(BlueprintType)")
    lines.append(f"struct {struct_name} : public FTableRowBase {{")
    lines.append("    GENERATED_BODY()")
    lines.append("")
    lines.append("public:")

    for chinese_name, cpp_type, prop_name in validated_fields:
        init_expr = get_default_init_expr(cpp_type)
        lines.append("    /** " + chinese_name + " */")
        lines.append(f'    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{category}", meta = (DisplayName = "{chinese_name}"))')
        lines.append(f"    {cpp_type} {prop_name}{init_expr}")
        lines.append("")

    lines.append("};")
    lines.append("")

    return header_name, struct_name, enum_name, "\n".join(lines)


def generate(ctx: GeneratorContext, xlsx_filename: str, stem: str, desc: str, headers: list, row_items: list, cat_folder: str) -> list:
    """
    生成单张表的 RowStruct 头文件产物（由 main.py 扫描时逐表调用）
    @param ctx 生成器运行上下文
    @param xlsx_filename 源 xlsx 文件名
    @param stem 表词根
    @param desc 表描述文本
    @param headers 表头三行契约
    @param row_items 数据行键值列表
    @param cat_folder 分类目录
    @return 单元素列表：RowStruct 头文件（overwrite）
    """
    header_name, _, _, cpp_code = generate_struct_header(
        xlsx_filename, stem, desc, headers, row_items,
        key_mode=ctx.cfg.row_key_mode,
        nested_struct_types=ctx.cfg.nested_struct_types,
    )
    out_path = os.path.join(ctx.cfg.cpp_output_base_dir, cat_folder, header_name)
    return [GeneratedFile(path=out_path, content=cpp_code, policy="overwrite")]
