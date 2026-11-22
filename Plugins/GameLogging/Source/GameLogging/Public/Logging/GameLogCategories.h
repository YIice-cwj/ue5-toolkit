// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLogCategories.h
 * @brief 游戏全局通用与未注册模块兜底日志分类声明（同时透传核心日志宏）
 */

#pragma once

#include "CoreMinimal.h"
#include "Logging/LogCategory.h"

/**
 * @brief 全局通用与未注册模块兜底分类
 */
GAMELOGGING_API DECLARE_LOG_CATEGORY_EXTERN(LogGame, Log, All);

#include "Logging/GameLogMacros.h"
