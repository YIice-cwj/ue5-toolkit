#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
表查询骨架生成器 (query_generator.py)

为每张数据表生成 F{stem}Query 骨架头文件与实现文件（首次生成不覆盖）
取数入口统一收口到 FStaticDataSnapshot：调用方必须先持快照，行指针生命周期由快照引用计数兜底。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext, scaffold_banner
from naming_utils import to_field_name


def generate_query_scaffold_header(cfg: Any, cat_folder: str, item: dict) -> str:
    """生成单张表的查询骨架头文件（F{stem}Query，首次生成不覆盖）"""
    stem = item["stem"]
    struct_name = item["struct_name"]
    lines = [
        *scaffold_banner("query scaffold", stem),
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        f'#include "{cfg.row_types_include_prefix}/{cat_folder}/{item["header_name"]}"',
        '#include "Generated/Data/StaticData/FStaticDataSnapshot.h"',
        "",
        "/**",
        f" * @brief {item['desc']} 表级查询（纯静态方法，取数专用）",
        " * @note 此文件首次由生成脚本创建，后续重新生成数据表时【绝不会被覆盖】。",
        " *       开发者可在此扩展该表专属辅助查询（外键反查、字段过滤、范围查询等）。",
        " */",
        f"class {cfg.api_macro} F{stem}Query {{",
        "public:",
        f"    F{stem}Query() = delete;",
        "",
        "    /**",
        "     * @brief 主键查询（返回持快照所有权的行共享指针，可跨版本安全持有）",
        "     * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）",
        "     * @param InKey 唯一键",
        "     * @return 行共享指针（别名共享快照所有权），未命中或快照为空返回空",
        "     */",
        f"    static TSharedPtr<const {struct_name}> FindByKey(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);",
        "};",
        ""
    ]
    return "\n".join(lines)


def generate_query_scaffold_cpp(cfg: Any, cat_folder: str, item: dict) -> str:
    """生成单张表的查询骨架实现文件（F{stem}Query）"""
    stem = item["stem"]
    struct_name = item["struct_name"]
    map_name = f"{to_field_name(stem, cat_folder)}Map"
    lines = [
        *scaffold_banner("query implementation", stem),
        f'#include "{cfg.static_data_include_prefix}/{cat_folder}/{stem}Query.h"',
        "",
        "/**",
        " * @brief 主键查询（返回持快照所有权的行共享指针）",
        " * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）",
        " * @param InKey 唯一键",
        " * @return 行共享指针（别名共享快照所有权），未命中或快照为空返回空",
        " *",
        " * 实现思路：以别名共享指针把快照所有权挂到行指针上，调用方无需额外持有快照即可安全跨版本使用",
        " */",
        f"TSharedPtr<const {struct_name}> F{stem}Query::FindByKey(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey) {{",
        "    if (!InSnapshot.IsValid()) {",
        "        return nullptr;",
        "    }",
        f"    const {struct_name}* Row = InSnapshot->{cat_folder}Data.{map_name}.Find(InKey);",
        "    return Row != nullptr ? TSharedPtr<const " + struct_name + ">(InSnapshot, Row) : nullptr;",
        "}",
        ""
    ]
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    为全部分类的全部数据表生成 F{stem}Query 骨架
    @param ctx 生成器运行上下文
    @return 每张表的 Query.h（Public）与 Query.cpp（Private），均 preserve
    """
    files = []
    for cat_folder, tables in ctx.category_tables.items():
        for item in tables:
            files.append(GeneratedFile(
                path=os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{item['stem']}Query.h"),
                content=generate_query_scaffold_header(ctx.cfg, cat_folder, item),
                policy="preserve",
            ))
            files.append(GeneratedFile(
                path=os.path.join(ctx.cfg.static_data_private_dir, cat_folder, f"{item['stem']}Query.cpp"),
                content=generate_query_scaffold_cpp(ctx.cfg, cat_folder, item),
                policy="preserve",
            ))
    return files
