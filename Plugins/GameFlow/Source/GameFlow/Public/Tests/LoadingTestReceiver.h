// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "LoadingTestReceiver.generated.h"

/**
 * @brief 关卡流转子系统加载状态测试接收器
 */
UCLASS()
class GAMEFLOW_API ULoadingTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 加载状态变动回调：记录加载态与目标关卡
     * @param bInIsLoading 是否处于加载态
     * @param InTargetLevelName 目标关卡名称
     */
    UFUNCTION()
    void OnLoadingStateChanged(bool bInIsLoading, FName InTargetLevelName) {
        bLastIsLoading = bInIsLoading;
        LastTargetLevel = InTargetLevelName;
        ++LoadingChangeCount;
    }

    bool bLastIsLoading = false;
    FName LastTargetLevel = NAME_None;
    int32 LoadingChangeCount = 0;
};
