/**
 * @file UGameVitalComponent.cpp
 * @brief 实体生命与生死状态管理组件实现
 */

#include "Stat/UGameVitalComponent.h"

#include "Stat/UGameStatComponent.h"
#include "Net/UnrealNetwork.h"
#include "Logging/GameLogCategories.h"

UGameVitalComponent::UGameVitalComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

void UGameVitalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UGameVitalComponent, bVitalAlive);
}

void UGameVitalComponent::BeginPlay() {
    Super::BeginPlay();

    if (!BoundStatComponent.IsValid()) {
        if (UGameStatComponent* OwnerStatComp = ResolveStatComponent()) {
            BindStatComponent(OwnerStatComp);
        }
    }
}

void UGameVitalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (BoundStatComponent.IsValid() && StatChangedSubscriptionHandle.IsValid()) {
        BoundStatComponent->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
        StatChangedSubscriptionHandle.Reset();
    }

    Super::EndPlay(EndPlayReason);
}

void UGameVitalComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    if (BoundStatComponent.IsValid() && StatChangedSubscriptionHandle.IsValid()) {
        BoundStatComponent->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
        StatChangedSubscriptionHandle.Reset();
    }

    BoundStatComponent = InStatComponent;

    if (InStatComponent != nullptr && PrimaryVitalTag.IsValid()) {
        StatChangedSubscriptionHandle = InStatComponent->SubscribeStatChanged(
            this,
            PrimaryVitalTag,
            [this](const FGameplayTag& Tag, float OldVal, float NewVal, float MaxVal) {
                HandleBoundStatChanged(Tag, OldVal, NewVal, MaxVal);
            }
        );
        EvaluateVitalState();
        GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 成功绑定数值组件并监听主生命标签 [%s]。"),
            *GetNameSafe(GetOwner()), *PrimaryVitalTag.ToString());
    }
}

void UGameVitalComponent::SetPrimaryVitalTag(const FGameplayTag& InTag) {
    if (PrimaryVitalTag.MatchesTagExact(InTag)) {
        return;
    }

    if (BoundStatComponent.IsValid() && StatChangedSubscriptionHandle.IsValid()) {
        BoundStatComponent->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
        StatChangedSubscriptionHandle.Reset();
    }

    PrimaryVitalTag = InTag;

    if (UGameStatComponent* StatComp = ResolveStatComponent()) {
        if (InTag.IsValid()) {
            StatChangedSubscriptionHandle = StatComp->SubscribeStatChanged(
                this,
                PrimaryVitalTag,
                [this](const FGameplayTag& Tag, float OldVal, float NewVal, float MaxVal) {
                    HandleBoundStatChanged(Tag, OldVal, NewVal, MaxVal);
                }
            );
        }
    }

    EvaluateVitalState();
    GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 切换主生命标签为 [%s]。"),
        *GetNameSafe(GetOwner()), *InTag.ToString());
}

void UGameVitalComponent::ReviveVital(float FillPercent) {
    UGameStatComponent* StatComp = ResolveStatComponent();
    if (StatComp == nullptr || !PrimaryVitalTag.IsValid()) {
        GAME_LOG(Warning, TEXT("【生命组件】复活失败：数值组件未解析或主生命标签无效。"));
        return;
    }

    const float Min = StatComp->GetStatMin(PrimaryVitalTag);
    const float Max = StatComp->GetStatMax(PrimaryVitalTag);
    if (Max <= Min) {
        GAME_LOG(Warning, TEXT("【生命组件】复活失败：主生命标签 [%s] 的上限 <= 下限 (%.2f <= %.2f)。"),
            *PrimaryVitalTag.ToString(), Max, Min);
        return;
    }

    const float ClampedPercent = FMath::Clamp(FillPercent, 0.0f, 1.0f);
    const float TargetCurrent = Min + (Max - Min) * ClampedPercent;
    StatComp->SetStatCurrent(PrimaryVitalTag, TargetCurrent);
    EvaluateVitalState();
    GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 执行复活: 恢复比例=%.2f, 恢复后生命值=%.2f / %.2f。"),
        *GetNameSafe(GetOwner()), ClampedPercent, TargetCurrent, Max);
}

void UGameVitalComponent::EvaluateVitalState() {
    if (!PrimaryVitalTag.IsValid()) {
        if (!bVitalAlive) {
            bVitalAlive = true;
            BroadcastVitalStateChanged(true);
            GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 无主生命标签约束，恢复存活态。"), *GetNameSafe(GetOwner()));
        }
        return;
    }

    UGameStatComponent* StatComp = ResolveStatComponent();
    if (StatComp == nullptr || !StatComp->HasStat(PrimaryVitalTag)) {
        return;
    }

    const float EffectiveCurrent = StatComp->GetStatEffectiveCurrent(PrimaryVitalTag);
    const float Min = StatComp->GetStatMin(PrimaryVitalTag);
    const bool bNewAlive = EffectiveCurrent > Min;

    if (bNewAlive != bVitalAlive) {
        bVitalAlive = bNewAlive;
        BroadcastVitalStateChanged(bVitalAlive);
        GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 生死状态跃迁: %s (有效值=%.2f, 阈值=%.2f)。"),
            *GetNameSafe(GetOwner()), bVitalAlive ? TEXT("存活") : TEXT("阵亡"), EffectiveCurrent, Min);
    }
}

void UGameVitalComponent::OnRep_VitalAlive() {
    BroadcastVitalStateChanged(bVitalAlive);
}

void UGameVitalComponent::HandleBoundStatChanged(const FGameplayTag& StatTag, float OldValue, float NewValue, float MaxValue) {
    EvaluateVitalState();
}

void UGameVitalComponent::BroadcastVitalStateChanged(bool bNewAlive) {
    OnVitalStateChanged.Broadcast(bNewAlive, PrimaryVitalTag);
    OnVitalStateChangedNative.Broadcast(bNewAlive, PrimaryVitalTag);
}

UGameStatComponent* UGameVitalComponent::ResolveStatComponent() {
    if (BoundStatComponent.IsValid()) {
        return BoundStatComponent.Get();
    }

    if (AActor* OwnerActor = GetOwner()) {
        return OwnerActor->FindComponentByClass<UGameStatComponent>();
    }

    return nullptr;
}
