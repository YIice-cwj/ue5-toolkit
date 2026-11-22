// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file FGameLogRegistry.h
 * @brief 全局线程安全模块日志注册中心（基于常驻不可移动原子槽位实现零锁热路径分发）
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogCategory.h"

#include <atomic>

namespace Game::Log {

/**
 * @brief 模块日志分类常驻原子槽位类型（生命周期与进程一致，支持无锁读取与原子覆写）
 */
using FLogCategorySlot = std::atomic<FLogCategoryBase*>;

struct FModuleLogSlotRecord;

/**
 * @brief 模块日志注册中心（全局线程安全、SIOF 免疫、O(1) 哈希索引与无锁槽位热路径）
 */
class GAMELOGGING_API FGameLogRegistry final {
public:
    /**
     * @brief 纯静态单例工具类，禁止构造实例
     */
    FGameLogRegistry() = delete;

    /**
     * @brief 注册（或按模块名覆写）模块日志分类，并原子更新对应模块常驻槽位
     * @param InModuleName 模块名（区分大小写 ANSI 字符串，内部拷贝；为空忽略）
     * @param InCategory 日志分类指针（为空忽略）
     */
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);

    /**
     * @brief 注销指定模块的日志分类，并将其常驻槽位原子回退至兜底分类
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
     * @brief 解析或按需创建指定模块对应的常驻原子槽位指针（调用点仅需首次解析并永久缓存）
     * @param InModuleName 模块名（为空时返回恒定指向兜底分类的常驻槽位）
     * @return 永不为 nullptr 且进程期恒定有效的只读原子槽位指针
     */
    [[nodiscard]] static const FLogCategorySlot* ResolveCategorySlot(const char* InModuleName);

private:
    friend class FModuleLogRegistrar;

    /**
     * @brief 为 RAII 注册器登记分类并返回目标槽位记录与被覆盖的前一项分类
     * @param InModuleName 模块名（为空指针或空字符串时返回 nullptr）
     * @param InCategory 新日志分类指针（为空时返回 nullptr）
     * @param OutPreviousCategory 传出覆盖前已登记的分类指针；此前未登记时为 nullptr
     * @return 指向目标模块常驻槽位记录的指针；入参无效时返回 nullptr
     */
    [[nodiscard]] static FModuleLogSlotRecord* RegisterScopedCategory(
        const char* InModuleName,
        FLogCategoryBase* InCategory,
        FLogCategoryBase*& OutPreviousCategory
    );

    /**
     * @brief 注销仍归指定 RAII 实例所有的分类，并恢复其构造前的前一项分类或回退兜底分类
     * @param InSlotRecord 构造时取得的模块常驻槽位记录指针
     * @param InCategory 当前 RAII 实例持有的分类指针
     * @param InPreviousCategory 构造前分类指针；此前未登记时为 nullptr
     */
    static void UnregisterScopedCategory(
        FModuleLogSlotRecord* InSlotRecord,
        FLogCategoryBase* InCategory,
        FLogCategoryBase* InPreviousCategory
    );
};

} // namespace Game::Log
