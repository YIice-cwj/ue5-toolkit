#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
静态数据快照/缓存层生成器 (static_data_snapshot_generator.py)

- FStaticDataSnapshot.h：数据版本号 + 各域复合缓存 + 各域行表
- FStaticDataCache.h/.cpp：各域 FVersionedCache 持有者 + 各域访问器 + Reset
- FStaticDataSnapshotStore.h/.cpp：域无关 RCU 存储（Acquire/GetVersion/Publish/TryBeginReload/EndReload）

接口导出宏取 cfg.api_macro（随配置变化）；域集合取自 category_tables 的键（排序）。
"""

import os

from typing import Any

from generators import GeneratedFile, GeneratorContext
from generators.game_registration_generator import domains_of

SNAPSHOT_HEADER = "FStaticDataSnapshot.h"
CACHE_HEADER = "FStaticDataCache.h"
CACHE_SOURCE = "FStaticDataCache.cpp"
STORE_HEADER = "FStaticDataSnapshotStore.h"
STORE_SOURCE = "FStaticDataSnapshotStore.cpp"

CACHE_HEADER_INCLUDE = "Generated/Data/StaticData/FStaticDataCache.h"
SNAPSHOT_HEADER_INCLUDE = "Generated/Data/StaticData/FStaticDataSnapshot.h"
STORE_HEADER_INCLUDE = "Generated/Data/StaticData/FStaticDataSnapshotStore.h"
TYPES_INCLUDE = "Generated/Data/StaticData/StaticDataTypes.h"


def build_snapshot_header(cfg: Any, domain_list: list) -> str:
    """
    生成 FStaticDataSnapshot.h（版本号 + 各域复合缓存 + 各域行表）
    @param cfg 运行配置
    @param domain_list 域目录名列表（已排序）
    @return 头文件内容
    """
    lines = [
        "// Automatically generated static data snapshot carrier. Do not edit manually.",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        f'#include "{CACHE_HEADER_INCLUDE}"',
        f'#include "{TYPES_INCLUDE}"',
        "",
        "/**",
        " * @brief 静态数据快照（发布后只读；禁止拷贝与移动）",
        " */",
        f"class {cfg.api_macro} FStaticDataSnapshot {{",
        "public:",
        "    /** 数据版本号（与快照同代绑定，自 1 起递增） */",
        "    uint32 DataVersion = 0;",
        "",
        "    /** 各域复合聚合缓存（行 join 后的强类型视图） */",
        "    FStaticDataCache Cache;",
    ]
    for domain in domain_list:
        lines.extend([
            "",
            f"    /** {domain} 域行表 */",
            f"    F{domain}StaticData {domain}Data;",
        ])
    lines.extend([
        "",
        "    FStaticDataSnapshot() = default;",
        "    ~FStaticDataSnapshot() = default;",
        "    FStaticDataSnapshot(const FStaticDataSnapshot&) = delete;",
        "    FStaticDataSnapshot& operator=(const FStaticDataSnapshot&) = delete;",
        "    FStaticDataSnapshot(FStaticDataSnapshot&&) = delete;",
        "    FStaticDataSnapshot& operator=(FStaticDataSnapshot&&) = delete;",
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
    lines = [
        "// Automatically generated static data cache holder. Do not edit manually.",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        "",
        f'#include "{TYPES_INCLUDE}"',
        '#include "Cache/FVersionedCache.h"',
        "",
        "/**",
        " * @brief 各域聚合缓存容器（挂接于 UStaticDataSubsystem，随其生命周期）",
        " */",
        f"class {cfg.api_macro} FStaticDataCache {{",
        "public:",
        "    FStaticDataCache() = default;",
        "    ~FStaticDataCache() = default;",
        "    FStaticDataCache(const FStaticDataCache&) = delete;",
        "    FStaticDataCache& operator=(const FStaticDataCache&) = delete;",
        "    FStaticDataCache(FStaticDataCache&&) = delete;",
        "    FStaticDataCache& operator=(FStaticDataCache&&) = delete;",
        "",
        "    /**",
        "     * @brief 清空全部域缓存（热更提交后调用，旧指针契约失效）",
        "     */",
        "    void Reset();",
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
        "    /** 各域逻辑只读缓存（mutable：const 快照上允许查找与缓存内部惰性填充） */",
    ])
    for domain in domain_list:
        lines.append(f'    mutable FVersionedCache<F{domain}CompositeData> {domain}CompositesCache{{FName(TEXT("{domain}"))}};')
    lines.extend([
        "};",
        "",
    ])
    return "\n".join(lines)


def build_cache_source(cfg: Any, domain_list: list) -> str:
    """
    生成 FStaticDataCache.cpp（Reset 与各域访问器实现）
    @param cfg 运行配置
    @param domain_list 域目录名列表（已排序）
    @return 实现文件内容
    """
    lines = [
        "// Automatically generated static data cache holder implementation. Do not edit manually.",
        f'#include "{CACHE_HEADER_INCLUDE}"',
        "",
        "void FStaticDataCache::Reset() {",
    ]
    for domain in domain_list:
        lines.append(f"    {domain}CompositesCache.Reset();")
    lines.append("}")
    for domain in domain_list:
        lines.extend([
            "",
            f"FVersionedCache<F{domain}CompositeData>& FStaticDataCache::{domain}Composites() const {{",
            f"    return {domain}CompositesCache;",
            "}",
        ])
    lines.append("")
    return "\n".join(lines)


def build_store_header(cfg: Any) -> str:
    """
    生成 FStaticDataSnapshotStore.h（域无关 RCU 存储声明）
    @param cfg 运行配置
    @return 头文件内容
    """
    return "\n".join([
        "// Automatically generated static data snapshot RCU store. Do not edit manually.",
        "#pragma once",
        "",
        '#include "CoreMinimal.h"',
        '#include "Misc/ScopeRWLock.h"',
        "",
        "#include <atomic>",
        "",
        f'#include "{SNAPSHOT_HEADER_INCLUDE}"',
        "",
        "/**",
        " * @brief 静态数据快照 RCU 存储（禁止拷贝与移动）",
        " */",
        f"class {cfg.api_macro} FStaticDataSnapshotStore {{",
        "public:",
        "    FStaticDataSnapshotStore() = default;",
        "    ~FStaticDataSnapshotStore() = default;",
        "    FStaticDataSnapshotStore(const FStaticDataSnapshotStore&) = delete;",
        "    FStaticDataSnapshotStore& operator=(const FStaticDataSnapshotStore&) = delete;",
        "    FStaticDataSnapshotStore(FStaticDataSnapshotStore&&) = delete;",
        "    FStaticDataSnapshotStore& operator=(FStaticDataSnapshotStore&&) = delete;",
        "",
        "    /**",
        "     * @brief 瞬态读锁拷贝在线快照指针后出锁",
        "     * @return 当前在线快照共享指针，尚未发布过返回空",
        "     */",
        "    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> Acquire() const;",
        "",
        "    /**",
        "     * @brief 取当前数据版本号（经 Acquire 读快照内字段）",
        "     * @return 版本号，尚未发布过返回 0",
        "     */",
        "    [[nodiscard]] uint32 GetVersion() const;",
        "",
        "    /**",
        "     * @brief 原子发布新快照",
        "     * @param NextSnapshot 新快照共享指针（所有权转移给存储）",
        "     */",
        "    void Publish(TSharedPtr<const FStaticDataSnapshot> NextSnapshot);",
        "",
        "    /**",
        "     * @brief CAS 抢占重载门闩",
        "     * @return 抢占成功返回 true，已有重载进行中返回 false",
        "     */",
        "    [[nodiscard]] bool TryBeginReload();",
        "",
        "    /**",
        "     * @brief 释放重载门闩",
        "     */",
        "    void EndReload();",
        "",
        "private:",
        "    mutable FRWLock SnapshotLock;",
        "    TSharedPtr<const FStaticDataSnapshot> OnlineSnapshot;",
        "    std::atomic<bool> bReloadInProgress{false};",
        "};",
        "",
    ])


def build_store_source(cfg: Any) -> str:
    """
    生成 FStaticDataSnapshotStore.cpp（RCU 存储实现）
    @param cfg 运行配置
    @return 实现文件内容
    """
    return "\n".join([
        "// Automatically generated static data snapshot RCU store implementation. Do not edit manually.",
        f'#include "{STORE_HEADER_INCLUDE}"',
        "",
        f'#include "{SNAPSHOT_HEADER_INCLUDE}"',
        "",
        "/**",
        " * @brief 瞬态读锁拷贝在线快照指针后出锁",
        " * @return 当前在线快照共享指针，尚未发布过返回空",
        " */",
        "TSharedPtr<const FStaticDataSnapshot> FStaticDataSnapshotStore::Acquire() const {",
        "    FReadScopeLock ReadLock(SnapshotLock);",
        "    return OnlineSnapshot;",
        "}",
        "",
        "/**",
        " * @brief 取当前数据版本号（经 Acquire 读快照内字段）",
        " * @return 版本号，尚未发布过返回 0",
        " */",
        "uint32 FStaticDataSnapshotStore::GetVersion() const {",
        "    const TSharedPtr<const FStaticDataSnapshot> Snapshot = Acquire();",
        "    return Snapshot.IsValid() ? Snapshot->DataVersion : 0;",
        "}",
        "",
        "/**",
        " * @brief 原子发布新快照",
        " * @param NextSnapshot 新快照共享指针（所有权转移给存储）",
        " *",
        " * 实现思路：写锁内仅两次指针搬移，旧快照先腾挪到局部变量，出锁后随局部变量自然析构",
        " */",
        "void FStaticDataSnapshotStore::Publish(TSharedPtr<const FStaticDataSnapshot> NextSnapshot) {",
        "    TSharedPtr<const FStaticDataSnapshot> RetiredSnapshot;",
        "    {",
        "        FWriteScopeLock WriteLock(SnapshotLock);",
        "        RetiredSnapshot = MoveTemp(OnlineSnapshot);",
        "        OnlineSnapshot = MoveTemp(NextSnapshot);",
        "    }",
        "}",
        "",
        "/**",
        " * @brief CAS 抢占重载门闩",
        " * @return 抢占成功返回 true，已有重载进行中返回 false",
        " */",
        "bool FStaticDataSnapshotStore::TryBeginReload() {",
        "    bool bExpected = false;",
        "    return bReloadInProgress.compare_exchange_strong(bExpected, true);",
        "}",
        "",
        "/**",
        " * @brief 释放重载门闩",
        " */",
        "void FStaticDataSnapshotStore::EndReload() {",
        "    bReloadInProgress.store(false);",
        "}",
        "",
    ])


def generate(ctx: GeneratorContext) -> list:
    """
    生成快照/缓存层全部 5 个产物（随域集合自动展开；overwrite 全量重写）
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
        GeneratedFile(path=os.path.join(cfg.static_data_private_dir, STORE_SOURCE),
                      content=build_store_source(cfg), policy="overwrite"),
    ]
