/**
 * @file UGamePoolableTestObject.h
 * @brief 非 Actor UObject 对象池单元测试专用受管对象载体（单文件单类）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/IGamePoolable.h"
#include "UObject/Object.h"

#include "UGamePoolableTestObject.generated.h"

/**
 * @brief 对象池单元测试专用纯 UObject（实现 IGamePoolable 生命周期回调与载荷验证）
 */
UCLASS()
class GAMEPOOL_API UGamePoolableTestObject : public UObject, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出激活回调
     */
    virtual void OnAcquireFromPool_Implementation() override {
        ++AcquireCount;
        if (AcquireCallback) {
            AcquireCallback(this);
        }
    }

    /**
     * @brief 归还回对象池休眠回调
     */
    virtual void OnReleaseToPool_Implementation() override {
        ++ReleaseCount;
        if (ReleaseCallback) {
            ReleaseCallback(this);
        }
    }

    /**
     * @brief 借出携带业务载荷回调
     * @param InPayload 带类型指纹的业务载荷视图
     */
    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) override {
        ++PayloadAcquireCount;
        if (const int32* ValuePtr = IGamePoolable::ExtractPayload<int32>(InPayload)) {
            PayloadValue = *ValuePtr;
        }
        if (PayloadAcquireCallback) {
            PayloadAcquireCallback(this);
        }
    }

    int32 AcquireCount = 0;
    int32 ReleaseCount = 0;
    int32 PayloadAcquireCount = 0;
    int32 PayloadValue = 0;
    TFunction<void(UGamePoolableTestObject*)> AcquireCallback;
    TFunction<void(UGamePoolableTestObject*)> PayloadAcquireCallback;
    TFunction<void(UGamePoolableTestObject*)> ReleaseCallback;
};
