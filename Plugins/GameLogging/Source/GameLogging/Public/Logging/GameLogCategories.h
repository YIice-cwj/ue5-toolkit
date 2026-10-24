// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <atomic>

#include "Containers/Array.h"
#include "HAL/PlatformTime.h"
#include "HAL/PreprocessorHelpers.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"

#include "Config/GameCompileSwitches.h"

/**
 * @brief 全局通用与未注册模块兜底分类
 */
GAMELOGGING_API DECLARE_LOG_CATEGORY_EXTERN(LogGame, Log, All);

namespace Game::Log {

/**
 * @brief 模块日志注册中心（全局线程安全）
 */
class GAMELOGGING_API FGameLogRegistry {
public:
    static void RegisterCategory(const char* InModuleName, FLogCategoryBase* InCategory);

    static void UnregisterCategory(const char* InModuleName);

    [[nodiscard]] static FLogCategoryBase& GetCategory(const char* InModuleName);

    [[nodiscard]] static FLogCategoryBase& GetFallbackCategory();

    [[nodiscard]] static int32 GetEpoch() noexcept;
};

/**
 * @brief 模块日志 RAII 自动注册器（模块名自持拷贝，调用方临时串亦安全）
 */
class GAMELOGGING_API FModuleLogRegistrar {
public:
    FModuleLogRegistrar(const char* InModuleName, FLogCategoryBase* InCategory);
    ~FModuleLogRegistrar();
    FModuleLogRegistrar(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar& operator=(const FModuleLogRegistrar&) = delete;
    FModuleLogRegistrar(FModuleLogRegistrar&&) = delete;
    FModuleLogRegistrar& operator=(FModuleLogRegistrar&&) = delete;

private:
    TArray<ANSICHAR> ModuleName;
};

} // namespace Game::Log

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
    static const ::Game::Log::FModuleLogRegistrar UE_JOIN(GModuleLogReg_, __LINE__)(UE_MODULE_NAME, &CategoryName)

/**
 * @brief 注册已有或外部导出的日志分类至当前模块
 * @param CategoryRef 日志分类引用
 */
#define GAME_REGISTER_MODULE_CATEGORY(CategoryRef) \
    static const ::Game::Log::FModuleLogRegistrar UE_JOIN(GModuleLogReg_, __LINE__)(UE_MODULE_NAME, &(CategoryRef))

#endif

/**
 * @brief 自动按当前所属模块分发的通用日志宏
 *
 * 双轨分发架构：
 * 1. 编译期优先（Compile-time Priority）：若当前上下文已定义 GAME_CURRENT_MODULE_LOG，直接编译为原生 UE_LOG，零运行时损耗；
 * 2. 纪元安全动态分发（Epoch-safe Dynamic Dispatch）：未显式指定时，通过 UBT UE_MODULE_NAME 与局部原子指针缓存实现 O(1) 访问；
 *    全局纪元号（Epoch）在任何模块注册/覆写/注销时递增，缓存指针与纪元不匹配即重新解析，
 *    彻底解决静态初始化早期 Fallback 污染与注销后缓存残留两类失效问题。
 */
#ifdef GAME_CURRENT_MODULE_LOG
    #define GAME_LOG(Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                UE_LOG(GAME_CURRENT_MODULE_LOG, Verbosity, Format, ##__VA_ARGS__) \
            } \
        } while (false)
#else
    #define GAME_LOG(Verbosity, Format, ...) \
        do { \
            if constexpr (::Game::Config::bEnableLog) { \
                static std::atomic<::FLogCategoryBase*> UEPrivateCachedLogCat{nullptr}; \
                static std::atomic<int32> UEPrivateCachedEpoch{-1}; \
                const int32 CurrentEpoch = ::Game::Log::FGameLogRegistry::GetEpoch(); \
                const int32 CachedEpoch = UEPrivateCachedEpoch.load(std::memory_order_acquire); \
                ::FLogCategoryBase* LogCatPtr = UEPrivateCachedLogCat.load(std::memory_order_relaxed); \
                if (UNLIKELY(LogCatPtr == nullptr || CachedEpoch != CurrentEpoch)) { \
                    LogCatPtr = &::Game::Log::FGameLogRegistry::GetCategory(UE_MODULE_NAME); \
                    UEPrivateCachedLogCat.store(LogCatPtr, std::memory_order_relaxed); \
                    UEPrivateCachedEpoch.store(CurrentEpoch, std::memory_order_release); \
                } \
                UE_LOG_REF((*LogCatPtr), Verbosity, Format, ##__VA_ARGS__) \
            } \
        } while (false)
#endif

/**
 * @brief 条件日志宏：仅在 Condition 为真时执行日志输出
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
 * @brief 单次日志宏：在应用程序整个生命周期内该调用点仅输出一次（防循环刷屏）
 */
#define GAME_LOG_ONCE(Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<bool> bUEPrivateHasLogged{false}; \
            if (UNLIKELY(!bUEPrivateHasLogged.exchange(true, std::memory_order_relaxed))) { \
                GAME_LOG(Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 条件单次日志宏：仅在 Condition 首次为真时输出一次
 */
#define GAME_CLOG_ONCE(Condition, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            if (Condition) { \
                GAME_LOG_ONCE(Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 限频日志宏：限制该调用点最多每隔指定秒数输出一次
 * @param SecondsInterval 最小输出时间间隔（秒，如 1.0）
 */
#define GAME_LOG_THROTTLE(SecondsInterval, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<double> UEPrivateLastLogTime{-1.0e9}; \
            const double CurrentSeconds = FPlatformTime::Seconds(); \
            double LastSeconds = UEPrivateLastLogTime.load(std::memory_order_relaxed); \
            if (UNLIKELY(CurrentSeconds - LastSeconds >= (SecondsInterval))) { \
                if (UEPrivateLastLogTime.compare_exchange_strong(LastSeconds, CurrentSeconds, std::memory_order_relaxed)) { \
                    GAME_LOG(Verbosity, Format, ##__VA_ARGS__); \
                } \
            } \
        } \
    } while (false)