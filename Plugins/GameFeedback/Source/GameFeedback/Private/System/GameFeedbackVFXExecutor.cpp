// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackVFXExecutor.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace GameFeedback::Execution {

/**
 * @brief 按显式信号方向计算粒子发射朝向
 * @param Signal 表现信号
 * @param OrientationMode 粒子朝向模式
 * @return 粒子世界旋转
 */
FRotator ResolveVFXRotation(const FGameFeedbackSignal& Signal, EGameFeedbackVFXOrientation OrientationMode)
{
    switch (OrientationMode) {
    case EGameFeedbackVFXOrientation::AlignToImpactDirection:
        return Signal.ImpactDirection.IsNearlyZero() ? Signal.HitResult.ImpactNormal.Rotation() : Signal.ImpactDirection.Rotation();
    case EGameFeedbackVFXOrientation::OppositeToTraceDirection:
        return Signal.TraceDirection.IsNearlyZero()
            ? Signal.HitResult.ImpactNormal.Rotation()
            : (-Signal.TraceDirection).GetSafeNormal().Rotation();
    case EGameFeedbackVFXOrientation::AlignToReflectedNormal:
        return Signal.TraceDirection.IsNearlyZero()
            ? Signal.HitResult.ImpactNormal.Rotation()
            : FMath::GetReflectionVector(Signal.TraceDirection, Signal.HitResult.ImpactNormal).Rotation();
    case EGameFeedbackVFXOrientation::AlignToNormal:
    default:
        return Signal.HitResult.ImpactNormal.Rotation();
    }
}

/**
 * @brief 在世界中发射受击粒子特效（软引用未就绪时异步预热并跳过本次播放）
 * @param World 当前世界
 * @param Signal 表现信号
 * @param Action 粒子参数
 */
void SpawnVFX(UWorld* World, const FGameFeedbackSignal& Signal, const FGameFeedbackVFXAction& Action)
{
    if (!World || Signal.Magnitude < Action.MinMagnitude || Action.NiagaraSystem.IsNull()) {
        return;
    }

    UNiagaraSystem* System = Action.NiagaraSystem.Get();
    if (!System) {
        UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Action.NiagaraSystem.ToSoftObjectPath(),
            FStreamableDelegate(),
            FStreamableManager::DefaultAsyncLoadPriority,
            true);
        return;
    }

    const FRotator Rotation = ResolveVFXRotation(Signal, Action.OrientationMode);

    const FVector Location = Signal.HitResult.ImpactPoint.IsNearlyZero()
        ? (Signal.Target.IsValid() ? Signal.Target->GetActorLocation() : FVector::ZeroVector)
        : Signal.HitResult.ImpactPoint;

    if (Action.bAttachToHitComponent && Signal.HitResult.GetComponent()) {
        UNiagaraFunctionLibrary::SpawnSystemAttached(
            System,
            Signal.HitResult.GetComponent(),
            Signal.HitResult.BoneName,
            Location,
            Rotation,
            EAttachLocation::KeepWorldPosition,
            true,
            true,
            ENCPoolMethod::AutoRelease
        );
    } else {
        UNiagaraFunctionLibrary::SpawnSystemAtLocation(
            World,
            System,
            Location,
            Rotation,
            FVector::OneVector,
            true,
            true,
            ENCPoolMethod::AutoRelease
        );
    }
}

}
