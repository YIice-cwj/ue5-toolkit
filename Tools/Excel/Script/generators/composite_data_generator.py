#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
单体聚合数据生成器 (composite_data_generator.py)

为每个分类生成 F{cat_folder}CompositeData 头文件（每次覆盖）
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext
from naming_utils import to_field_name


def generate_composite_data_header(cfg: Any, cat_folder: str, tables: list) -> str:
    """
    生成单体聚合数据头文件 (F{cat_folder}CompositeData)
    @param cfg 运行配置
    @param cat_folder 分类目录名
    @param tables 该分类的表元信息列表
    """
    generated_header = f"{cat_folder}CompositeData.generated.h"
    agg_header = f"{cfg.row_types_include_prefix}/{cat_folder}/{cat_folder}DataTableRowTypes.h"

    lines = []
    lines.append("/**")
    lines.append(f" * @file {cat_folder}CompositeData.h")
    lines.append(f" * @brief 复合数据容器")
    lines.append(" */")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "CoreMinimal.h"')
    lines.append("")
    lines.append(f'#include "{agg_header}"')
    lines.append(f'#include "{generated_header}"')
    lines.append("")

    lines.append("/**")
    lines.append(f" * @brief {cat_folder} 单体聚合数据（汇集该模块所有分表的单行数据）")
    lines.append(" */")
    lines.append("USTRUCT(BlueprintType)")
    lines.append(f"struct {cfg.api_macro} F{cat_folder}CompositeData {{")
    lines.append("    GENERATED_BODY()")
    lines.append("")
    lines.append("public:")

    for item in tables:
        stem = item["stem"]
        desc = item["desc"]
        struct_name = item["struct_name"]
        field_name = to_field_name(stem, cat_folder)
        lines.append(f'    /** {desc} */')
        lines.append(f'    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "{cat_folder}|Composite", meta = (DisplayName = "{desc}"))')
        lines.append(f'    {struct_name} {field_name};')
        lines.append("")

    lines.append("};")
    lines.append("")

    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    为全部分类生成 CompositeData 头文件
    @param ctx 生成器运行上下文
    @return 各分类 F{cat_folder}CompositeData.h（overwrite）
    """
    files = []
    for cat_folder, tables in ctx.category_tables.items():
        content = generate_composite_data_header(ctx.cfg, cat_folder, tables)
        out_path = os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{cat_folder}CompositeData.h")
        files.append(GeneratedFile(path=out_path, content=content, policy="overwrite"))
    return files
