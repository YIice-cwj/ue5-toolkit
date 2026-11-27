/**
 * @file GameFeedbackVFXExecutorTest.cpp
 * @brief 粒子特效执行器朝向解析、生成计划决策、附着回退、去重预热与 DS 短路自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Execution/GameFeedbackVFXExecutor.h"
#include "Feedback/GameFeedbackActions.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "Tests/GameFeedbackTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXTraceOrientationTest,
    "Game.Feedback.VFXTraceOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证粒子四种朝向模式、非单位法线归一化反射与缺失轨迹时的优雅降级
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackVFXTraceOrientationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameFeedbackSignal Signal;
    Signal.TraceDirection = FVector::ForwardVector;
    Signal.ImpactDirection = FVector::RightVector;
    Signal.HitResult.ImpactNormal = FVector::UpVector;
    Signal.HitResult.TraceStart = FVector::ZeroVector;
    Signal.HitResult.TraceEnd = FVector::ZeroVector;

    const FRotator NormalRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::AlignToNormal);
    TestEqual(TEXT("法线模式应对齐 ImpactNormal"), NormalRotation, FVector::UpVector.Rotation());

    const FRotator ImpactRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::AlignToImpactDirection);
    TestEqual(TEXT("冲击方向模式应对齐 ImpactDirection"), ImpactRotation, FVector::RightVector.Rotation());

    const FRotator OppositeRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::OppositeToTraceDirection);
    TestEqual(TEXT("反向轨迹模式必须使用显式 TraceDirection"), OppositeRotation.Vector(), -FVector::ForwardVector);

    const FRotator ReflectedRotation = GameFeedback::Execution::ResolveVFXRotation(
        Signal, EGameFeedbackVFXOrientation::AlignToReflectedNormal);
    const FVector ExpectedReflection = FMath::GetReflectionVector(FVector::ForwardVector, FVector::UpVector);
    TestTrue(TEXT("反射法线模式必须使用显式 TraceDirection"), ReflectedRotation.Vector().Equals(ExpectedReflection, KINDA_SMALL_NUMBER));

    FGameFeedbackSignal UnnormalizedNormalSignal = Signal;
    UnnormalizedNormalSignal.TraceDirection = FVector(1.0f, 0.0f, -1.0f).GetSafeNormal();
    UnnormalizedNormalSignal.HitResult.ImpactNormal = FVector(0.0f, 0.0f, 5.0f);
    const FRotator NormalizedReflected = GameFeedback::Execution::ResolveVFXRotation(
        UnnormalizedNormalSignal, EGameFeedbackVFXOrientation::AlignToReflectedNormal);
    const FVector ExpectedUnitReflection = FMath::GetReflectionVector(UnnormalizedNormalSignal.TraceDirection, FVector::UpVector);
    TestTrue(TEXT("非单位法线必须先归一化再计算反射方向"), NormalizedReflected.Vector().Equals(ExpectedUnitReflection, KINDA_SMALL_NUMBER));

    FGameFeedbackSignal ZeroTraceSignal;
    ZeroTraceSignal.TraceDirection = FVector::ZeroVector;
    ZeroTraceSignal.ImpactDirection = FVector::ZeroVector;
    ZeroTraceSignal.HitResult.ImpactNormal = FVector::UpVector;
    TestEqual(
        TEXT("缺失 ImpactDirection 时降级为法线朝向"),
        GameFeedback::Execution::ResolveVFXRotation(ZeroTraceSignal, EGameFeedbackVFXOrientation::AlignToImpactDirection),
        FVector::UpVector.Rotation());
    TestEqual(
        TEXT("缺失 TraceDirection 时反向轨迹模式降级为法线朝向"),
        GameFeedback::Execution::ResolveVFXRotation(ZeroTraceSignal, EGameFeedbackVFXOrientation::OppositeToTraceDirection),
        FVector::UpVector.Rotation());
    TestEqual(
        TEXT("缺失 TraceDirection 时反射法线模式降级为法线朝向"),
        GameFeedback::Execution::ResolveVFXRotation(ZeroTraceSignal, EGameFeedbackVFXOrientation::AlignToReflectedNormal),
        FVector::UpVector.Rotation());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXExecutorSpawnTest,
    "Game.Feedback.VFXExecutorSpawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证粒子特效发射的阈值门禁、去重异步预热、世界空间与附着生成计划决策及组件生成
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackVFXExecutorSpawnTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameFeedback::Execution;

    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();
    TestNotNull(TEXT("DefinitionProvider 有效"), Provider);
    if (!Provider) {
        return false;
    }

    AActor* Target = World->SpawnActor<AActor>();
    UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Target);
    Target->SetRootComponent(Mesh);
    Mesh->RegisterComponent();

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(50.0f, 0.0f, 0.0f);
    Signal.HitResult.Component = Mesh;
    Signal.HitResult.BoneName = FName(TEXT("Spine_01"));

    FGameFeedbackVFXAction Action;
    Action.MinMagnitude = 5.0f;

    TestEqual(TEXT("空世界应决策跳过"), ResolveVFXSpawnPlan(nullptr, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::Skipped);
    TestNull(TEXT("空世界不得生成粒子组件"), SpawnVFX(nullptr, Signal, Action));

    Signal.Magnitude = 2.0f;
    TestEqual(TEXT("低于最小强度门禁应决策跳过"), ResolveVFXSpawnPlan(World, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::Skipped);
    TestNull(TEXT("低于最小强度门禁不得生成粒子组件"), SpawnVFX(World, Signal, Action));

    Signal.Magnitude = 10.0f;
    TestEqual(TEXT("未配置粒子软引用应决策跳过"), ResolveVFXSpawnPlan(World, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::Skipped);

    Action.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Feedback/Test/NonExistentFX.NonExistentFX")));
    TestEqual(TEXT("未驻留软引用应决策异步预热"), ResolveVFXSpawnPlan(World, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::AsyncPreload);
    TestNull(TEXT("未驻留软引用触发预热并返回空"), SpawnVFX(World, Signal, Action));
    const int32 HandlesAfterFirstMiss = Provider->GetActivePreloadHandleCount();
    TestNull(TEXT("同路径重复请求必须去重"), SpawnVFX(World, Signal, Action));
    TestEqual(TEXT("重复未命中绝不得新增泄漏句柄"), Provider->GetActivePreloadHandleCount(), HandlesAfterFirstMiss);

    UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage());
    Action.NiagaraSystem = System;
    Action.bAttachToHitComponent = false;
    const FGameFeedbackVFXSpawnPlan WorldPlan = ResolveVFXSpawnPlan(World, Signal, Action);
    TestEqual(TEXT("非附着模式应决策世界空间生成"), WorldPlan.Mode, EGameFeedbackVFXSpawnMode::SpawnAtLocation);
    TestEqual(TEXT("世界空间计划目标资产应匹配"), WorldPlan.System, System);
    TestNull(TEXT("世界空间计划不得设置附着组件"), WorldPlan.AttachComponent);
    TestTrue(TEXT("世界空间计划坐标应对齐命中点"), WorldPlan.Location.Equals(FVector(50.0f, 0.0f, 0.0f), 0.1f));

    UNiagaraComponent* WorldSpawned = SpawnVFX(World, Signal, Action);
    if (FApp::CanEverRender()) {
        TestNotNull(TEXT("可渲染环境下世界空间生成应返回有效 NiagaraComponent"), WorldSpawned);
    }

    Action.bAttachToHitComponent = true;
    const FGameFeedbackVFXSpawnPlan AttachPlan = ResolveVFXSpawnPlan(World, Signal, Action);
    TestEqual(TEXT("已注册组件在附着模式下应决策 SpawnAttached"), AttachPlan.Mode, EGameFeedbackVFXSpawnMode::SpawnAttached);
    TestEqual(TEXT("附着计划目标组件必须指向命中网格"), AttachPlan.AttachComponent, static_cast<UPrimitiveComponent*>(Mesh));
    TestEqual(TEXT("附着计划骨骼名必须透传"), AttachPlan.BoneName, FName(TEXT("Spine_01")));

    UNiagaraComponent* AttachedSpawned = SpawnVFX(World, Signal, Action);
    if (FApp::CanEverRender() && AttachedSpawned) {
        TestEqual(TEXT("可渲染环境下粒子组件必须挂接于命中网格组件"), AttachedSpawned->GetAttachParent(), static_cast<USceneComponent*>(Mesh));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXDedicatedServerTest,
    "Game.Feedback.VFXDedicatedServer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Dedicated Server 环境下特效执行器直接短路跳过且零句柄注册
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackVFXDedicatedServerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameFeedback::Execution;

    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    World->WorldType = EWorldType::PIE;
    World->SetPlayInEditorInitialNetMode(NM_DedicatedServer);
    TestEqual(TEXT("测试世界必须处于 DedicatedServer 模式"), World->GetNetMode(), NM_DedicatedServer);
    UGameFeedbackDefinitionProvider* Provider = World->GetSubsystem<UGameFeedbackDefinitionProvider>();

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;

    FGameFeedbackVFXAction Action;
    Action.MinMagnitude = 1.0f;
    Action.NiagaraSystem = NewObject<UNiagaraSystem>(GetTransientPackage());

    TestEqual(TEXT("DS 环境下已加载粒子必须决策跳过"), ResolveVFXSpawnPlan(World, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::Skipped);
    TestNull(TEXT("DS 环境下已加载粒子必须短路返回 nullptr"), SpawnVFX(World, Signal, Action));

    Action.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Feedback/Test/DSFX.DSFX")));
    TestEqual(TEXT("DS 环境下软引用粒子必须决策跳过"), ResolveVFXSpawnPlan(World, Signal, Action).Mode, EGameFeedbackVFXSpawnMode::Skipped);
    TestNull(TEXT("DS 环境下软引用粒子必须短路返回 nullptr"), SpawnVFX(World, Signal, Action));
    if (Provider) {
        TestEqual(TEXT("DS 短路不得注册任何预热句柄"), Provider->GetActivePreloadHandleCount(), 0);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackVFXUnregisteredFallbackTest,
    "Game.Feedback.VFXUnregisteredFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证当命中的组件未注册时，VFX 执行器平滑降级决策为世界空间生成而不附着于非法组件
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackVFXUnregisteredFallbackTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using namespace GameFeedback::Execution;

    GameFeedbackTestHelpers::FScopedLightweightGameWorld WorldGuard;
    UWorld* World = WorldGuard.GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    AActor* Target = World->SpawnActor<AActor>();
    UStaticMeshComponent* UnregisteredMesh = NewObject<UStaticMeshComponent>(Target);

    FGameFeedbackSignal Signal;
    Signal.Magnitude = 10.0f;
    Signal.Target = Target;
    Signal.HitResult.ImpactPoint = FVector(100.0f, 0.0f, 0.0f);
    Signal.HitResult.Component = UnregisteredMesh;

    FGameFeedbackVFXAction Action;
    Action.MinMagnitude = 1.0f;
    Action.bAttachToHitComponent = true;
    UNiagaraSystem* System = NewObject<UNiagaraSystem>(GetTransientPackage());
    Action.NiagaraSystem = System;

    const FGameFeedbackVFXSpawnPlan Plan = ResolveVFXSpawnPlan(World, Signal, Action);
    TestEqual(TEXT("未注册组件附着必须平滑降级为世界空间生成"), Plan.Mode, EGameFeedbackVFXSpawnMode::SpawnAtLocation);
    TestNull(TEXT("降级后的生成计划不得保留未注册附着组件"), Plan.AttachComponent);
    TestTrue(TEXT("降级生成坐标应对齐命中点"), Plan.Location.Equals(FVector(100.0f, 0.0f, 0.0f), 0.1f));

    UNiagaraComponent* FallbackComponent = SpawnVFX(World, Signal, Action);
    if (FApp::CanEverRender()) {
        TestNotNull(TEXT("可渲染环境下回退生成应返回有效组件"), FallbackComponent);
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
