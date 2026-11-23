#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成式免反射行序列化器生成器 (serialization_generator.py)

为每张数据表的 RowStruct 产出逐行二进制负载读写函数（FArchive 直读直写，不经 UScriptStruct 反射），
并汇总为一个注册入口（结构名 -> 读写器），供 FStaticDataRepository 按结构名调度：
- 声明产物：Public/Generated/Data/StaticData/StaticDataSerialization.h
- 实现产物：Private/Generated/Data/StaticData/StaticDataSerialization.cpp
"""

import os
from typing import Any
import zlib

from cpp_type_utils import clean_cpp_type, payload_mode
from generators import GeneratedFile, GeneratorContext
from naming_utils import to_property_name

HEADER_NAME = "StaticDataSerialization.h"
SOURCE_NAME = "StaticDataSerialization.cpp"
TEXT_HELPER = "SerializeRowText"
SOFT_OBJECT_HELPER = "SerializeRowSoftObject"
SOFT_CLASS_HELPER = "SerializeRowSoftClass"


def compute_schema_hash(struct_name: str, fields: list) -> int:
    """
    计算行结构字段签名的确定性 CRC32 校验和（用于检测 .bin 与当前 C++ RowStruct 字段漂移）
    @param struct_name 行结构体名称
    @param fields [(属性名, C++类型), ...] 列表
    @return 32 位无符号整型校验和
    """
    signature = f"{struct_name}|" + "|".join(f"{fname}:{ftype}" for fname, ftype in fields)
    return zlib.crc32(signature.encode("utf-8")) & 0xFFFFFFFF


def get_register_func(cfg: Any) -> str:
    """
    获取序列化器注册函数名（配置驱动）
    @param cfg 运行配置
    @return 注册函数名
    """
    return getattr(cfg, "row_serializer_register_func", f"Register{getattr(cfg, 'project_prefix', 'Game')}StaticDataRowSerializers")


def extract_fields(headers: list) -> list:
    """
    依据表头契约提取 (字段名, 清洗后 C++ 类型) 列表（跳过首列 RowName 键与空字段名）
    @param headers 表头三行契约（中文名 / 英文名 / C++ 类型）
    @return 字段列表，顺序与 RowStruct 声明顺序一致
    """
    field_names = headers[1]
    cpp_types = headers[2]
    fields = []
    for col_idx in range(1, len(field_names)):
        field_name = field_names[col_idx]
        if not field_name:
            continue
        raw_type = cpp_types[col_idx] if col_idx < len(cpp_types) else "FString"
        cleaned_type = clean_cpp_type(raw_type)
        prop_name = to_property_name(field_name, cleaned_type)
        fields.append((prop_name, cleaned_type))
    return fields


def collect_tables(ctx: GeneratorContext) -> list:
    """
    汇总全部待序列化的表（按分类目录与头文件名排序，保证产物字节稳定）
    @param ctx 生成器运行上下文
    @return 表元信息列表，每项含 struct_name / header_include / fields
    """
    tables = []
    for cat_folder in sorted(ctx.category_tables.keys()):
        for item in ctx.category_tables[cat_folder]:
            tables.append({
                "struct_name": item["struct_name"],
                "header_include": f'{ctx.cfg.row_types_include_prefix}/{cat_folder}/{item["header_name"]}',
                "fields": item["fields"],
            })
    return tables


def generate_header(cfg: Any) -> str:
    """
    生成注册入口声明头（每次覆盖）
    @param cfg 运行配置
    @return 头文件代码文本
    """
    lines = [
        "/**",
        f" * @file {HEADER_NAME}",
        " * @brief 静态数据行序列化器",
        " */",
        "",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        '#include "StaticData/FStaticDataRepository.h"',
        "",
        "/**",
        " * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）",
        " * @param Repository 目标仓库（登记后方可按行结构类型 AddTable / ExtractTable）",
        " */",
        f"{cfg.api_macro} void {get_register_func(cfg)}(FStaticDataRepository& Repository);",
        "",
    ]
    return "\n".join(lines)


def generate_source(cfg: Any, tables: list) -> str:
    """
    生成逐行负载读写器与注册入口实现（每次覆盖）
    @param cfg 运行配置
    @param tables 待序列化表元信息列表
    @return 实现文件代码文本
    """
    nested_struct_types = getattr(cfg, "nested_struct_types", {}) or {}
    lines = [
        "/**",
        f" * @file {SOURCE_NAME}",
        " * @brief 静态数据行序列化器实现",
        " */",
        "",
        f'#include "{cfg.static_data_include_prefix}/{HEADER_NAME}"',
        "",
    ]

    all_fields = [(table, field) for table in tables for field in table["fields"]]
    has_text = any(cpp_type == "FText" for _, (_, cpp_type) in all_fields)
    has_tag = any("FGameplayTag" in cpp_type for _, (_, cpp_type) in all_fields)
    has_soft_object = any(cpp_type.startswith("TSoftObjectPtr<") for _, (_, cpp_type) in all_fields)
    has_soft_class = any(cpp_type.startswith("TSoftClassPtr<") for _, (_, cpp_type) in all_fields)
    used_types = {cpp_type for _, (_, cpp_type) in all_fields}
    has_nested_struct = any(type_name in used_types for type_name in nested_struct_types)

    extra_includes = []
    if has_text:
        extra_includes.append('#include "Internationalization/Text.h"')
    if has_tag:
        extra_includes.append('#include "GameplayTagContainer.h"')
    if has_soft_object or has_soft_class:
        extra_includes.append('#include "UObject/SoftObjectPtr.h"')
    if has_nested_struct:
        extra_includes.append('#include "Serialization/ObjectAndNameAsStringProxyArchive.h"')
    for type_name in sorted(nested_struct_types):
        if type_name in used_types:
            extra_includes.append(f'#include "{nested_struct_types[type_name]}"')
    if extra_includes:
        lines.extend(extra_includes)
        lines.append("")

    for table in tables:
        lines.append(f'#include "{table["header_include"]}"')
    lines.append("")

    lines.append("namespace {")
    lines.append("")
    if has_text:
        lines.extend([
            "/**",
            " * @brief FText 字段负载读写（通过 FTextStringHelper 完整保留 NSLOCTEXT 命名空间/键与普通显示文本）",
            " * @param Ar 归档（按 IsSaving 分流读写）",
            " * @param Text 待读写文本字段",
            " */",
            f"void {TEXT_HELPER}(FArchive& Ar, FText& Text) {{",
            "    FString TextBuffer;",
            "    if (Ar.IsSaving()) {",
            "        FTextStringHelper::WriteToBuffer(TextBuffer, Text);",
            "    }",
            "    Ar << TextBuffer;",
            "    if (Ar.IsLoading() && !Ar.IsError()) {",
            "        Text = FText::GetEmpty();",
            "        if (!TextBuffer.IsEmpty()) {",
            "            FTextStringHelper::ReadFromBuffer(*TextBuffer, Text);",
            "        }",
            "    }",
            "}",
            "",
        ])
    if has_soft_object:
        lines.extend([
            "/**",
            " * @brief TSoftObjectPtr 字段负载读写（负载为软对象路径字符串）",
            " * @tparam T 软对象指向类型",
            " * @param Ar 归档（按 IsSaving 分流读写）",
            " * @param Ptr 待读写软对象指针",
            " */",
            "template <typename T>",
            f"void {SOFT_OBJECT_HELPER}(FArchive& Ar, TSoftObjectPtr<T>& Ptr) {{",
            "    FString ObjectPathString = Ar.IsSaving() ? Ptr.ToSoftObjectPath().ToString() : FString();",
            "    Ar << ObjectPathString;",
            "    if (Ar.IsLoading() && !Ar.IsError()) {",
            "        Ptr = TSoftObjectPtr<T>(FSoftObjectPath(ObjectPathString));",
            "    }",
            "}",
            "",
        ])
    if has_soft_class:
        lines.extend([
            "/**",
            " * @brief TSoftClassPtr 字段负载读写（负载为软类路径字符串）",
            " * @tparam T 软类指向基类类型",
            " * @param Ar 归档（按 IsSaving 分流读写）",
            " * @param Ptr 待读写软类指针",
            " */",
            "template <typename T>",
            f"void {SOFT_CLASS_HELPER}(FArchive& Ar, TSoftClassPtr<T>& Ptr) {{",
            "    FString ClassPathString = Ar.IsSaving() ? Ptr.ToSoftObjectPath().ToString() : FString();",
            "    Ar << ClassPathString;",
            "    if (Ar.IsLoading() && !Ar.IsError()) {",
            "        Ptr = TSoftClassPtr<T>(FSoftObjectPath(ClassPathString));",
            "    }",
            "}",
            "",
        ])

    for table in tables:
        struct_name = table["struct_name"]
        schema_hash = compute_schema_hash(struct_name, table["fields"])
        table_has_struct = any(payload_mode(cpp_type, nested_struct_types) == "struct" for _, cpp_type in table["fields"])
        lines.extend([
            "/**",
            f" * @brief {struct_name} 行负载读写（生成式免反射，首部含字段结构签名校验，字段顺序即负载布局）",
            " * @param Ar 归档（按 IsSaving 分流读写）",
            " * @param RowPtr 行结构体指针",
            " * @return 归档无错且结构签名匹配返回 true",
            " */",
            f"bool Serialize{struct_name}(FArchive& Ar, void* RowPtr) {{",
            f"    {struct_name}& Row = *static_cast<{struct_name}*>(RowPtr);",
            f"    uint32 SchemaHash = 0x{schema_hash:08X}u;",
            "    Ar << SchemaHash;",
            f"    if (Ar.IsLoading() && (Ar.IsError() || SchemaHash != 0x{schema_hash:08X}u)) {{",
            "        Ar.SetError();",
            "        return false;",
            "    }",
        ])
        if table_has_struct:
            lines.append("    FObjectAndNameAsStringProxyArchive StructProxyAr(Ar, true);")
        for field_name, cpp_type in table["fields"]:
            mode = payload_mode(cpp_type, nested_struct_types)
            if mode == "text":
                lines.append(f"    {TEXT_HELPER}(Ar, Row.{field_name});")
            elif mode == "soft_object":
                lines.append(f"    {SOFT_OBJECT_HELPER}(Ar, Row.{field_name});")
            elif mode == "soft_class":
                lines.append(f"    {SOFT_CLASS_HELPER}(Ar, Row.{field_name});")
            elif mode == "struct":
                lines.append(f"    {cpp_type}::StaticStruct()->SerializeItem(StructProxyAr, &Row.{field_name}, nullptr);")
            else:
                lines.append(f"    Ar << Row.{field_name};")
        lines.append("    return !Ar.IsError();")
        lines.append("}")
        lines.append("")

    lines.extend([
        "}  // namespace",
        "",
        "/**",
        " * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）",
        " * @param Repository 目标仓库",
        " */",
        f"void {get_register_func(cfg)}(FStaticDataRepository& Repository) {{",
    ])
    for table in tables:
        struct_name = table["struct_name"]
        lines.append(f"    Repository.RegisterRowSerializer<{struct_name}>(&Serialize{struct_name});")
    lines.append("}")
    lines.append("")
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    生成全部生成式行序列化器产物
    @param ctx 生成器运行上下文
    @return 两件产物：注册入口声明头与实现源文件（overwrite）
    """
    tables = collect_tables(ctx)
    if not tables:
        return []
    return [
        GeneratedFile(path=os.path.join(ctx.cfg.static_data_public_dir, HEADER_NAME),
                      content=generate_header(ctx.cfg), policy="overwrite"),
        GeneratedFile(path=os.path.join(ctx.cfg.static_data_private_dir, SOURCE_NAME),
                      content=generate_source(ctx.cfg, tables), policy="overwrite"),
    ]
