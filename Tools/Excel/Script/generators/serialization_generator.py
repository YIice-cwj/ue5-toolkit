#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成式免反射行序列化器生成器 (serialization_generator.py)

为每张数据表的 RowStruct 产出逐行二进制负载读写函数（FArchive 直读直写，不经 UScriptStruct 反射），
并汇总为一个注册入口（结构名 -> 读写器），供 FStaticDataRepository 按结构名调度：
- 声明产物：Public/Generated/Data/StaticData/StaticDataSerialization.h
- 实现产物：Private/Generated/Data/StaticData/StaticDataSerialization.cpp

负载布局规则：字段顺序与 RowStruct 声明顺序（表头列序）严格一致；
FText 字段经显示字符串往返（与 JSON 源同语义），TSoftObjectPtr 字段经软对象路径字符串往返
（普通 FArchive 不支持对象引用序列化），其余字段直接走 FArchive operator<<；
出现未纳入支持清单的字段类型时直接报错中止管线（宁可生成失败，不可产出布局漂移的负载）。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext
from cpp_type_utils import clean_cpp_type, payload_mode
from naming_utils import to_property_name

HEADER_NAME = "StaticDataSerialization.h"
SOURCE_NAME = "StaticDataSerialization.cpp"
TEXT_HELPER = "SerializeRowText"
SOFT_OBJECT_HELPER = "SerializeRowSoftObject"


def get_register_func(cfg: Any) -> str:
    """获取序列化器注册函数名（配置驱动）"""
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
    """生成注册入口声明头（每次覆盖）"""
    lines = []
    lines.append("// Automatically generated static data row serializers. Do not edit manually.")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "CoreMinimal.h"')
    lines.append("")
    lines.append('#include "StaticData/FStaticDataRepository.h"')
    lines.append("")
    lines.append("/**")
    lines.append(" * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）")
    lines.append(" * @param Repository 目标仓库（登记后方可按行结构类型 AddTable / ExtractTable）")
    lines.append(" */")
    lines.append(f"{cfg.api_macro} void {get_register_func(cfg)}(FStaticDataRepository& Repository);")
    lines.append("")
    return "\n".join(lines)


def generate_source(cfg: Any, tables: list) -> str:
    """生成逐行负载读写器与注册入口实现（每次覆盖）"""
    nested_struct_types = getattr(cfg, "nested_struct_types", {}) or {}
    lines = []
    lines.append("// Automatically generated static data row serializers. Do not edit manually.")
    lines.append(f'#include "{cfg.static_data_include_prefix}/{HEADER_NAME}"')
    lines.append("")

    extra_includes = []
    all_fields = [(table, field) for table in tables for field in table["fields"]]
    if any(cpp_type == "FText" for _, (_, cpp_type) in all_fields):
        extra_includes.append('#include "Internationalization/Text.h"')
    if any("FGameplayTag" in cpp_type for _, (_, cpp_type) in all_fields):
        extra_includes.append('#include "GameplayTagContainer.h"')
    if any("TSoftObjectPtr<" in cpp_type for _, (_, cpp_type) in all_fields):
        extra_includes.append('#include "UObject/SoftObjectPtr.h"')
    used_types = {cpp_type for _, (_, cpp_type) in all_fields}
    for type_name in sorted(nested_struct_types):
        if type_name in used_types:
            extra_includes.append(f'#include "{nested_struct_types[type_name]}"')
    for include_line in extra_includes:
        lines.append(include_line)
    if extra_includes:
        lines.append("")

    for table in tables:
        lines.append(f'#include "{table["header_include"]}"')
    lines.append("")

    lines.append("namespace {")
    lines.append("")
    if any(cpp_type == "FText" for _, (_, cpp_type) in all_fields):
        lines.append("/**")
        lines.append(" * @brief FText 字段负载读写（负载为显示字符串，与 JSON 源同语义）")
        lines.append(" * @param Ar 归档（按 IsSaving 分流读写）")
        lines.append(" * @param Text 待读写文本字段")
        lines.append(" * @return 归档无错返回 true")
        lines.append(" */")
        lines.append(f"bool {TEXT_HELPER}(FArchive& Ar, FText& Text) {{")
        lines.append("    FString TextString = Ar.IsSaving() ? Text.ToString() : FString();")
        lines.append("    Ar << TextString;")
        lines.append("    if (Ar.IsLoading()) {")
        lines.append("        Text = FText::FromString(TextString);")
        lines.append("    }")
        lines.append("    return !Ar.IsError();")
        lines.append("}")
        lines.append("")
    if any(cpp_type.startswith("TSoftObjectPtr<") for _, (_, cpp_type) in all_fields):
        lines.append("/**")
        lines.append(" * @brief TSoftObjectPtr 字段负载读写（负载为软对象路径字符串，普通 FArchive 不支持对象引用序列化）")
        lines.append(" * @param Ar 归档（按 IsSaving 分流读写）")
        lines.append(" * @param Ptr 待读写软对象指针")
        lines.append(" * @return 归档无错返回 true")
        lines.append(" */")
        lines.append("template<typename T>")
        lines.append(f"bool {SOFT_OBJECT_HELPER}(FArchive& Ar, TSoftObjectPtr<T>& Ptr) {{")
        lines.append("    FString ObjectPathString = Ar.IsSaving() ? Ptr.ToSoftObjectPath().ToString() : FString();")
        lines.append("    Ar << ObjectPathString;")
        lines.append("    if (Ar.IsLoading()) {")
        lines.append("        Ptr = TSoftObjectPtr<T>(FSoftObjectPath(ObjectPathString));")
        lines.append("    }")
        lines.append("    return !Ar.IsError();")
        lines.append("}")
        lines.append("")
    for table in tables:
        struct_name = table["struct_name"]
        lines.append("/**")
        lines.append(f" * @brief {struct_name} 行负载读写（生成式免反射，字段顺序即负载布局）")
        lines.append(" * @param Ar 归档（按 IsSaving 分流读写）")
        lines.append(" * @param RowPtr 行结构体指针")
        lines.append(" * @return 归档无错返回 true")
        lines.append(" */")
        lines.append(f"bool Serialize{struct_name}(FArchive& Ar, void* RowPtr) {{")
        lines.append(f"    {struct_name}& Row = *static_cast<{struct_name}*>(RowPtr);")
        for field_name, cpp_type in table["fields"]:
            mode = payload_mode(cpp_type, nested_struct_types)
            if mode == "text":
                lines.append(f"    {TEXT_HELPER}(Ar, Row.{field_name});")
            elif mode == "soft_object":
                lines.append(f"    {SOFT_OBJECT_HELPER}(Ar, Row.{field_name});")
            elif mode == "struct":
                lines.append(f"    {cpp_type}::StaticStruct()->SerializeItem(Ar, &Row.{field_name}, nullptr);")
            else:
                lines.append(f"    Ar << Row.{field_name};")
        lines.append("    return !Ar.IsError();")
        lines.append("}")
        lines.append("")
    lines.append("}  // namespace")
    lines.append("")

    lines.append("/**")
    lines.append(" * @brief 注册全部生成式行负载读写器（免反射，供 FStaticDataRepository 按结构名调度）")
    lines.append(" * @param Repository 目标仓库")
    lines.append(" *")
    lines.append(" * 实现思路：逐表把「结构名 -> 读写器」登记入仓库；结构名直接取 RowStruct 的 StaticStruct 名，")
    lines.append(" * 与 FStaticDataRepository 模板入口的 RowType::StaticStruct()->GetName() 同源，避免名称字面量漂移。")
    lines.append(" */")
    lines.append(f"void {get_register_func(cfg)}(FStaticDataRepository& Repository) {{")
    for table in tables:
        struct_name = table["struct_name"]
        lines.append(f"    Repository.RegisterRowSerializer(FName({struct_name}::StaticStruct()->GetName()), &Serialize{struct_name});")
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
