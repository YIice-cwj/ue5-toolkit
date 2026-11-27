/**
 * @file AGameFeedbackTestActor.h
 * @brief 自动化测试专用可配置速度 Actor（覆盖 UGameFeedbackAdapter 非零速度冲击方向分支）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AGameFeedbackTestActor.generated.h"

/**
 * @brief 可显式注入速度向量的测试 Actor
 */
UCLASS(NotBlueprintable)
class AGameFeedbackTestActor : public AActor {
    GENERATED_BODY()

public:
    /**
     * @brief 返回注入的测试速度向量
     * @return 测试速度向量
     */
    FVector GetVelocity() const override {
        return SimulatedVelocity;
    }

    FVector SimulatedVelocity = FVector::ZeroVector;
};
