// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "Pool/IGamePoolable.h"

#include "GamePoolableTestActor.generated.h"

/**
 * @brief 消息总线测试载荷结构体
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FTestGameplayMessage {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameCore|Test")
    int32 Value = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameCore|Test")
    FString Text;
};

/**
 * @brief 异步消息动作测试接收器
 */
UCLASS()
class GAMECORE_API UGameMessageTestReceiver : public UObject {
    GENERATED_BODY()

public:
    /** 异步节点回调：累计接收次数并记录实际通道 */
    UFUNCTION()
    void OnAsyncMessageReceived(FGameplayTag ActualChannel) {
        ++ReceivedCount;
        LastChannel = ActualChannel;
    }

    int32 ReceivedCount = 0;
    FGameplayTag LastChannel;
};

/**
 * @brief 对象池单元测试专用 Actor（实现 IGamePoolable 回调计数）
 */
UCLASS()
class GAMECORE_API AGamePoolableTestActor : public AActor, public IGamePoolable {
    GENERATED_BODY()

public:
    /** 构造：创建 SceneRoot 根组件（供池化生命周期演示挂载） */
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
