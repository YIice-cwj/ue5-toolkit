#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
RowStruct 行结构生成器 (row_struct_generator.py)

生成每张数据表的 RowStruct 头文件：
- 键模式 fname（默认）：不生成枚举，DataTable 行键直接使用 FName
- 键模式 enum（仅小表）：生成 E{stem}Key 唯一键枚举 + ToRowName / ToString 内联辅助函数
- USTRUCT F{stem}Row : public FTableRowBase
- 自动为全部 TSoftObjectPtr<T> / TSoftClassPtr<T> 提取前置声明，零硬编码具体引擎类名
"""

import os
import re
from typing import Any, List, Mapping, Optional, Sequence, Tuple

from cpp_type_utils import clean_cpp_type, get_default_init_expr, validate_field_type
from generators import GeneratedFile, GeneratorContext, doxygen_brief, escape_cpp_string
from naming_utils import to_property_name

KEY_MODE_FNAME = "fname"
KEY_MODE_ENUM = "enum"

_SOFT_PTR_CLASS_RE = re.compile(r"TSoft(?:Object|Class)Ptr\s*<\s*([A-Za-z_][A-Za-z0-9_]*)\s*>")


def table_names(stem: str, key_mode: str = KEY_MODE_FNAME) -> Tuple[str, str, str]:
    """
    由表词根推导 (header_name, struct_name, enum_name)，与 generate_struct_header 内部命名一致
    @param stem 表词根
    @param key_mode 键模式（fname / enum）
    @return (header_name, struct_name, enum_name)
    """
    enum_name = f"E{stem}Key" if key_mode == KEY_MODE_ENUM else ""
    return f"{stem}Row.h", f"F{stem}Row", enum_name


def _extract_soft_ptr_forward_decls(cpp_types: Sequence[str]) -> List[str]:
    """
    自动从列类型清单中提取全部 TSoftObjectPtr<T> / TSoftClassPtr<T> 指向类型的前置声明
    @param cpp_types 清洗后的 C++ 类型序列
    @return 排序去重后的前置声明列表（如 ["class UTexture2D;", "class UNiagaraSystem;"]）
    """
    class_names = set()
    for cpp_type in cpp_types:
        for match in _SOFT_PTR_CLASS_RE.finditer(cpp_type):
            class_names.add(match.group(1))
    return [f"class {name};" for name in sorted(class_names)]


def _build_enum_items(row_items: Sequence[Tuple[str, str]]) -> List[Tuple[str, str, str]]:
    """
    构建键枚举条目（清洗为合法 C++ 标识符；若清洗后碰撞则自动追加递增序号防止漏行）
    @param row_items [(原始键, 显示名), ...]
    @return [(清洗后唯一标识符, 原始键, 显示名), ...]
    """
    seen_ids = {"None"}
    seen_raw_keys = set()
    valid_items: List[Tuple[str, str, str]] = []
    for raw_key, raw_display in row_items:
        stripped_key = raw_key.strip()
        if not stripped_key or stripped_key in seen_raw_keys:
            continue
        seen_raw_keys.add(stripped_key)
        base_id = re.sub(r"[^a-zA-Z0-9_]", "_", stripped_key)
        if base_id and base_id[0].isdigit():
            base_id = "_" + base_id
        if not base_id:
            continue
        clean_id = base_id
        suffix = 2
        while clean_id in seen_ids:
            clean_id = f"{base_id}_{suffix}"
            suffix += 1
        seen_ids.add(clean_id)
        clean_display = escape_cpp_string(raw_display)
        valid_items.append((clean_id, escape_cpp_string(raw_key), clean_display))
    return valid_items


def generate_struct_header(
    xlsx_filename: str,
    stem: str,
    desc: str,
    headers: Sequence[Sequence[str]],
    row_items: Sequence[Tuple[str, str]],
    key_mode: str = KEY_MODE_FNAME,
    nested_struct_types: Optional[Mapping[str, Any]] = None,
    api_macro: str = "",
) -> Tuple[str, str, str, str]:
    """
    根据表头前三行与数据行键值生成标准 UE5 C++ RowStruct 头文件代码
    @param xlsx_filename 源 xlsx 文件名
    @param stem 表词根
    @param desc 表描述文本
    @param headers 表头三行契约
    @param row_items 数据行键值列表
    @param key_mode 键模式（fname / enum）
    @param nested_struct_types 嵌套 USTRUCT 类型映射
    @param api_macro 模块导出宏（如 GAMEDATASAMPLES_API）
    @return (header_name, struct_name, enum_name, cpp_code)
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

    validated_fields = []
    cleaned_col_types: List[str] = []
    for col_idx in range(1, len(field_names)):
        field_name = field_names[col_idx]
        if not field_name:
            continue
        chinese_name = chinese_names[col_idx] if col_idx < len(chinese_names) else field_name
        cpp_type = clean_cpp_type(cpp_types[col_idx] if col_idx < len(cpp_types) else "FString")
        validate_field_type(cpp_type, nested_struct_types, context=f"{xlsx_filename} 字段 [{field_name}]")
        prop_name = to_property_name(field_name, cpp_type)
        validated_fields.append((chinese_name, cpp_type, prop_name))
        cleaned_col_types.append(cpp_type)

    has_gameplay_tag = any("gameplaytag" in t.lower() for t in cleaned_col_types)
    forward_decls = _extract_soft_ptr_forward_decls(cleaned_col_types)
    used_nested_types = sorted({cpp_type for _, cpp_type, _ in validated_fields if cpp_type in nested_struct_types})

    lines = [
        "/**",
        f" * @file {header_name}",
        f" * @brief 数据表行数据结构（生成自 {xlsx_filename}）",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "Engine/DataTable.h"',
    ]
    if has_gameplay_tag:
        lines.append('#include "GameplayTagContainer.h"')
    for type_name in used_nested_types:
        lines.append(f'#include "{nested_struct_types[type_name]}"')
    lines.append(f'#include "{generated_header}"')
    lines.append("")

    if forward_decls:
        lines.extend(forward_decls)
        lines.append("")

    if key_mode == KEY_MODE_ENUM:
        valid_items = _build_enum_items(row_items)
        if len(valid_items) + 1 > 255:
            raise ValueError(
                f"{xlsx_filename}: 枚举键模式最多支持 255 个键（含 None），当前 {len(valid_items) + 1} 个；"
                f"请将 row_key_mode 设为 fname 以解除行数限制"
            )

        lines.extend([
            "/**",
            f" * @brief {desc} 唯一键枚举（消除魔术字符串，支持类型安全与自动补全）",
            " */",
            "UENUM(BlueprintType)",
            f"enum class {enum_name} : uint8 {{",
            '    None = 0 UMETA(DisplayName = "无"),',
        ])
        for clean_id, _raw_key, clean_display in valid_items:
            lines.append(f'    {clean_id} UMETA(DisplayName = "{clean_display}"),')
        lines.extend([
            "};",
            "",
            "/**",
            f" * @brief 将 {enum_name} 转换为 DataTable RowName",
            " * @param InKey 唯一键枚举值",
            " * @return 对应的 DataTable RowName",
            " */",
            f"inline FName ToRowName({enum_name} InKey) noexcept {{",
            "    switch (InKey) {",
        ])
        for clean_id, raw_key, _clean_display in valid_items:
            lines.append(f'        case {enum_name}::{clean_id}: return TEXT("{raw_key}");')
        lines.extend([
            "        default: return NAME_None;",
            "    }",
            "}",
            "",
            "/**",
            f" * @brief 将 {enum_name} 转换为字符串表示",
            " * @param InKey 唯一键枚举值",
            " * @return 静态字符串表示",
            " */",
            f"inline const TCHAR* ToString({enum_name} InKey) noexcept {{",
            "    switch (InKey) {",
        ])
        for clean_id, _raw_key, _clean_display in valid_items:
            lines.append(f'        case {enum_name}::{clean_id}: return TEXT("{clean_id}");')
        lines.extend([
            '        default: return TEXT("None");',
            "    }",
            "}",
            "",
        ])

    api_prefix = f"{api_macro.strip()} " if api_macro and api_macro.strip() else ""
    lines.extend([
        "/**",
        f" * @brief {desc}",
        " */",
        "USTRUCT(BlueprintType)",
        f"struct {api_prefix}{struct_name} : public FTableRowBase {{",
        "    GENERATED_BODY()",
        "",
        "public:",
    ])

    for chinese_name, cpp_type, prop_name in validated_fields:
        init_expr = get_default_init_expr(cpp_type)
        safe_display = escape_cpp_string(chinese_name)
        lines.extend(doxygen_brief(chinese_name))
        lines.append(f'    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{category}", meta = (DisplayName = "{safe_display}"))')
        lines.append(f"    {cpp_type} {prop_name}{init_expr}")
        lines.append("")

    lines.append("};")
    lines.append("")

    return header_name, struct_name, enum_name, "\n".join(lines)


def generate(
    ctx: GeneratorContext,
    xlsx_filename: str,
    stem: str,
    desc: str,
    headers: Sequence[Sequence[str]],
    row_items: Sequence[Tuple[str, str]],
    cat_folder: str,
) -> List[GeneratedFile]:
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
        api_macro=ctx.cfg.api_macro,
    )
    out_path = os.path.join(ctx.cfg.cpp_output_base_dir, cat_folder, header_name)
    return [GeneratedFile(path=out_path, content=cpp_code, policy="overwrite")]
