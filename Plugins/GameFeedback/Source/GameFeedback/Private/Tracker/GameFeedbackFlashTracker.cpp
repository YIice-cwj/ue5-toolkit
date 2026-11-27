// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracker/GameFeedbackFlashTracker.h"

#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "TimerManager.h"

namespace {

using FInlineMeshArray = TArray<UMeshComponent*, TInlineAllocator<4>>;

/**
 * @brief 收集受击目标上满足筛选条件的网格组件（使用栈内联分配器避免常规堆分配）
 * @param Target 受击目标
 * @param HitResult 命中结果（精确模式据此取出被击中的网格组件）
 * @param Action 闪白参数
 * @param OutMeshes 输出符合条件的网格组件集合
 */
void CollectCandidateMeshes(AActor* Target, const FHitResult& HitResult, const FGameFeedbackHitFlashAction& Action, FInlineMeshArray& OutMeshes) {
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

}  // namespace

/**
 * @brief 析构函数：自动兜底还原全部追踪网格并失效回调令牌，杜绝定时器 UAF
 */
FGameFeedbackFlashTracker::~FGameFeedbackFlashTracker() {
    Shutdown(BoundWorld.Get());
}

/**
 * @brief 对目标应用受击闪白（同一网格多次命中按绝对到期时间平滑续期）
 * @param World 当前世界
 * @param Target 受击目标
 * @param HitResult 命中结果（精确模式据此取出被击中的网格组件）
 * @param Action 闪白参数
 * @param Magnitude 本次表现强度
 */
void FGameFeedbackFlashTracker::Execute(UWorld* World, AActor* Target, const FHitResult& HitResult, const FGameFeedbackHitFlashAction& Action, float Magnitude) {
    if (!World || !Target || !Action.bEnable || !Action.OverlayMaterial || Magnitude < Action.MinMagnitude) {
        return;
    }

    BoundWorld = World;
    if (!Records.IsEmpty()) {
        PruneInvalid(World);
    }

    FInlineMeshArray CandidateMeshes;
    CollectCandidateMeshes(Target, HitResult, Action, CandidateMeshes);
    if (CandidateMeshes.IsEmpty()) {
        return;
    }

    const double CurrentRealTime = GetCurrentTime();
    const double NewExpirationTime = CurrentRealTime + Action.Duration;

    for (UMeshComponent* Mesh : CandidateMeshes) {
        const TObjectKey<UMeshComponent> MeshKey(Mesh);
        FRecord* ExistingRecord = Records.Find(MeshKey);
        if (!ExistingRecord) {
            ExistingRecord = &Records.Add(MeshKey);
            ExistingRecord->OriginalOverlayMaterial = TStrongObjectPtr<UMaterialInterface>(Mesh->GetOverlayMaterial());
        }

        FRecord& Record = *ExistingRecord;
        const bool bNeedRearm = !World->GetTimerManager().IsTimerActive(Record.TimerHandle)
            || NewExpirationTime > Record.ExpirationRealTime;
        Record.ExpirationRealTime = FMath::Max(Record.ExpirationRealTime, NewExpirationTime);
        Mesh->SetOverlayMaterial(Action.OverlayMaterial);

        if (bNeedRearm) {
            const float RemainingDuration = static_cast<float>(FMath::Max(0.0, Record.ExpirationRealTime - CurrentRealTime));
            ArmExpirationTimer(*World, Record.TimerHandle, MeshKey, RemainingDuration);
        }
    }
}

/**
 * @brief 还原指定 Actor 上全部网格的原始 Overlay 并移除追踪
 * @param World 当前世界
 * @param Target 待重置目标
 */
void FGameFeedbackFlashTracker::ClearActor(UWorld* World, AActor* Target) {
    if (!Target) {
        return;
    }

    UWorld* EffectiveWorld = World ? World : Target->GetWorld();
    TInlineComponentArray<UMeshComponent*> Meshes(Target);
    for (UMeshComponent* Mesh : Meshes) {
        if (!Mesh) {
            continue;
        }
        const TObjectKey<UMeshComponent> MeshKey(Mesh);
        FRecord* Record = Records.Find(MeshKey);
        if (!Record) {
            continue;
        }

        if (UWorld* TargetWorld = EffectiveWorld ? EffectiveWorld : Mesh->GetWorld()) {
            TargetWorld->GetTimerManager().ClearTimer(Record->TimerHandle);
        }
        Mesh->SetOverlayMaterial(Record->OriginalOverlayMaterial.Get());
        Records.Remove(MeshKey);
    }
}

/**
 * @brief 还原全部网格 Overlay 并清空追踪（子系统注销或析构时调用）
 * @param World 当前世界
 */
void FGameFeedbackFlashTracker::Shutdown(UWorld* World) {
    UWorld* EffectiveWorld = World ? World : BoundWorld.Get();
    for (auto& Pair : Records) {
        UMeshComponent* Mesh = Pair.Key.ResolveObjectPtr();
        UWorld* TargetWorld = EffectiveWorld ? EffectiveWorld : (IsValid(Mesh) ? Mesh->GetWorld() : nullptr);
        if (TargetWorld) {
            TargetWorld->GetTimerManager().ClearTimer(Pair.Value.TimerHandle);
        }
        if (IsValid(Mesh)) {
            Mesh->SetOverlayMaterial(Pair.Value.OriginalOverlayMaterial.Get());
        }
    }

    Records.Empty();
}

/**
 * @brief 清理失效网格记录并同步取消残留定时器（释放已销毁网格所持有的强引用材质）
 * @param World 可选当前世界（非空时同步清除失效记录绑定的定时器）
 */
void FGameFeedbackFlashTracker::PruneInvalid(UWorld* World) {
    UWorld* EffectiveWorld = World ? World : BoundWorld.Get();
    for (auto It = Records.CreateIterator(); It; ++It) {
        if (!IsValid(It.Key().ResolveObjectPtr())) {
            if (EffectiveWorld) {
                EffectiveWorld->GetTimerManager().ClearTimer(It.Value().TimerHandle);
            }
            It.RemoveCurrent();
        }
    }
}

/**
 * @brief 设置时间提供者（用于测试或可控时钟注入）
 * @param InTimeProvider 返回当前时间（秒）的函数对象
 */
void FGameFeedbackFlashTracker::SetTimeProvider(TFunction<double()> InTimeProvider) {
    TimeProvider = MoveTemp(InTimeProvider);
}

/**
 * @brief 获取当前时间（秒），优先使用注入的时间提供者
 * @return 当前时间（秒）
 */
double FGameFeedbackFlashTracker::GetCurrentTime() const {
    return TimeProvider ? TimeProvider() : FPlatformTime::Seconds();
}

/**
 * @brief 挂载带弱存活令牌保护的到期定时器
 * @param World 目标世界
 * @param TimerHandle 输出定时器句柄
 * @param MeshKey 目标网格不可变对象键
 * @param DelaySeconds 延迟秒数
 */
void FGameFeedbackFlashTracker::ArmExpirationTimer(
    UWorld& World,
    FTimerHandle& TimerHandle,
    TObjectKey<UMeshComponent> MeshKey,
    float DelaySeconds) {
    FTimerManager& TimerManager = World.GetTimerManager();
    TimerManager.ClearTimer(TimerHandle);

    TWeakPtr<bool> WeakAlive = AliveToken;
    TimerManager.SetTimer(
        TimerHandle,
        FTimerDelegate::CreateLambda([WeakAlive, this, MeshKey]() {
            if (WeakAlive.IsValid()) {
                OnExpiration(MeshKey);
            }
        }),
        DelaySeconds,
        false);
}

/**
 * @brief 到期回调按真实时间还原 Overlay；提前触发时按剩余时间重新 arm
 * @param MeshKey 目标网格不可变对象键
 */
void FGameFeedbackFlashTracker::OnExpiration(TObjectKey<UMeshComponent> MeshKey) {
    UMeshComponent* TargetMesh = MeshKey.ResolveObjectPtr();
    if (!IsValid(TargetMesh)) {
        Records.Remove(MeshKey);
        return;
    }

    FRecord* Record = Records.Find(MeshKey);
    if (!Record) {
        return;
    }

    const double RemainingTime = Record->ExpirationRealTime - GetCurrentTime();
    if (RemainingTime <= 0.0) {
        TargetMesh->SetOverlayMaterial(Record->OriginalOverlayMaterial.Get());
        Records.Remove(MeshKey);
        return;
    }

    UWorld* World = TargetMesh->GetWorld();
    if (!World) {
        TargetMesh->SetOverlayMaterial(Record->OriginalOverlayMaterial.Get());
        Records.Remove(MeshKey);
        return;
    }

    ArmExpirationTimer(*World, Record->TimerHandle, MeshKey, static_cast<float>(RemainingTime));
}
