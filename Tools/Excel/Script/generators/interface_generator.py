#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
业务接口脚手架生成器 (interface_generator.py)

为每个分类生成 F{cat_folder}StaticDataInterface 骨架头文件与实现文件。
脚手架采用 repair 策略：不存在或判定损坏（is_broken_or_empty_scaffold 语义）时生成/修复。
"""

import os

from typing import Any, Optional

from generators import GeneratedFile, GeneratorContext, scaffold_banner
from naming_utils import to_field_name


def select_primary_table(cat_folder: str, tables: list) -> Optional[dict]:
    """推导当前模块主表：优先 definition，再含核心特征词的表 (progression/info/base/config/main/param)，再按模块名，最后回退首表"""
    if not tables:
        return None
    for item in tables:
        f_name = to_field_name(item["stem"], cat_folder)
        if f_name == "definition":
            return item
    for item in tables:
        f_name = to_field_name(item["stem"], cat_folder)
        if "definition" in f_name:
            return item
    for item in tables:
        s_lower = item["stem"].lower()
        if any(k in s_lower for k in ("progression", "info", "base", "config", "main", "param")):
            return item
    for item in tables:
        if item["stem"].lower().startswith(cat_folder.lower()):
            return item
    return tables[0]


def generate_interface_scaffold_header(cfg: Any, cat_folder: str, tables: list) -> str:
    """生成初始接口类头文件脚手架（门面：FindCompositeData + WarmUpCompositeCache）"""
    primary = select_primary_table(cat_folder, tables)
    lines = [
        *scaffold_banner("interface scaffold", cat_folder),
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
    ]
    if primary:
        header_name = primary["header_name"]
        lines.append(f'#include "{cfg.row_types_include_prefix}/{cat_folder}/{header_name}"')
    lines.extend([
        f'#include "{cfg.static_data_include_prefix}/{cat_folder}/{cat_folder}StaticData.h"',
        f'#include "{cfg.static_data_include_prefix}/FStaticDataCache.h"',
        f'#include "{cfg.static_data_include_prefix}/FStaticDataSnapshot.h"',
        "",
        "/**",
        f" * @brief {cat_folder} 静态数据业务接口（门面：编排查询与服务）",
        " * @note 此文件首次由生成脚本创建，后续重新生成数据表时【绝不会被覆盖】。",
        " *       开发者可在此自由扩展该模块专属的高级业务查询与数据组装接口。",
        " */",
        f"class {cfg.api_macro} F{cat_folder}StaticDataInterface {{",
        "public:",
        f"    F{cat_folder}StaticDataInterface() = delete;",
        "",
        "    /**",
        "     * @brief 获取单体聚合数据（FName 键，快照持有语义，未命中返回空）",
        "     * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）",
        "     * @param InKey 唯一索引键",
        "     * @return 聚合数据快照（引用计数兜底，可跨版本持有），未命中或快照为空返回空",
        "     */",
        f"    static TSharedPtr<const F{cat_folder}CompositeData> FindCompositeData(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey);",
        "",
        "    /**",
        "     * @brief 预热聚合缓存（加载/热更提交后调用，按主表全量键预热）",
        "     * @param InCache 聚合缓存",
        "     * @param InData 域静态数据容器",
        "     */",
        f"    static void WarmUpCompositeCache(FStaticDataCache& InCache, const F{cat_folder}StaticData& InData);",
        "};",
        ""
    ])
    return "\n".join(lines)


def generate_interface_scaffold_cpp(cfg: Any, cat_folder: str, tables: list) -> str:
    """生成初始接口类实现文件脚手架（门面：FindCompositeData + WarmUpCompositeCache）"""
    primary = select_primary_table(cat_folder, tables)
    primary_field = to_field_name(primary["stem"], cat_folder) if primary else ""
    primary_map = f"{primary_field}Map" if primary else ""
    primary_row_type = primary["struct_name"] if primary else ""

    lines = [
        *scaffold_banner("interface implementation", cat_folder),
        f'#include "{cfg.static_data_include_prefix}/{cat_folder}/{cat_folder}StaticDataInterface.h"',
        "",
        f'#include "{cfg.static_data_include_prefix}/FStaticDataCache.h"',
        f'#include "{cfg.static_data_include_prefix}/FStaticDataSnapshot.h"',
        "",
        "/**",
        " * @brief 获取单体聚合数据（FName 键）",
        " * @param InSnapshot 静态数据快照（由 UStaticDataSubsystem::GetSnapshot 取得）",
        " * @param InKey 唯一索引键",
        " * @return 聚合数据快照（引用计数兜底，可跨版本持有），未命中或快照为空返回空",
        " *",
        " * 实现思路：直接查快照内该域聚合缓存（缓存由 WarmUpCompositeCache 在提交前预热）",
        " */",
        f"TSharedPtr<const F{cat_folder}CompositeData> F{cat_folder}StaticDataInterface::FindCompositeData(const TSharedPtr<const FStaticDataSnapshot>& InSnapshot, const FName& InKey) {{",
        "    if (!InSnapshot.IsValid()) {",
        "        return nullptr;",
        "    }",
        f"    return InSnapshot->Cache.{cat_folder}Composites().FindShared(InKey);",
        "}",
        "",
        "/**",
        " * @brief 预热聚合缓存",
        " * @param InCache 聚合缓存",
        " * @param InData 域静态数据容器",
        " *",
        " * 实现思路：收集主表全部键并逐键构建聚合数据；跨表引用完整性由数据校验管线负责，",
        " */",
        f"void F{cat_folder}StaticDataInterface::WarmUpCompositeCache(FStaticDataCache& InCache, const F{cat_folder}StaticData& InData) {{",
    ]
    if primary:
        lines.extend([
            "    TArray<FName> PrimaryKeys;",
            f"    InData.{primary_map}.GenerateKeyArray(PrimaryKeys);",
            f"    InCache.{cat_folder}Composites().Rebuild(MoveTemp(PrimaryKeys), [&InData](FName Key) -> F{cat_folder}CompositeData {{",
            f"        F{cat_folder}CompositeData Composite;",
            f"        if (const {primary_row_type}* Row = InData.{primary_map}.Find(Key)) {{",
            f"            Composite.{primary_field} = *Row;",
            "        }",
            "        return Composite;",
            "    });",
        ])
    lines.extend([
        "}",
        "",
    ])
    return "\n".join(lines)


def generate(ctx: GeneratorContext) -> list:
    """
    为全部分类生成 StaticDataInterface 骨架头文件与实现文件
    @param ctx 生成器运行上下文
    @return 每分类 2 个产物：头文件与实现文件（均 repair，携带当前有效 map 名集合）
    """
    files = []
    for cat_folder, tables in ctx.category_tables.items():
        valid_map_names = {f"{to_field_name(t['stem'], cat_folder)}Map" for t in tables}
        header_path = os.path.join(ctx.cfg.static_data_public_dir, cat_folder, f"{cat_folder}StaticDataInterface.h")
        cpp_path = os.path.join(ctx.cfg.static_data_private_dir, cat_folder, f"{cat_folder}StaticDataInterface.cpp")
        files.append(GeneratedFile(
            path=header_path,
            content=generate_interface_scaffold_header(ctx.cfg, cat_folder, tables),
            policy="repair",
            valid_map_names=valid_map_names,
        ))
        files.append(GeneratedFile(
            path=cpp_path,
            content=generate_interface_scaffold_cpp(ctx.cfg, cat_folder, tables),
            policy="repair",
            valid_map_names=valid_map_names,
        ))
    return files
