/**
 * @file UGamePoolableTestObject.h
 * @brief 非 Actor UObject 对象池单元测试专用受管对象
 */

#pragma once

#include "CoreMinimal.h"
#include "ObjectPool/IGamePoolable.h"
#include "UObject/Object.h"

#include "UGamePoolableTestObject.generated.h"

/**
 * @brief 对象池单元测试专用纯 UObject（实现 IGamePoolable 生命周期回调）
 */
UCLASS()
class GAMEPOOL_API UGamePoolableTestObject : public UObject, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出激活回调
     */
    void OnAcquireFromPool_Implementation() override {
        ++AcquireCount;
        if (AcquireCallback) {
            AcquireCallback(this);
        }
    }

    /**
     * @brief 归还回对象池休眠回调
     */
    void OnReleaseToPool_Implementation() override {
        ++ReleaseCount;
        if (ReleaseCallback) {
            ReleaseCallback(this);
        }
    }

    int32 AcquireCount = 0;
    int32 ReleaseCount = 0;
    int32 PayloadValue = 0;
    TFunction<void(UGamePoolableTestObject*)> AcquireCallback;
    TFunction<void(UGamePoolableTestObject*)> ReleaseCallback;
};
