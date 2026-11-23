#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据快照/缓存层生成器 (static_data_snapshot_generator.py)

- FStaticDataSnapshot.h：数据版本号 + 各域复合缓存 + 各域行表（符合 15 层接口优先布局）
- FStaticDataCache.h/.cpp：各域 FVersionedCache 持有者 + 各域访问器（随快照生命周期只读，无冗余 Reset）
- FStaticDataSnapshotStore.h：薄类型别名，直接复用 GameDataCore::TStaticDataSnapshotStore<FStaticDataSnapshot>
"""

import os
from typing import Any

from generators import GeneratedFile, GeneratorContext, doxygen_brief
from generators.game_registration_generator import domains_of

SNAPSHOT_HEADER = "FStaticDataSnapshot.h"
CACHE_HEADER = "FStaticDataCache.h"
CACHE_SOURCE = "FStaticDataCache.cpp"
STORE_HEADER = "FStaticDataSnapshotStore.h"


def build_snapshot_header(cfg: Any, domain_list: list) -> str:
    """
    生成 FStaticDataSnapshot.h（版本号 + 各域复合缓存 + 各域行表）
    @param cfg 运行配置
    @param domain_list 域目录名列表（已排序）
    @return 头文件内容
    """
    cache_include = f"{cfg.static_data_include_prefix}/{CACHE_HEADER}"
    types_include = f"{cfg.static_data_include_prefix}/StaticDataTypes.h"
    lines = [
        "/**",
        f" * @file {SNAPSHOT_HEADER}",
        " * @brief 静态数据快照聚合载体",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        f'#include "{cache_include}"',
        f'#include "{types_include}"',
        "",
        "/**",
        " * @brief 静态数据快照（发布后只读；禁止拷贝与移动）",
        " */",
        f"class {cfg.api_macro} FStaticDataSnapshot {{",
        "public:",
        "    FStaticDataSnapshot() = default;",
        "    ~FStaticDataSnapshot() = default;",
        "    FStaticDataSnapshot(const FStaticDataSnapshot&) = delete;",
        "    FStaticDataSnapshot& operator=(const FStaticDataSnapshot&) = delete;",
        "    FStaticDataSnapshot(FStaticDataSnapshot&&) = delete;",
        "    FStaticDataSnapshot& operator=(FStaticDataSnapshot&&) = delete;",
        "",
        *doxygen_brief("数据版本号（与快照同代绑定，自 1 起递增）"),
        "    uint32 DataVersion = 0;",
        "",
        *doxygen_brief("各域复合聚合缓存（行 join 后的强类型视图）"),
        "    FStaticDataCache Cache;",
    ]
    for domain in domain_list:
        lines.append("")
        lines.extend(doxygen_brief(f"{domain} 域行表"))
        lines.append(f"    F{domain}StaticData {domain}Data;")
    lines.extend([
        "};",
        "",
    ])
    return "\n".join(lines)


def build_cache_header(cfg: Any, domain_list: list) -> str:
    """
    生成 FStaticDataCache.h（各域 FVersionedCache 持有者 + 访问器）
    @param cfg 运行配置
    @param domain_list 域目录名列表（已排序）
    @return 头文件内容
    """
    types_include = f"{cfg.static_data_include_prefix}/StaticDataTypes.h"
    lines = [
        "/**",
        f" * @file {CACHE_HEADER}",
        " * @brief 静态数据快照缓存容器类",
        " */",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        '#include "Cache/FVersionedCache.h"',
        f'#include "{types_include}"',
        "",
        "/**",
        " * @brief 各域聚合缓存容器（挂接于 FStaticDataSnapshot，随不可变快照生命周期）",
        " */",
        f"class {cfg.api_macro} FStaticDataCache {{",
        "public:",
        "    FStaticDataCache() = default;",
        "    ~FStaticDataCache() = default;",
        "    FStaticDataCache(const FStaticDataCache&) = delete;",
        "    FStaticDataCache& operator=(const FStaticDataCache&) = delete;",
        "    FStaticDataCache(FStaticDataCache&&) = delete;",
        "    FStaticDataCache& operator=(FStaticDataCache&&) = delete;",
    ]
    for domain in domain_list:
        lines.extend([
            "",
            "    /**",
            f"     * @brief 获取 {domain} 域聚合缓存",
            "     * @return 缓存引用（逻辑只读，const 快照上同样可查找）",
            "     */",
            f"    [[nodiscard]] FVersionedCache<F{domain}CompositeData>& {domain}Composites() const;",
        ])
    lines.extend([
        "",
        "private:",
    ])
    for domain in domain_list:
        lines.extend(doxygen_brief(f"{domain} 域逻辑只读聚合缓存（mutable：const 快照上允许查找与惰性填充）"))
        lines.append(f'    mutable FVersionedCache<F{domain}CompositeData> {domain}CompositesCache{{FName(TEXT("{domain}"))}};')
    lines.extend([
        "};",
        "",
    ])
    return "\n".join(lines)


def build_cache_source(cfg: Any, domain_list: list) -> str:
    """
    生成 FStaticDataCache.cpp（各域访问器实现）
    @param cfg 运行配置
    @param domain_list 域目录名列表（已排序）
    @return 实现文件内容
    """
    cache_include = f"{cfg.static_data_include_prefix}/{CACHE_HEADER}"
    lines = [
        "/**",
        f" * @file {CACHE_SOURCE}",
        " * @brief 静态数据快照缓存容器类实现",
        " */",
        f'#include "{cache_include}"',
    ]
    for domain in domain_list:
        lines.extend([
            "",
            "/**",
            f" * @brief 获取 {domain} 域聚合缓存",
            " * @return 缓存引用",
            " */",
            f"FVersionedCache<F{domain}CompositeData>& FStaticDataCache::{domain}Composites() const {{",
            f"    return {domain}CompositesCache;",
            "}",
        ])
    lines.append("")
    return "\n".join(lines)


def build_store_header(cfg: Any) -> str:
    """
    生成 FStaticDataSnapshotStore.h（薄类型别名，复用 GameDataCore::TStaticDataSnapshotStore）
    @param cfg 运行配置
    @return 头文件内容
    """
    snapshot_include = f"{cfg.static_data_include_prefix}/{SNAPSHOT_HEADER}"
    return "\n".join([
        "/**",
        f" * @file {STORE_HEADER}",
        " * @brief 静态数据快照 RCU 存储类型别名（复用 GameDataCore::TStaticDataSnapshotStore）",
        " */",
        "#pragma once",
        "",
        '#include "StaticData/TStaticDataSnapshotStore.h"',
        f'#include "{snapshot_include}"',
        "",
        "/**",
        " * @brief 静态数据快照 RCU 存储器（复用 GameDataCore 泛型模板 TStaticDataSnapshotStore）",
        " */",
        "using FStaticDataSnapshotStore = TStaticDataSnapshotStore<FStaticDataSnapshot>;",
        "",
    ])


def generate(ctx: GeneratorContext) -> list:
    """
    生成快照/缓存层全部 4 个产物（随域集合自动展开；overwrite 全量重写）
    @param ctx 生成器运行上下文
    @return GeneratedFile 列表
    """
    cfg = ctx.cfg
    domain_list = domains_of(ctx.category_tables)
    return [
        GeneratedFile(path=os.path.join(cfg.static_data_public_dir, SNAPSHOT_HEADER),
                      content=build_snapshot_header(cfg, domain_list), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.static_data_public_dir, CACHE_HEADER),
                      content=build_cache_header(cfg, domain_list), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.static_data_private_dir, CACHE_SOURCE),
                      content=build_cache_source(cfg, domain_list), policy="overwrite"),
        GeneratedFile(path=os.path.join(cfg.static_data_public_dir, STORE_HEADER),
                      content=build_store_header(cfg), policy="overwrite"),
    ]
