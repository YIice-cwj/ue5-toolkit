// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Logging/LogCategory.h"

namespace Game::Log {

/**
 * @brief 模块日志 RAII 自动注册器，严格嵌套作用域退出时恢复前一项登记
 *
 * 同一模块的多个实例必须按 LIFO 顺序销毁；不支持通过堆分配等方式进行非嵌套或乱序生命周期管理。
 */
class GAMELOGGING_API FModuleLogRegistrar {
public:
    /**
     * @brief 拷贝模块名并立即注册分类，同时保存被覆盖的前一项分类
     * @param InModuleName 非空模块名；为空指针或空字符串时不注册
     * @param InCategory 日志分类指针；为空时不注册
     */
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 注销当前实例仍持有的登记，并恢复构造前的前一项分类
     */
    ~FModuleLogRegistrar();

    /**
     * @brief 禁止拷贝构造，避免重复注销同一登记
     */
    FModuleLogRegistrar(const FModuleLogRegistrar&) = delete;

    /**
     * @brief 禁止拷贝赋值，避免重复注销同一登记
     * @return 禁止调用
     */
    FModuleLogRegistrar& operator=(const FModuleLogRegistrar&) = delete;

    /**
     * @brief 禁止移动构造，保持登记所有权唯一
     */
    FModuleLogRegistrar(FModuleLogRegistrar&&) = delete;

    /**
     * @brief 禁止移动赋值，保持登记所有权唯一
     * @return 禁止调用
     */
    FModuleLogRegistrar& operator=(FModuleLogRegistrar&&) = delete;

private:
    TArray<ANSICHAR> ModuleName;
    FLogCategoryBase* Category = nullptr;
    FLogCategoryBase* PreviousCategory = nullptr;
};

} // namespace Game::Log

