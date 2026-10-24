#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
汇总头文件生成器 (aggregator_generator.py)

生成三类汇总头文件（每次覆盖）：
1. 分类汇总:   DataTables/{cat}/{cat}DataTableRowTypes.h
2. 全表主汇总: DataTables/DataTableRowTypes.h
3. 静态数据主汇总: Data/StaticData/StaticDataTypes.h
（分类汇总与主汇总在配置开关层面合并为单个 "aggregators" 产物）
"""

import os

from generators import GeneratedFile, GeneratorContext


def write_agg_header(banner: str, headers: list) -> str:
    """
    构建一个汇总头文件内容（pragma once + CoreMinimal.h + 一组 include）
    @param banner 文件头横幅注释
    @param headers include 列表
    @return 汇总头文件内容字符串
    """
    lines = [
        banner,
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
    ]
    for h in headers:
        lines.append(f'#include "{h}"')
    lines.append("")
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    生成全部汇总头文件
    @param ctx 生成器运行上下文
    @return 依次为：各分类汇总头、全表主汇总头、静态数据主汇总头（均 overwrite）
    """
    files = []

    for cat_folder, headers in ctx.category_headers.items():
        content = write_agg_header(
            f"// Automatically generated aggregator header for {cat_folder} DataTable rows.",
            headers,
        )
        out_path = os.path.join(ctx.cfg.cpp_output_base_dir, cat_folder, f"{cat_folder}DataTableRowTypes.h")
        files.append(GeneratedFile(path=out_path, content=content, policy="overwrite"))

    content = write_agg_header(
        "// Automatically generated master aggregator header for all DataTable row types.",
        [f"{cat_folder}/{cat_folder}DataTableRowTypes.h" for cat_folder in sorted(ctx.category_headers.keys())],
    )
    files.append(GeneratedFile(
        path=os.path.join(ctx.cfg.cpp_output_base_dir, "DataTableRowTypes.h"),
        content=content,
        policy="overwrite",
    ))

    static_agg_includes = []
    for cat_folder in sorted(ctx.category_tables.keys()):
        static_agg_includes.append(f"{cat_folder}/{cat_folder}CompositeData.h")
        static_agg_includes.append(f"{cat_folder}/{cat_folder}StaticData.h")
    content = write_agg_header(
        "// Automatically generated master aggregator header for all StaticData types.",
        static_agg_includes,
    )
    files.append(GeneratedFile(
        path=os.path.join(ctx.cfg.static_data_public_dir, "StaticDataTypes.h"),
        content=content,
        policy="overwrite",
    ))

    return files
