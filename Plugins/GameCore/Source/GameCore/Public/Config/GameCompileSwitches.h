/**
 * @file GameCompileSwitches.h
 * @brief 集中编译开关（宏为唯一真源）+ constexpr 派生镜像，杜绝双源漂移
 *
 * 设计思路：
 * - 一级硬开关统一 GAME_ENABLE_* 前缀防宏命名污染，一律显式定义为整型 0/1，
 *   判式统一 `#if GAME_ENABLE_X != 0`，禁止空定义与 `#ifdef` 惯用法混用；
 * - namespace Game::Config 常量全部由宏派生（`(GAME_ENABLE_X != 0)`），严禁手工另写值；
 *   C++ 代码中的功能分支统一优先使用 `if constexpr (Game::Config::bX)` 进行控制；
 * - 参与 C++ 表达式的宏必须前置 `#ifndef` 默认定义（未定义宏在 `#if` 中按 0，在表达式中报错）；
 * - 本文件保持极小且禁止被公共头 include（仅 .cpp 与物理隔离点使用），控制重编译面。
 */

#pragma once

#include "CoreMinimal.h"

#ifndef GAME_ENABLE_TEST
    #define GAME_ENABLE_TEST 0
#endif

#ifndef GAME_ENABLE_GM
    #define GAME_ENABLE_GM (UE_BUILD_SHIPPING == 0)
#endif

#ifndef GAME_ENABLE_DEBUG_DRAW
    #define GAME_ENABLE_DEBUG_DRAW (UE_BUILD_SHIPPING == 0)
#endif

#ifndef GAME_ENABLE_LOG
    #define GAME_ENABLE_LOG ((NO_LOGGING) == 0)
#endif

namespace Game::Config {

inline constexpr bool bIsEditor         = (WITH_EDITOR != 0);
inline constexpr bool bEnableTest       = (GAME_ENABLE_TEST != 0);
inline constexpr bool bEnableGM         = (GAME_ENABLE_GM != 0);
inline constexpr bool bEnableDebugDraw  = (GAME_ENABLE_DEBUG_DRAW != 0);
inline constexpr bool bEnableLog        = (GAME_ENABLE_LOG != 0);

}  // namespace Game::Config
