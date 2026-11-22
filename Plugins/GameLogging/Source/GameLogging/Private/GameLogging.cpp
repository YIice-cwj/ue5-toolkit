// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLogging.cpp
 * @brief GameLogging 模块入口实现与默认兜底分类注册
 */

#include "GameLogging.h"

#include "Modules/ModuleManager.h"

GAME_REGISTER_MODULE_CATEGORY(LogGame);

IMPLEMENT_MODULE(FDefaultModuleImpl, GameLogging);
