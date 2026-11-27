/**
 * @file UGameVitalComponent.cpp
 * @brief 实体生命与生死状态管理组件实现
 */

#include "Stat/UGameVitalComponent.h"

#include "GameFramework/Actor.h"
#include "GameGameplayCoreLog.h"
#include "Net/UnrealNetwork.h"
#include "Stat/UGameStatComponent.h"

/**
 * @brief 构造函数：关闭组件 Tick、开启组件网络复制
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
 * @brief 从对象池出库激活回调：补齐监听并重新对齐存活状态
 */
void UGameVitalComponent::OnAcquireFromPool_Implementation() {
    if (!bExplicitlyUnbound && !StatChangedSubscriptionHandle.IsValid() && PrimaryVitalTag.IsValid()) {
        if (UGameStatComponent* StatComp = ResolveStatComponent()) {
            BindStatComponent(StatComp);
            return;
        }
    }
    EvaluateVitalState();
}

/**
 * @brief 归还对象池休眠回调：静默重置本组件存活状态（遵循 SRP，不越权改写数值组件）
 */
void UGameVitalComponent::OnReleaseToPool_Implementation() {
    bVitalAlive = true;
    bLastBroadcastedAlive = true;

    GAME_GAMEPLAY_LOG(
        LogGameVital,
        Verbose,
        TEXT("宿主 [%s] 归还对象池，已静默重置存活状态。"),
        *GetNameSafe(GetOwner())
    );
}

/**
 * @brief 组件初始化生命周期回调：补齐未建立的主生命订阅（支持 CDO 预配置与自动发现 Owner 数值组件）
 */
void UGameVitalComponent::BeginPlay() {
    Super::BeginPlay();

    if (!bExplicitlyUnbound && !StatChangedSubscriptionHandle.IsValid()) {
        if (UGameStatComponent* StatComp = ResolveStatComponent()) {
            BindStatComponent(StatComp);
        }
    }
}

/**
 * @brief 组件销毁生命周期回调：注销与关联数值组件的属性监听委托
 * @param EndPlayReason 结束运行的原因
 */
void UGameVitalComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    UnsubscribeFromBoundStat();
    Super::EndPlay(EndPlayReason);
}

/**
 * @brief 显式绑定关联的数值组件（传入 nullptr 时显式解绑且不再自动回绑 Owner 数值组件）
 * @param InStatComponent 目标数值组件指针；为空时解绑
 */
void UGameVitalComponent::BindStatComponent(UGameStatComponent* InStatComponent) {
    UnsubscribeFromBoundStat();
    BoundStatComponent = InStatComponent;
    bExplicitlyUnbound = (InStatComponent == nullptr);

    if (InStatComponent != nullptr && PrimaryVitalTag.IsValid()) {
        TrySubscribeAndEvaluate();
        GAME_GAMEPLAY_LOG(
            LogGameVital,
            Verbose,
            TEXT("宿主 [%s] 绑定数值组件并监听主生命标签 [%s]。"),
            *GetNameSafe(GetOwner()),
            *PrimaryVitalTag.ToString()
        );
    }
}

/**
 * @brief 设置主生命属性标签并依据关联组件重新评估生死状态
 * @param InTag 目标生命属性标签（例如 Stat.Health）
 */
void UGameVitalComponent::SetPrimaryVitalTag(const FGameplayTag& InTag) {
    if (PrimaryVitalTag == InTag) {
        return;
    }

    UnsubscribeFromBoundStat();
    PrimaryVitalTag = InTag;

    if (!PrimaryVitalTag.IsValid()) {
        EvaluateVitalState();
        GAME_GAMEPLAY_LOG(LogGameVital, Verbose, TEXT("宿主 [%s] 清空主生命标签。"), *GetNameSafe(GetOwner()));
        return;
    }

    if (!bExplicitlyUnbound) {
        if (UGameStatComponent* StatComp = ResolveStatComponent()) {
            BoundStatComponent = StatComp;
            TrySubscribeAndEvaluate();
        }
    }

    GAME_GAMEPLAY_LOG(
        LogGameVital,
        Verbose,
        TEXT("宿主 [%s] 切换主生命标签为 [%s]。"),
        *GetNameSafe(GetOwner()),
        *InTag.ToString()
    );
}

/**
 * @brief 按百分比恢复主生命数值并复活（目标值 = Min + (Max - Min) * FillPercent）
 * @param FillPercent 填充比例（0.0 ~ 1.0 有限浮点，默认 1.0f 即回满生命值）
 */
void UGameVitalComponent::ReviveVital(float FillPercent) {
    const AActor* OwnerActor = GetOwner();
    if (OwnerActor != nullptr && !OwnerActor->HasAuthority()) {
        GAME_GAMEPLAY_LOG_THROTTLE(LogGameVital, 1.0, Warning, TEXT("非权威端拒绝执行复活。"));
        return;
    }

    if (!FMath::IsFinite(FillPercent)) {
        GAME_GAMEPLAY_LOG(LogGameVital, Warning, TEXT("复活失败：FillPercent 为非有限浮点。"));
        return;
    }

    UGameStatComponent* StatComp = ResolveStatComponent();
    if (StatComp == nullptr || !PrimaryVitalTag.IsValid()) {
        GAME_GAMEPLAY_LOG(LogGameVital, Warning, TEXT("复活失败：数值组件未解析或主生命标签无效。"));
        return;
    }

    const TGameStatusResult<FGameStatValue> StatResult = StatComp->GetStatValue(PrimaryVitalTag);
    if (StatResult.IsFailure()) {
        GAME_GAMEPLAY_LOG(
            LogGameVital,
            Warning,
            TEXT("复活失败：主生命属性 [%s] 尚未在数值组件中注册。"),
            *PrimaryVitalTag.ToString()
        );
        return;
    }

    const FGameStatValue& StatValue = StatResult.GetValue();
    if (StatValue.Max <= StatValue.Min) {
        GAME_GAMEPLAY_LOG(
            LogGameVital,
            Warning,
            TEXT("复活失败：主生命标签 [%s] 的上限 <= 下限 (%.2f <= %.2f)。"),
            *PrimaryVitalTag.ToString(),
            StatValue.Max,
            StatValue.Min
        );
        return;
    }

    const float ClampedPercent = FMath::Clamp(FillPercent, 0.0f, 1.0f);
    const float TargetCurrent = StatValue.Min + (StatValue.Max - StatValue.Min) * ClampedPercent;
    StatComp->SetStatCurrent(PrimaryVitalTag, TargetCurrent);
    if (!bVitalAlive) {
        EvaluateVitalState();
    }

    GAME_GAMEPLAY_LOG(
        LogGameVital,
        Verbose,
        TEXT("宿主 [%s] 执行复活: 比例=%.2f, 目标生命=%.2f / %.2f, 当前存活=%s。"),
        *GetNameSafe(GetOwner()),
        ClampedPercent,
        TargetCurrent,
        StatValue.Max,
        bVitalAlive ? TEXT("True") : TEXT("False")
    );
}

/**
 * @brief 依据关联的数值组件重新评估存活状态并分发生死状态变动
 */
void UGameVitalComponent::EvaluateVitalState() {
    if (!PrimaryVitalTag.IsValid()) {
        if (!bVitalAlive) {
            bVitalAlive = true;
            BroadcastVitalStateChanged(true);
            GAME_GAMEPLAY_LOG(LogGameVital, Verbose, TEXT("宿主 [%s] 无主生命标签约束，恢复存活态。"), *GetNameSafe(GetOwner()));
        }
        return;
    }

    UGameStatComponent* StatComp = ResolveStatComponent();
    if (StatComp == nullptr) {
        return;
    }

    const TGameStatusResult<FGameStatValue> StatResult = StatComp->GetStatValue(PrimaryVitalTag);
    if (StatResult.IsFailure()) {
        return;
    }

    const float EffectiveCurrent = StatComp->GetStatEffectiveCurrent(PrimaryVitalTag);
    const float Min = StatResult.GetValue().Min;
    const bool bNewAlive = EffectiveCurrent > Min;

    if (bNewAlive != bVitalAlive) {
        bVitalAlive = bNewAlive;
        BroadcastVitalStateChanged(bVitalAlive);
        GAME_GAMEPLAY_LOG(
            LogGameVital,
            Verbose,
            TEXT("宿主 [%s] 生死状态跃迁: %s (有效值=%.2f, 阈值=%.2f)。"),
            *GetNameSafe(GetOwner()),
            bVitalAlive ? TEXT("存活") : TEXT("阵亡"),
            EffectiveCurrent,
            Min
        );
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
 * @brief 从当前绑定的数值组件注销已登记的属性监听并清理句柄
 */
void UGameVitalComponent::UnsubscribeFromBoundStat() {
    if (StatChangedSubscriptionHandle.IsValid()) {
        if (UGameStatComponent* OldStatComp = BoundStatComponent.Get()) {
            OldStatComp->UnsubscribeStatChanged(SubscribedStatTag, StatChangedSubscriptionHandle);
        }
        StatChangedSubscriptionHandle.Reset();
    }
    SubscribedStatTag = FGameplayTag();
}

/**
 * @brief 在绑定组件与主生命标签均有效时建立属性监听并在必要时评估状态
 */
void UGameVitalComponent::TrySubscribeAndEvaluate() {
    UGameStatComponent* StatComp = BoundStatComponent.Get();
    if (StatComp == nullptr || !PrimaryVitalTag.IsValid()) {
        return;
    }

    SubscribedStatTag = PrimaryVitalTag;
    StatChangedSubscriptionHandle = StatComp->SubscribeStatChanged(
        this,
        PrimaryVitalTag,
        [this](const FGameplayTag& Tag, float OldVal, float NewVal, float MaxVal) {
            HandleBoundStatChanged(Tag, OldVal, NewVal, MaxVal);
        }
    );
}

/**
 * @brief 主生命属性变更后重新评估存活状态
 * @param StatTag 触发回调的属性标签
 * @param OldValue 变化前的有效当前值
 * @param NewValue 变化后的有效当前值
 * @param MaxValue 当前有效上限
 */
void UGameVitalComponent::HandleBoundStatChanged(
    [[maybe_unused]] const FGameplayTag& StatTag,
    [[maybe_unused]] float OldValue,
    [[maybe_unused]] float NewValue,
    [[maybe_unused]] float MaxValue
) {
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
 * @brief 返回显式绑定的数值组件；未显式解绑且未绑定时从宿主自动发现并缓存
 * @return 可用的数值组件；显式解绑、无宿主或未发现时返回 nullptr
 */
UGameStatComponent* UGameVitalComponent::ResolveStatComponent() {
    if (BoundStatComponent.IsValid()) {
        return BoundStatComponent.Get();
    }

    if (bExplicitlyUnbound) {
        return nullptr;
    }

    if (AActor* OwnerActor = GetOwner()) {
        UGameStatComponent* FoundComponent = OwnerActor->FindComponentByClass<UGameStatComponent>();
        BoundStatComponent = FoundComponent;
        return FoundComponent;
    }

    return nullptr;
}
