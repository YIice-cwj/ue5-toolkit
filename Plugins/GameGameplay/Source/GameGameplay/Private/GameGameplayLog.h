/**
 * @file GameGameplayLog.h
 * @brief GameGameplay 子域日志分类声明与 Unity Build 安全的编译期直轨日志宏
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/GameLogMacros.h"

/**
 * @brief 能力、ASC 与引导子域专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameAbility, Log, All);

/**
 * @brief 属性集与属性映射子域专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameAttribute, Log, All);

/**
 * @brief 伤害结算、通道注册表与打击上下文子域专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameDamage, Log, All);

/**
 * @brief GAS Buff 与打击感 Cue 桥接子域专属日志分类
 */
DECLARE_LOG_CATEGORY_EXTERN(LogGameGasBridge, Log, All);

/**
 * @brief 指定子域分类的编译期直轨日志宏（受 Game::Config::bEnableLog 编译期门控，Unity Build 安全）
 * @param CategoryName 日志分类名（如 LogGameAbility / LogGameAttribute / LogGameDamage / LogGameGasBridge）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字面量
 */
#define GAME_GAS_LOG(CategoryName, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
        } \
    } while (false)

/**
 * @brief 指定子域分类的条件日志宏（受 Game::Config::bEnableLog 编译期门控，Unity Build 安全）
 * @param CategoryName 日志分类名
 * @param Condition 触发输出的条件表达式
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字面量
 */
#define GAME_GAS_CLOG(CategoryName, Condition, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            if (Condition) { \
                UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)

/**
 * @brief 指定子域分类的限频日志宏（抑制态零时钟/零 CAS 短路，Unity Build 安全）
 * @param CategoryName 日志分类名
 * @param SecondsInterval 最小输出间隔（秒）
 * @param Verbosity 日志详细级别
 * @param Format 格式化文本字面量
 */
#define GAME_GAS_LOG_THROTTLE(CategoryName, SecondsInterval, Verbosity, Format, ...) \
    do { \
        if constexpr (::Game::Config::bEnableLog) { \
            static std::atomic<double> GameGasLogLastTime{-1.0e9}; \
            if (!::Game::Log::Private::IsCategorySuppressed(CategoryName, ::ELogVerbosity::Verbosity) \
                && UNLIKELY(::Game::Log::Private::TryConsumeThrottle(GameGasLogLastTime, (SecondsInterval)))) { \
                UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
            } \
        } \
    } while (false)
