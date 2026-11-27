/**
 * @file GameGameplayCoreLog.h
 * @brief GameGameplayCore 子域日志分类声明与 Unity Build 安全的编译期直轨日志宏
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/GameLogMacros.h"

/**
 * @brief 通用数值子系统专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameStat, Log, All);

/**
 * @brief 生命与存活状态机子系统专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameVital, Log, All);

/**
 * @brief 通用 Buff 运行时子系统专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameBuff, Log, All);

/**
 * @brief 指定子域分类的编译期直轨日志宏（受 Game::Config::bEnableLog 编译期开关门控，Unity Build 安全）
 * @param CategoryName 日志分类名（如 LogGameStat / LogGameVital / LogGameBuff）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字面量
 */
#define GAME_GAMEPLAY_LOG(CategoryName, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * @brief 指定子域分类的限频日志宏（抑制态零时钟/零 CAS 短路，Unity Build 安全）
 * @param CategoryName 日志分类名
 * @param SecondsInterval 最小输出间隔（秒）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字面量
 */
#define GAME_GAMEPLAY_LOG_THROTTLE(CategoryName, SecondsInterval, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<double> GameGameplayLogLastTime{-1.0e9}; \
            if (!::Game::Log::Private::IsCategorySuppressed(CategoryName, ::ELogVerbosity::Verbosity) \
                && UNLIKELY(::Game::Log::Private::TryConsumeThrottle(GameGameplayLogLastTime, (SecondsInterval)))) { \
                UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)
