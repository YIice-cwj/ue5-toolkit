// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FModuleLogRegistrar.h
 * @brief 模块日志 RAII 自动注册守卫声明（零模块名重复拷贝，O(1) 析构恢复）
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogCategory.h"

namespace Game::Log {

struct FModuleLogSlotRecord;

/**
 * @brief 模块日志 RAII 自动注册器，严格嵌套作用域退出时恢复前一项登记
 *
 * 同一模块的多个实例必须按 LIFO 顺序销毁；不支持通过堆分配等方式进行非嵌套或乱序生命周期管理。
 */
class GAMELOGGING_API FModuleLogRegistrar final {
public:
    /**
     * @brief 立即注册模块日志分类并保存槽位记录与被覆盖的前一项分类
     * @param InModuleName 非空模块名；为空指针或空字符串时不注册
     * @param InCategory 日志分类指针；为空时不注册
     */
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 注销当前实例仍持有的登记，并恢复构造前的前一项分类或兜底分类
     */
    ~FModuleLogRegistrar();

    FModuleLogRegistrar(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar& operator=(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar(FModuleLogRegistrar&&) = delete;
    FModuleLogRegistrar& operator=(FModuleLogRegistrar&&) = delete;

private:
    FModuleLogSlotRecord* SlotRecord = nullptr;
    FLogCategoryBase* Category = nullptr;
    FLogCategoryBase* PreviousCategory = nullptr;
};

} // namespace Game::Log
