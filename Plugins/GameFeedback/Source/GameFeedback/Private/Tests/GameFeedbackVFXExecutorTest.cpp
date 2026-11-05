/**
 * @file GameFeedbackVFXExecutorTest.cpp
 * @brief 粒子特效执行器朝向解析与生成附着自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "NiagaraSystem.h"
#include "System/GameFeedbackVFXExecutor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXTraceOrientationTest,
    "Game.Feedback.VFXTraceOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证网络缺失 Trace 端点时 VFX 仍按显式轨迹方向计算朝向
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackVFXTraceOrientationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameFeedbackSignal Signal;
    Signal.TraceDirection = FVector::ForwardVector;
    Signal.HitResult.ImpactNormal = FVector::UpVector;
    Signal.HitResult.TraceStart = FVector::ZeroVector;
    Signal.HitResult.TraceEnd = FVector::ZeroVector;

    const FRotator OppositeRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::OppositeToTraceDirection);
    TestEqual(TEXT("反向轨迹模式必须使用显式 TraceDirection"), OppositeRotation.Vector(), -FVector::ForwardVector);

    const FRotator ReflectedRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::AlignToReflectedNormal);
    const FVector ExpectedReflection = FMath::GetReflectionVector(FVector::ForwardVector, FVector::UpVector);
    TestTrue(TEXT("反射法线模式必须使用显式 TraceDirection"), ReflectedRotation.Vector().Equals(ExpectedReflection, KINDA_SMALL_NUMBER));

    FGameFeedbackSignal ZeroTraceSignal;
    ZeroTraceSignal.TraceDirection = FVector::ZeroVector;
    ZeroTraceSignal.HitResult.ImpactNormal = FVector::UpVector;
    const FRotator ZeroOppositeRotation = GameFeedback::Execution::ResolveVFXRotation(
        ZeroTraceSignal, EGameFeedbackVFXOrientation::OppositeToTraceDirection);
    TestEqual(TEXT("缺失 TraceDirection 时反向轨迹模式降级为法线朝向"), ZeroOppositeRotation, FVector::UpVector.Rotation());
    const FRotator ZeroReflectedRotation = GameFeedback::Execution::ResolveVFXRotation(
        ZeroTraceSignal, EGameFeedbackVFXOrientation::AlignToReflectedNormal);
    TestEqual(TEXT("缺失 TraceDirection 时反射法线模式降级为法线朝向"), ZeroReflectedRotation, FVector::UpVector.Rotation());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXExecutorSpawnTest,
    "Game.Feedback.VFXExecutorSpawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证粒子特效发射在阈值门禁、异步预热、位置回退及附着组件分支的行为
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackVFXExecutorSpawnTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Target = World->SpawnActor<AActor>();
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Target);
    Target->SetRootComponent(Mesh);
    Mesh->RegisterComponent();

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(50.0f, 0.0f, 0.0f);
    Signal.HitResult.Component = Mesh;

    FGameFeedbackVFXAction Action;
    Action.MinMagnitude = 5.0f;

    TestNotNull(TEXT("测试世界有效"), World);
    TestNotNull(TEXT("测试目标实体有效"), Target);
    TestNotNull(TEXT("测试网格组件有效"), Mesh);

    GameFeedback::Execution::SpawnVFX(nullptr, Signal, Action);
    Signal.Magnitude = 2.0f;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);
    TestTrue(TEXT("低于最小强度门禁时安全跳过"), Signal.Magnitude < Action.MinMagnitude);
    Signal.Magnitude = 10.0f;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);
    TestTrue(TEXT("未配置特效软指针时安全返回"), Action.NiagaraSystem.IsNull());

    Action.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentFX.NonExistentFX")));
    TestFalse(TEXT("配置软指针后非空"), Action.NiagaraSystem.IsNull());
    TestFalse(TEXT("未加载软资产有效指针为空"), Action.NiagaraSystem.IsValid());
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);

    UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage());
    TestNotNull(TEXT("测试粒子系统创建成功"), System);
    Action.NiagaraSystem = System;
    TestTrue(TEXT("直接绑定粒子系统对象时有效"), Action.NiagaraSystem.IsValid());
    Action.bAttachToHitComponent = false;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);

    Signal.HitResult.ImpactPoint = FVector::ZeroVector;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);
    Signal.Target = nullptr;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);

    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(50.0f, 0.0f, 0.0f);
    Action.bAttachToHitComponent = true;
    GameFeedback::Execution::SpawnVFX(World, Signal, Action);

    World->DestroyWorld(false);
    World->RemoveFromRoot();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
