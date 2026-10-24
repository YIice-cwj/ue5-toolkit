// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "IGamePoolable.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class GAMECORE_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

/**
 * @brief 对象池可复用对象生命周期契约接口
 */
class GAMECORE_API IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出并激活时调用（重置状态、刷新生命周期）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GameCore|Pool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {}

    /**
     * @brief 归还至对象池休眠时调用（清理引用、重置特效/声音）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GameCore|Pool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {}
};

