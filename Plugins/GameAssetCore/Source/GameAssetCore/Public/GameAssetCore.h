/**
 * @file GameAssetCore.h
 * @brief GameAssetCore 插件运行时模块入口与全量公共契约聚合门面头
 */

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

#include "Blueprint/AsyncAction_LoadGameAsset.h"
#include "Blueprint/AsyncAction_LoadGameAssetsBatch.h"
#include "Blueprint/UGameAssetBlueprintLibrary.h"
#include "Core/AssetErrorCodes.h"
#include "Core/GameAssetTypes.h"
#include "Loader/FGameAssetLoadHandle.h"
#include "Loader/FGameAssetLoader.h"
#include "Pinning/FGameAssetPinner.h"
#include "Pinning/FScopedPinnedAsset.h"

/**
 * @brief GameAssetCore 核心资产模块生命周期管理者
 */
class FGameAssetCoreModule : public IModuleInterface {
public:
    /**
     * @brief 模块启动初始化（注册常驻资产 GC 强引用收集器）
     */
    void StartupModule() override;

    /**
     * @brief 模块关闭与清理（自动释放全部全局常驻资产句柄并注销 GC 引用器）
     */
    void ShutdownModule() override;
};
