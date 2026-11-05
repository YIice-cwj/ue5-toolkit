/**
 * @file UGameVitalComponent.h
 * @brief 实体生命与生死状态管理组件（单一职责 · 负责主生命绑定、存活判定、致死与复活状态机）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "UGameVitalComponent.generated.h"

class UGameStatComponent;

/**
 * @brief 动态多播委托：存活状态或主生命标签发生变动
 * @param bIsAlive 当前是否存活
 * @param VitalTag 触发变动的主生命属性标签
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChanged,
    bool, bIsAlive,
    const FGameplayTag&, VitalTag
);

/**
 * @brief 原生 C++ 多播委托：存活状态或主生命标签发生变动
 * @param bIsAlive 当前是否存活
 * @param VitalTag 触发变动的主生命属性标签引用
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChangedNative,
    bool,
    const FGameplayTag&
);

/**
 * @brief 实体生命与存活状态组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameVitalComponent : public UActorComponent {
    GENERATED_BODY()

    friend class FGameVitalRepAliveForwardBroadcastTest;

public:
    /**
     * @brief 构造函数：开启组件网络复制
     */
    UGameVitalComponent();

    /**
     * @brief 网络属性复制规则注册
     * @param OutLifetimeProps 输出的生命周期属性数组
     */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * @brief 组件初始化生命周期回调：若未显式绑定数值组件，尝试从 Owner 自动发现并挂接
     */
    void BeginPlay() override;

    /**
     * @brief 组件销毁生命周期回调：注销与关联数值组件的属性监听委托
     * @param EndPlayReason 结束运行的原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * @brief 显式绑定关联的数值组件
     * @param InStatComponent 目标数值组件指针
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    /**
     * @brief 设置主生命属性标签并依据关联组件重新评估生死状态
     * @param InTag 目标生命属性标签（例如 Stat.Health）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    /**
     * @brief 读取主生命属性标签
     * @return 当前主生命属性标签
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept {
        return PrimaryVitalTag;
    }

    /**
     * @brief 查询主生命当前是否存活（未设置主生命标签时恒为 true）
     * @return 若存活返回 true，已阵亡返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    [[nodiscard]] FORCEINLINE bool IsVitalAlive() const noexcept {
        return bVitalAlive;
    }

    /**
     * @brief 按百分比恢复主生命数值并复活（目标值 = Min + (Max - Min) * FillPercent）
     * @param FillPercent 填充比例（0.0 ~ 1.0，默认 1.0f 即回满生命值）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void ReviveVital(float FillPercent = 1.0f);

    /**
     * @brief 依据关联的数值组件重新评估存活状态并分发生死状态变动
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Vital")
    void EvaluateVitalState();

protected:
    /**
     * @brief 存活标志位网络复制到达客户端时的回调函数
     */
    UFUNCTION()
    void OnRep_VitalAlive();

private:
    /**
     * @brief 主生命属性变更后重新评估存活状态
     * @param StatTag 触发回调的属性标签
     * @param OldValue 变化前的有效当前值
     * @param NewValue 变化后的有效当前值
     * @param MaxValue 当前有效上限
     */
    void HandleBoundStatChanged(const FGameplayTag& StatTag, float OldValue, float NewValue, float MaxValue);

    /**
     * @brief 向蓝图和原生订阅者广播最新存活状态与主生命标签
     * @param bNewAlive 最新存活状态
     */
    void BroadcastVitalStateChanged(bool bNewAlive);

    /**
     * @brief 返回显式绑定的数值组件，未绑定时从宿主查找
     * @return 可用的数值组件；无宿主或未发现时返回 nullptr
     */
    UGameStatComponent* ResolveStatComponent();

private:
    UPROPERTY(EditDefaultsOnly, Category = "Game|Vital")
    FGameplayTag PrimaryVitalTag;

    UPROPERTY(ReplicatedUsing = OnRep_VitalAlive)
    bool bVitalAlive = true;

    /**
     * @brief 上次广播的存活状态（用于客户端网络复制去重，避免重复触发 OnVitalStateChanged）
     */
    bool bLastBroadcastedAlive = true;

    TWeakObjectPtr<UGameStatComponent> BoundStatComponent;
    FDelegateHandle StatChangedSubscriptionHandle;

public:
    /**
     * @brief 存活状态变动动态多播委托
     */
    UPROPERTY(BlueprintAssignable, Category = "Game|Vital")
    FOnGameVitalStateChanged OnVitalStateChanged;

    /**
     * @brief 存活状态变动原生 C++ 多播委托
     */
    FOnGameVitalStateChangedNative OnVitalStateChangedNative;
};
