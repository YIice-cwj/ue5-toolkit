/**
 * @file AGamePoolableTestActor.h
 * @brief Actor 对象池单元测试专用受管 Actor 载体（单文件单类）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Core/IGamePoolable.h"
#include "GameFramework/Actor.h"

#include "AGamePoolableTestActor.generated.h"

/**
 * @brief 对象池单元测试专用 Actor（实现 IGamePoolable 回调计数与载荷注入验证）
 */
UCLASS()
class GAMEPOOL_API AGamePoolableTestActor : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：创建 SceneRoot 根组件（供池化生命周期与挂载测试使用）
     * @param ObjectInitializer 对象初始化器
     */
    explicit AGamePoolableTestActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
        : Super(ObjectInitializer) {
        RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    }

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
     * @brief 归还至对象池休眠回调
     */
    virtual void OnReleaseToPool_Implementation() override {
        ++ReleaseCount;
        if (ReleaseCallback) {
            ReleaseCallback(this);
        }
    }

    /**
     * @brief 带强类型指纹载荷的借出回调
     * @param InPayload 带类型指纹的载荷视图
     */
    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) override {
        ++PayloadAcquireCount;
        if (const int32* ValuePtr = IGamePoolable::ExtractPayload<int32>(InPayload)) {
            LastReceivedPayloadValue = *ValuePtr;
        }
        if (PayloadAcquireCallback) {
            PayloadAcquireCallback(this);
        }
    }

    int32 AcquireCount = 0;
    int32 ReleaseCount = 0;
    int32 PayloadAcquireCount = 0;
    int32 LastReceivedPayloadValue = 0;
    TFunction<void(AGamePoolableTestActor*)> AcquireCallback;
    TFunction<void(AGamePoolableTestActor*)> PayloadAcquireCallback;
    TFunction<void(AGamePoolableTestActor*)> ReleaseCallback;
};
