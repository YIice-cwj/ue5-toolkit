// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Logging/LogCategory.h"

namespace Game::Log {

/**
 * @brief 模块日志注册中心（全局线程安全）
 */
class GAMELOGGING_API FGameLogRegistry {
public:
    /**
     * @brief 纯静态单例工具类，禁止构造实例
     */
    FGameLogRegistry() = delete;

    /**
     * @brief 注册（或按模块名覆写）模块日志分类，分类发生变更时全局纪元号 +1
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
     * @brief 读取当前全局纪元号（任何实际生效的注册、覆写或注销都会使其递增）
     * @return 纪元号
     */
    [[nodiscard]] static int32 GetEpoch() noexcept;

private:
    friend class FModuleLogRegistrar;

    /**
     * @brief 拷贝 ANSI 模块名（含结尾符），保证注册表与 RAII 实例自持名称内存
     * @param InName 源名称
     * @return 含结尾符的模块名字符数组
     */
    [[nodiscard]] static TArray<ANSICHAR> MakeAnsiName(const char* InName);

    /**
     * @brief 为 RAII 注册器登记分类并原子取得被覆盖的前一项分类
     * @param InModuleName 已校验的非空模块名
     * @param InCategory 已校验的新日志分类指针
     * @return 前一项分类指针；此前未注册时返回 nullptr
     */
    [[nodiscard]] static FLogCategoryBase* RegisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 注销仍归指定 RAII 实例所有的分类，并恢复其构造前的前一项分类
     * @param InModuleName 已注册的非空模块名
     * @param InCategory 当前 RAII 实例持有的分类指针
     * @param InPreviousCategory 构造前分类指针；此前未注册时为 nullptr
     */
    static void UnregisterScopedCategory(const char* InModuleName, FLogCategoryBase* InCategory,
        FLogCategoryBase* InPreviousCategory);
};

} // namespace Game::Log

