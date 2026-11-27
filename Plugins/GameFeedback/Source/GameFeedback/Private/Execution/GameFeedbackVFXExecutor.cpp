// Copyright Epic Games, Inc. All Rights Reserved.

#include "Execution/GameFeedbackVFXExecutor.h"

#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "System/GameFeedbackInternalHelpers.h"

namespace GameFeedback::Execution {

/**
 * @brief 按显式信号方向计算粒子发射朝向（自动归一化反射法线并在缺失轨迹时降级至表面法线）
 * @param Signal 表现信号
 * @param OrientationMode 粒子朝向模式
 * @return 粒子世界旋转
 */
FRotator ResolveVFXRotation(const FGameFeedbackSignal& Signal, EGameFeedbackVFXOrientation OrientationMode) {
    switch (OrientationMode) {
    case EGameFeedbackVFXOrientation::AlignToImpactDirection:
        return Signal.ImpactDirection.IsNearlyZero() ? Signal.HitResult.ImpactNormal.Rotation() : Signal.ImpactDirection.Rotation();
    case EGameFeedbackVFXOrientation::OppositeToTraceDirection:
        return Signal.TraceDirection.IsNearlyZero()
            ? Signal.HitResult.ImpactNormal.Rotation()
            : (-Signal.TraceDirection).GetSafeNormal().Rotation();
    case EGameFeedbackVFXOrientation::AlignToReflectedNormal: {
        const FVector SafeNormal = FVector(Signal.HitResult.ImpactNormal).GetSafeNormal();
        if (Signal.TraceDirection.IsNearlyZero() || SafeNormal.IsNearlyZero()) {
            return Signal.HitResult.ImpactNormal.Rotation();
        }
        return FMath::GetReflectionVector(Signal.TraceDirection, SafeNormal).Rotation();
    }
    case EGameFeedbackVFXOrientation::AlignToNormal:
    default:
        return Signal.HitResult.ImpactNormal.Rotation();
    }
}

/**
 * @brief 纯逻辑推导本次粒子特效的执行计划（阈值门禁、DS 短路、异步预热、附着回退与位姿计算）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 * @return 确定的粒子执行计划
 */
FGameFeedbackVFXSpawnPlan ResolveVFXSpawnPlan(
    UWorld* World,
    const FGameFeedbackSignal& Signal,
    const FGameFeedbackVFXAction& Action) {
    FGameFeedbackVFXSpawnPlan Plan;
    if (!World || World->GetNetMode() == NM_DedicatedServer || Signal.Magnitude < Action.MinMagnitude || Action.NiagaraSystem.IsNull()) {
        return Plan;
    }

    UNiagaraSystem* System = Action.NiagaraSystem.Get();
    if (!System) {
        Plan.Mode = EGameFeedbackVFXSpawnMode::AsyncPreload;
        return Plan;
    }

    Plan.System = System;
    Plan.Location = GameFeedback::Internal::ResolveImpactLocation(Signal);
    Plan.Rotation = ResolveVFXRotation(Signal, Action.OrientationMode);

    UPrimitiveComponent* HitComponent = Signal.HitResult.GetComponent();
    if (Action.bAttachToHitComponent && HitComponent && HitComponent->IsRegistered() && !HitComponent->IsBeingDestroyed()) {
        Plan.Mode = EGameFeedbackVFXSpawnMode::SpawnAttached;
        Plan.AttachComponent = HitComponent;
        Plan.BoneName = Signal.HitResult.BoneName;
        return Plan;
    }

    Plan.Mode = EGameFeedbackVFXSpawnMode::SpawnAtLocation;
    return Plan;
}

/**
 * @brief 在世界中发射受击粒子特效（软引用未就绪时经 DefinitionProvider 去重异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 * @return 成功生成的 Niagara 组件指针；被门禁拦截、DS 短路、无头渲染禁用或异步预热时返回 nullptr
 */
UNiagaraComponent* SpawnVFX(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action) {
    const FGameFeedbackVFXSpawnPlan Plan = ResolveVFXSpawnPlan(World, Signal, Action);
    if (Plan.Mode == EGameFeedbackVFXSpawnMode::Skipped) {
        return nullptr;
    }

    if (Plan.Mode == EGameFeedbackVFXSpawnMode::AsyncPreload) {
        if (UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(World)) {
            Provider->PreloadSoftAsset(Action.NiagaraSystem.ToSoftObjectPath());
        }
        return nullptr;
    }

    if (Plan.Mode == EGameFeedbackVFXSpawnMode::SpawnAttached) {
        return UNiagaraFunctionLibrary::SpawnSystemAttached(
            Plan.System,
            Plan.AttachComponent,
            Plan.BoneName,
            Plan.Location,
            Plan.Rotation,
            EAttachLocation::KeepWorldPosition,
            true,
            true,
            ENCPoolMethod::AutoRelease
        );
    }

    return UNiagaraFunctionLibrary::SpawnSystemAtLocation(
        World,
        Plan.System,
        Plan.Location,
        Plan.Rotation,
        FVector::OneVector,
        true,
        true,
        ENCPoolMethod::AutoRelease
    );
}

}  // namespace GameFeedback::Execution
