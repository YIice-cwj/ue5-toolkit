// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogCategory.h"

namespace Game::Log {

/**
 * @brief 模块日志注册中心（全局线程安全）
 */
class GAMELOGGING_API FGameLogRegistry {
public:
    /**
     * @brief 注册（或按模块名覆写）模块日志分类，成功后全局纪元号 +1
     * @param InModuleName 模块名（内部拷贝，调用方临时串亦安全；为空忽略）
     * @param InCategory 日志分类指针（为空忽略）
     */
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 注销指定模块的日志分类，确有移除时全局纪元号 +1
     * @param InModuleName 模块名（为空忽略）
     */
    static void UnregisterCategory(const char* InModuleName);

    /**
     * @brief 按模块名查找日志分类（未注册或模块名为空时返回兜底分类）
     * @param InModuleName 模块名
     * @return 日志分类引用
     */
    [[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);

    /**
     * @brief 获取兜底日志分类（无日志构建下为局部 LogGame 实例，否则为全局 LogGame）
     * @return 兜底日志分类引用
     */
    [[nodiscard]] static FLogCategoryBase& GetFallbackCategory();

    /**
     * @brief 读取当前全局纪元号（任何注册 / 覆写 / 注销都会使其递增）
     * @return 纪元号
     */
    [[nodiscard]] static int32 GetEpoch() noexcept;
};

} // namespace Game::Log

