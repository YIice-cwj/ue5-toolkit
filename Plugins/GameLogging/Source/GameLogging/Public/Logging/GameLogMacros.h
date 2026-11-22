// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLogMacros.h
 * @brief 模块感知核心日志宏与零开销编译期分发、去重与限频辅助通道
 */

#pragma once

#include "CoreMinimal.h"

#include <atomic>

#include "HAL/PlatformTime.h"
#include "HAL/PreprocessorHelpers.h"
#include "Logging/LogMacros.h"

#include "Config/GameCompileSwitches.h"
#include "Logging/FGameLogRegistry.h"
#include "Logging/FModuleLogRegistrar.h"

namespace Game::Log::Private {

/**
 * @brief 通过调用点静态常驻槽位缓存无锁解析当前所属模块的最新日志分类
 * @param InOutSlotCache 调用点静态缓存的模块原子槽位指针
 * @param InModuleName UBT 注入的当前模块名
 * @return 最新生效的日志分类引用
 */
[[nodiscard]] FORCEINLINE ::FLogCategoryBase& ResolveCategory(
    std::atomic<const FLogCategorySlot*>& InOutSlotCache,
    const char* InModuleName
) {
    const FLogCategorySlot* Slot = InOutSlotCache.load(std::memory_order_acquire);
    if (UNLIKELY(Slot == nullptr)) {
        Slot = FGameLogRegistry::ResolveCategorySlot(InModuleName);
        InOutSlotCache.store(Slot, std::memory_order_release);
    }
    return *Slot->load(std::memory_order_acquire);
}

/**
 * @brief 判断指定日志分类在给定详细级别下是否处于抑制状态
 * @param Category 日志分类引用
 * @param Verbosity 目标详细级别
 * @return 被抑制（不输出）返回 true
 */
[[nodiscard]] FORCEINLINE bool IsCategorySuppressed(
    const ::FLogCategoryBase& Category,
    ::ELogVerbosity::Type Verbosity
) noexcept {
    return Category.IsSuppressed(Verbosity);
}

#if NO_LOGGING
/**
 * @brief 无日志构建下的静态分类抑制判定重载（恒返回抑制）
 * @param Category 无日志空分类占位
 * @param Verbosity 目标详细级别
 * @return 恒返回 true
 */
[[nodiscard]] FORCEINLINE bool IsCategorySuppressed(
    const ::FNoLoggingCategory& Category,
    ::ELogVerbosity::Type Verbosity
) noexcept {
    (void)Category;
    (void)Verbosity;
    return true;
}
#endif

/**
 * @brief TTAS 无锁单次触发消费辅助函数
 * @param InOutHasLogged 调用点静态触发标记原子变量
 * @return 仅首次胜出线程返回 true，其余全部返回 false
 */
[[nodiscard]] FORCEINLINE bool TryConsumeOnce(std::atomic<bool>& InOutHasLogged) noexcept {
    if (LIKELY(InOutHasLogged.load(std::memory_order_relaxed))) {
        return false;
    }
    return !InOutHasLogged.exchange(true, std::memory_order_relaxed);
}

/**
 * @brief 基于显式时间戳的 CAS 无锁限频消费辅助函数（兼顾确定性单元测试）
 * @param InOutLastLogTime 调用点静态上次输出时间原子变量
 * @param CurrentSeconds 当前时间戳（秒）
 * @param IntervalSeconds 最小输出时间间隔（秒）
 * @return 本次允许输出返回 true
 */
[[nodiscard]] FORCEINLINE bool TryConsumeThrottleAt(
    std::atomic<double>& InOutLastLogTime,
    double CurrentSeconds,
    double IntervalSeconds
) noexcept {
    double LastSeconds = InOutLastLogTime.load(std::memory_order_relaxed);
    if (LIKELY(CurrentSeconds - LastSeconds < IntervalSeconds)) {
        return false;
    }
    return InOutLastLogTime.compare_exchange_strong(LastSeconds, CurrentSeconds, std::memory_order_relaxed);
}

/**
 * @brief 基于平台高精度时钟的 CAS 无锁限频消费辅助函数
 * @param InOutLastLogTime 调用点静态上次输出时间原子变量
 * @param IntervalSeconds 最小输出时间间隔（秒）
 * @return 本次允许输出返回 true
 */
[[nodiscard]] FORCEINLINE bool TryConsumeThrottle(
    std::atomic<double>& InOutLastLogTime,
    double IntervalSeconds
) noexcept {
    return TryConsumeThrottleAt(InOutLastLogTime, FPlatformTime::Seconds(), IntervalSeconds);
}

} // namespace Game::Log::Private

#if NO_LOGGING

/**
 * @brief 无日志构建下的模块日志注册（日志整体编译移除，无需分类符号与注册表条目）
 * @param CategoryName 日志分类名称
 */
#define GAME_REGISTER_MODULE_LOG(CategoryName)

/**
 * @brief 无日志构建下的分类注册（空实现）
 * @param CategoryRef 日志分类引用
 */
#define GAME_REGISTER_MODULE_CATEGORY(CategoryRef)

#else

/**
 * @brief 在模块最顶层（主 .cpp）声明并注册该模块专属日志分类
 * @param CategoryName 日志分类名称（如 LogGameData）
 */
#define GAME_REGISTER_MODULE_LOG(CategoryName) \
    DEFINE_LOG_CATEGORY_STATIC(CategoryName, Log, All); \
    static const ::Game::Log::FModuleLogRegistrar UE_JOIN(GModuleLogReg_, __COUNTER__)(UE_MODULE_NAME, &CategoryName)

/**
 * @brief 注册已有或外部导出的日志分类至当前模块
 * @param CategoryRef 日志分类引用
 */
#define GAME_REGISTER_MODULE_CATEGORY(CategoryRef) \
    static const ::Game::Log::FModuleLogRegistrar UE_JOIN(GModuleLogReg_, __COUNTER__)(UE_MODULE_NAME, &(CategoryRef))

#endif

/**
 * @brief 自动按当前所属模块分发的通用日志宏
 *
 * 双轨分发架构：
 * 1. 编译期直轨（Compile-time Priority）：若当前上下文已定义 GAME_CURRENT_MODULE_LOG，直接编译为原生 UE_LOG；
 * 2. 槽位直达动态分发（Slot-based Lock-free Dynamic Dispatch）：未显式指定时，调用点首次通过 UBT UE_MODULE_NAME
 *    取得该模块的进程常驻原子槽位指针并永久缓存；后续热路径零跨 DLL 调用、零锁竞争，且注册/覆写/注销即时无撕裂可见。
 * @param Verbosity 日志详细级别（如 Log, Warning, Error）
 * @param Format 格式化文本字符串
 */
#ifdef GAME_CURRENT_MODULE_LOG
    #define GAME_LOG(Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                UE_LOG(GAME_CURRENT_MODULE_LOG, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } while (false)
#else
    #define GAME_LOG(Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                static std::atomic<const ::Game::Log::FLogCategorySlot*> GameLogPrivateSlot{nullptr}; \
                ::FLogCategoryBase& GameLogPrivateCat = ::Game::Log::Private::ResolveCategory(GameLogPrivateSlot, UE_MODULE_NAME); \
                UE_LOG_REF(GameLogPrivateCat, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } while (false)
#endif

/**
 * @brief 条件日志宏：仅在 Condition 为真时执行日志输出
 * @param Condition 执行判断条件表达式
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字符串
 */
#define GAME_CLOG(Condition, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            if (Condition) { \
                GAME_LOG(Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 单次日志宏：在应用程序整个生命周期内该调用点仅输出首次日志（TTAS 无锁双重检测）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字符串
 */
#define GAME_LOG_ONCE(Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<bool> bGameLogPrivateHasLogged{false}; \
            if (UNLIKELY(::Game::Log::Private::TryConsumeOnce(bGameLogPrivateHasLogged))) { \
                GAME_LOG(Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 条件单次日志宏：仅在 Condition 首次为真时输出一次日志（TTAS 短路求值）
 * @param Condition 执行判断条件表达式
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字符串
 */
#define GAME_CLOG_ONCE(Condition, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<bool> bGameLogPrivateHasLogged{false}; \
            if (UNLIKELY(!bGameLogPrivateHasLogged.load(std::memory_order_relaxed)) \
                && (Condition) \
                && !bGameLogPrivateHasLogged.exchange(true, std::memory_order_relaxed)) { \
                GAME_LOG(Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 限频日志宏：限制该调用点最多每隔指定秒数输出一次（抑制级别前置短路，零时钟与零 CAS 开销）
 * @param SecondsInterval 最小输出时间间隔（秒，如 1.0）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字符串
 */
#ifdef GAME_CURRENT_MODULE_LOG
    #define GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                static std::atomic<double> GameLogPrivateLastTime{-1.0e9}; \
                if (!::Game::Log::Private::IsCategorySuppressed(GAME_CURRENT_MODULE_LOG, ::ELogVerbosity::Verbosity) \
                    && UNLIKELY(::Game::Log::Private::TryConsumeThrottle(GameLogPrivateLastTime, (SecondsInterval)))) { \
                    UE_LOG(GAME_CURRENT_MODULE_LOG, Verbosity, Format, ##__VA_ARGS__); \
                } \
            } \
        } while (false)
#else
    #define GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                static std::atomic<const ::Game::Log::FLogCategorySlot*> GameLogPrivateSlot{nullptr}; \
                static std::atomic<double> GameLogPrivateLastTime{-1.0e9}; \
                ::FLogCategoryBase& GameLogPrivateCat = ::Game::Log::Private::ResolveCategory(GameLogPrivateSlot, UE_MODULE_NAME); \
                if (!::Game::Log::Private::IsCategorySuppressed(GameLogPrivateCat, ::ELogVerbosity::Verbosity) \
                    && UNLIKELY(::Game::Log::Private::TryConsumeThrottle(GameLogPrivateLastTime, (SecondsInterval)))) { \
                    UE_LOG_REF(GameLogPrivateCat, Verbosity, Format, ##__VA_ARGS__); \
                } \
            } \
        } while (false)
#endif
