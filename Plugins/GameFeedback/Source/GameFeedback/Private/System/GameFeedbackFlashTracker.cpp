// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameFeedbackFlashTracker.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

namespace {

/**
 * @brief 收集受击目标上满足筛选条件的网格组件
 * @param Target 受击目标
 * @param HitResult 命中结果（精确模式据此取出被击中的网格组件）
 * @param Action 闪白参数
 * @param OutMeshes 输出符合条件的网格组件集合
 */
void CollectCandidateMeshes(AActor* Target, const FHitResult& HitResult, const FGameFeedbackHitFlashAction& Action, TArray<UMeshComponent*>& OutMeshes)
{
    if (Action.TargetMode == EGameFeedbackFlashTargetMode::HitComponentOnly) {
        if (UMeshComponent* HitMesh = Cast<UMeshComponent>(HitResult.GetComponent())) {
            OutMeshes.Add(HitMesh);
        }
        return;
    }

    if (Action.TargetMode == EGameFeedbackFlashTargetMode::AllSkeletalMeshes) {
        TInlineComponentArray<USkeletalMeshComponent*> SkelMeshes(Target);
        for (USkeletalMeshComponent* SkelMesh : SkelMeshes) {
            if (SkelMesh) {
                OutMeshes.Add(SkelMesh);
            }
        }
        return;
    }

    if (Action.TargetMode == EGameFeedbackFlashTargetMode::TaggedComponentsOnly) {
        TInlineComponentArray<UMeshComponent*> AllMeshes(Target);
        for (UMeshComponent* Mesh : AllMeshes) {
            if (Mesh && Mesh->ComponentHasTag(Action.ComponentFilterTag)) {
                OutMeshes.Add(Mesh);
            }
        }
    }
}

}

/**
 * @brief 对目标应用受击闪白（同一网格多次命中按绝对到期时间平滑续期）
 * @param World 当前世界
 * @param Target 受击目标
 * @param HitResult 命中结果（精确模式据此取出被击中的网格组件）
 * @param Action 闪白参数
 * @param Magnitude 本次表现强度
 */
void FGameFeedbackFlashTracker::Execute(UWorld* World, AActor* Target, const FHitResult& HitResult, const FGameFeedbackHitFlashAction& Action, float Magnitude)
{
    if (!World || !Target || !Action.bEnable || !Action.OverlayMaterial || Magnitude < Action.MinMagnitude) {
        return;
    }

    TArray<UMeshComponent*> CandidateMeshes;
    CollectCandidateMeshes(Target, HitResult, Action, CandidateMeshes);

    const double CurrentRealTime = FPlatformTime::Seconds();
    const double NewExpirationTime = CurrentRealTime + Action.Duration;

    for (UMeshComponent* Mesh : CandidateMeshes) {
        if (!Mesh) {
            continue;
        }

        FRecord& Record = Records.FindOrAdd(Mesh);
        if (Record.ExpirationRealTime <= CurrentRealTime) {
            Record.OriginalOverlayMaterial = TStrongObjectPtr<UMaterialInterface>(Mesh->GetOverlayMaterial());
        }
        Record.ExpirationRealTime = FMath::Max(Record.ExpirationRealTime, NewExpirationTime);
        Mesh->SetOverlayMaterial(Action.OverlayMaterial);

        const float RemainingDuration = static_cast<float>(FMath::Max(0.0, Record.ExpirationRealTime - CurrentRealTime));
        World->GetTimerManager().ClearTimer(Record.TimerHandle);
        TWeakObjectPtr<UMeshComponent> WeakMesh(Mesh);
        World->GetTimerManager().SetTimer(
            Record.TimerHandle,
            FTimerDelegate::CreateRaw(this, &FGameFeedbackFlashTracker::OnExpiration, WeakMesh),
            RemainingDuration,
            false);
    }
}

/**
 * @brief 还原指定 Actor 上全部网格的原始 Overlay 并移除追踪
 * @param World 当前世界
 * @param Target 待重置目标
 */
void FGameFeedbackFlashTracker::ClearActor(UWorld* World, AActor* Target)
{
    if (!Target) {
        return;
    }

    TInlineComponentArray<UMeshComponent*> Meshes(Target);
    for (UMeshComponent* Mesh : Meshes) {
        if (!Mesh) {
            continue;
        }
        if (FRecord* Record = Records.Find(Mesh)) {
            if (World) {
                World->GetTimerManager().ClearTimer(Record->TimerHandle);
            }
            Mesh->SetOverlayMaterial(Record->OriginalOverlayMaterial.Get());
            Records.Remove(Mesh);
        }
    }
}

/**
 * @brief 还原全部网格 Overlay 并清空追踪（子系统注销时调用）
 * @param World 当前世界
 */
void FGameFeedbackFlashTracker::Shutdown(UWorld* World)
{
    if (World) {
        FTimerManager& TimerManager = World->GetTimerManager();
        for (auto& Pair : Records) {
            TimerManager.ClearTimer(Pair.Value.TimerHandle);
            if (UMeshComponent* Mesh = Pair.Key.Get()) {
                Mesh->SetOverlayMaterial(Pair.Value.OriginalOverlayMaterial.Get());
            }
        }
    }

    Records.Empty();
}

/**
 * @brief 到期回调按真实时间还原 Overlay；提前触发时按剩余时间重新 arm
 * @param WeakMesh 目标网格弱引用
 */
void FGameFeedbackFlashTracker::OnExpiration(TWeakObjectPtr<UMeshComponent> WeakMesh)
{
    UMeshComponent* TargetMesh = WeakMesh.Get();
    if (!TargetMesh) {
        Records.Remove(WeakMesh);
        return;
    }

    FRecord* Record = Records.Find(TargetMesh);
    if (!Record) {
        return;
    }

    const double RemainingTime = Record->ExpirationRealTime - FPlatformTime::Seconds();
    if (RemainingTime <= 0.0) {
        TargetMesh->SetOverlayMaterial(Record->OriginalOverlayMaterial.Get());
        Records.Remove(TargetMesh);
        return;
    }

    TargetMesh->GetWorld()->GetTimerManager().SetTimer(
        Record->TimerHandle,
        FTimerDelegate::CreateRaw(this, &FGameFeedbackFlashTracker::OnExpiration, WeakMesh),
        static_cast<float>(RemainingTime),
        false);
}
