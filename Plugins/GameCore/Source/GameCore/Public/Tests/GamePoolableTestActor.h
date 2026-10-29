// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "Pool/IGamePoolable.h"
#include "Tests/GameMessageTestReceiver.h"

#include "GamePoolableTestActor.generated.h"

/**
 * @brief 对象池单元测试专用 Actor（实现 IGamePoolable 回调计数）
 */
UCLASS()
class GAMECORE_API AGamePoolableTestActor : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：创建 SceneRoot 根组件（供池化生命周期演示挂载）
     * @param ObjectInitializer 对象初始化器
     */
    explicit AGamePoolableTestActor(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get())
        : Super(ObjectInitializer) {
        RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    }

    void OnAcquireFromPool_Implementation() override {
        ++AcquireCount;
    }

    void OnReleaseToPool_Implementation() override {
        ++ReleaseCount;
    }

    int32 AcquireCount = 0;
    int32 ReleaseCount = 0;
};
