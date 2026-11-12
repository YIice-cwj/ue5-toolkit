// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "IGamePoolable.generated.h"

UINTERFACE(BlueprintType, Blueprintable)
class GAMEPOOL_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

/**
 * @brief 对象池可复用对象生命周期契约接口
 */
class GAMEPOOL_API IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出并激活时调用（重置状态、刷新生命周期）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {}

    /**
     * @brief 从对象池借出并携带业务载荷时调用（纯 C++ 零堆分配接口，默认转发至无参虚方法）
     * @param InPayload 业务载荷不可变指针
     */
    virtual void OnAcquireFromPoolWithPayload(const void* InPayload) {
        (void)InPayload;
        OnAcquireFromPool_Implementation();
    }

    /**
     * @brief 归还至对象池休眠时调用（清理引用、重置特效/声音）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {}
};
