/**
 * @file UGameVitalComponent.cpp
 * @brief 实体生命与生死状态管理组件实现
 */

#include "Stat/UGameVitalComponent.h"

#include "Stat/UGameStatComponent.h"
#include "Net/UnrealNetwork.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 构造函数：开启组件网络复制，关闭组件 Tick
 */
UGameVitalComponent::UGameVitalComponent() {
    PrimaryComponentTick.bCanEverTick = false;
    SetIsReplicatedByDefault(true);
}

/**
 * @brief 网络属性复制规则注册
 * @param OutLifetimeProps 输出的生命周期属性数组
 */
void UGameVitalComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const {
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UGameVitalComponent, bVitalAlive);
}

/**
 * @brief 从对象池出库激活回调
 */
void UGameVitalComponent::OnAcquireFromPool_Implementation() {
    EvaluateVitalState();
}

/**
 * @brief 归还对象池休眠回调：重置存活状态并回满主生命
 */
void UGameVitalComponent::OnReleaseToPool_Implementation() {
    bVitalAlive = true;
    bLastBroadcastedAlive = true;
    if (UGameStatComponent* StatComp = ResolveStatComponent()) {
        if (PrimaryVitalTag.IsValid() && StatComp->HasStat(PrimaryVitalTag)) {
            const float Max = StatComp->GetStatMax(PrimaryVitalTag);
            StatComp->SetStatCurrent(PrimaryVitalTag, Max);
        }
    }
    GAME_LOG(Log, TEXT("【生命组件】宿主 [%s] 归还对象池，已重置存活状态与主生命。"), *GetNameSafe(GetOwner()));
}

/**
 * @brief 组件初始化生命周期回调：若未显式绑定数值组件，尝试从 Owner 自动发现并挂接
 */
void UGameVitalComponent::BeginPlay() {
    Super::BeginPlay();

    if (!BoundStatComponent.IsValid()) {
        if (UGameStatComponent* OwnerStatComp = ResolveStatComponent()) {
            BindStatComponent(OwnerStatComp);
        }
    }
}

/**
 * @brief 组件销毁生命周期回调：注销与关联数值组件的属性监听委托
 * @param EndPlayReason 结束运行的原因
 */
void UGameVitalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    if (UGameStatComponent* StatComp = ResolveStatComponent()) {
        if (StatChangedSubscriptionHandle.IsValid()) {
            StatComp->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
            StatChangedSubscriptionHandle.Reset();
        }
    }

    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 显式绑定关联的数值组件
 * @param InStatComponent 目标数值组件指针
 */
void UGameVitalComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    if (UGameStatComponent* OldStatComp = ResolveStatComponent()) {
        if (StatChangedSubscriptionHandle.IsValid()) {
            OldStatComp->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
            StatChangedSubscriptionHandle.Reset();
        }
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

/**
 * @brief 设置主生命属性标签并依据关联组件重新评估生死状态
 * @param InTag 目标生命属性标签（例如 Stat.Health）
 */
void UGameVitalComponent::SetPrimaryVitalTag(const FGameplayTag& InTag) {
    if (PrimaryVitalTag.MatchesTagExact(InTag)) {
        return;
    }

    if (UGameStatComponent* OldStatComp = ResolveStatComponent()) {
        if (StatChangedSubscriptionHandle.IsValid()) {
            OldStatComp->UnsubscribeStatChanged(PrimaryVitalTag, StatChangedSubscriptionHandle);
            StatChangedSubscriptionHandle.Reset();
        }
    }

    PrimaryVitalTag = InTag;

    if (UGameStatComponent* StatComp = ResolveStatComponent()) {
        BoundStatComponent = StatComp;
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

/**
 * @brief 按百分比恢复主生命数值并复活（目标值 = Min + (Max - Min) * FillPercent）
 * @param FillPercent 填充比例（0.0 ~ 1.0，默认 1.0f 即回满生命值）
 */
void UGameVitalComponent::ReviveVital(float FillPercent) {
    const AActor* OwnerActor = GetOwner();
    if (OwnerActor != nullptr && !OwnerActor->HasAuthority()) {
        GAME_LOG(Warning, TEXT("【生命组件】非权威端拒绝执行复活。"));
        return;
    }

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

/**
 * @brief 依据关联的数值组件重新评估存活状态并分发生死状态变动
 */
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

/**
 * @brief 存活标志位网络复制到达客户端时的回调函数
 */
void UGameVitalComponent::OnRep_VitalAlive() {
    if (bLastBroadcastedAlive != bVitalAlive) {
        BroadcastVitalStateChanged(bVitalAlive);
    }
}

/**
 * @brief 主生命属性变更后重新评估存活状态
 * @param StatTag 触发回调的属性标签
 * @param OldValue 变化前的有效当前值
 * @param NewValue 变化后的有效当前值
 * @param MaxValue 当前有效上限
 */
void UGameVitalComponent::HandleBoundStatChanged([[maybe_unused]] const FGameplayTag& StatTag, [[maybe_unused]] float OldValue, [[maybe_unused]] float NewValue, [[maybe_unused]] float MaxValue) {
    EvaluateVitalState();
}

/**
 * @brief 向蓝图和原生订阅者广播最新存活状态与主生命标签
 * @param bNewAlive 最新存活状态
 */
void UGameVitalComponent::BroadcastVitalStateChanged(bool bNewAlive) {
    bLastBroadcastedAlive = bNewAlive;
    OnVitalStateChanged.Broadcast(bNewAlive, PrimaryVitalTag);
    OnVitalStateChangedNative.Broadcast(bNewAlive, PrimaryVitalTag);
}

/**
 * @brief 返回显式绑定的数值组件，未绑定时从宿主查找
 * @return 可用的数值组件；无宿主或未发现时返回 nullptr
 */
UGameStatComponent* UGameVitalComponent::ResolveStatComponent() {
    if (BoundStatComponent.IsValid()) {
        return BoundStatComponent.Get();
    }

    if (AActor* OwnerActor = GetOwner()) {
        UGameStatComponent* FoundComponent = OwnerActor->FindComponentByClass<UGameStatComponent>();
        BoundStatComponent = FoundComponent;
        return FoundComponent;
    }

    return nullptr;
}
