#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
域服务骨架生成器 (service_generator.py)

为每个分类生成 F{cat_folder}Service 骨架头文件（首次生成不覆盖，强制无状态）
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext, scaffold_banner


def generate_service_scaffold_header(cfg: Any, cat_folder: str) -> str:
    """生成域服务骨架头文件（F{cat_folder}Service，首次生成不覆盖，强制无状态）"""
    lines = [
        *scaffold_banner("service scaffold", cat_folder),
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        "/**",
        f" * @brief {cat_folder} 业务服务（纯静态函数集合，无状态）",
        " * @note 此文件首次由生成脚本创建，后续重新生成数据表时【绝不会被覆盖】。",
        f" *       膨胀时按业务主题拆分（如 F{cat_folder}CombatService）；",
        " *       禁止直接触碰容器/TMap，一律经 F{表}Query 取数。",
        " */",
        f"class {cfg.api_macro} F{cat_folder}Service {{",
        "public:",
        f"    F{cat_folder}Service() = delete;",
        "",
        "    /** 开发者在此扩展业务计算（纯函数，便于单测） */",
        "};",
        ""
    ]
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    为全部分类生成 Service 骨架头文件
    @param ctx 生成器运行上下文
    @return 各分类 F{cat_folder}Service.h（preserve）
    """
    files = []
    for cat_folder in ctx.category_tables:
        content = generate_service_scaffold_header(ctx.cfg, cat_folder)
        out_path = os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{cat_folder}Service.h")
        files.append(GeneratedFile(path=out_path, content=content, policy="preserve"))
    return files
