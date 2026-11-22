// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FModuleLogSlotRecord.h
 * @brief 模块日志常驻槽位内部记录结构声明（仅供 FGameLogRegistry 与 FModuleLogRegistrar 内部使用）
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/FGameLogRegistry.h"

namespace Game::Log {

/**
 * @brief 单一模块对应的常驻槽位记录（地址永不移动，支持调用点无锁原子读与 RAII O(1) 注销）
 */
struct FModuleLogSlotRecord final {
public:
    /**
     * @brief 构造并初始化为指定初始日志分类
     * @param InInitialCategory 初始日志分类指针
     */
    explicit FModuleLogSlotRecord(FLogCategoryBase* InInitialCategory) noexcept
        : Category(InInitialCategory) {
    }

public:
    FLogCategorySlot Category;
    bool bRegistered = false;
};

} // namespace Game::Log
