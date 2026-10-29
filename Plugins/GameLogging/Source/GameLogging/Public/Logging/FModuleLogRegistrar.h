// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Logging/LogCategory.h"

namespace Game::Log {

/**
 * @brief 模块日志 RAII 自动注册器（模块名自持拷贝，调用方临时串亦安全）
 */
class GAMELOGGING_API FModuleLogRegistrar {
public:
    /**
     * @brief 构造：拷贝模块名并立即注册分类（任一参数为空则不注册）
     * @param InModuleName 模块名
     * @param InCategory 日志分类指针
     */
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 析构：按模块名注销分类
     */
    ~FModuleLogRegistrar();
    FModuleLogRegistrar(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar& operator=(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar(FModuleLogRegistrar&&) = delete;
    FModuleLogRegistrar& operator=(FModuleLogRegistrar&&) = delete;

private:
    TArray<ANSICHAR> ModuleName;
};

} // namespace Game::Log

