// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FModuleLogRegistrar.cpp
 * @brief 模块日志 RAII 自动注册守卫实现
 */

#include "Logging/FModuleLogRegistrar.h"

#include "Logging/FGameLogRegistry.h"

namespace Game::Log {

FModuleLogRegistrar::FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory) {
    SlotRecord = FGameLogRegistry::RegisterScopedCategory(InModuleName, InCategory, PreviousCategory);
    if (SlotRecord != nullptr) {
        Category = InCategory;
    }
}

FModuleLogRegistrar::~FModuleLogRegistrar() {
    if (SlotRecord != nullptr) {
        FGameLogRegistry::UnregisterScopedCategory(SlotRecord, Category, PreviousCategory);
    }
}

} // namespace Game::Log
