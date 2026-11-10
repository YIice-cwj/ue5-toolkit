/**
 * @file UGameStatComponent.h
 * @brief 通用数值管理组件（Tag 驱动 · 任意属性注册 · 动静双轨广播 · 代理读写契约 · 模块化子系统组合）
 *
 * 架构说明：
 * 组件遵循单一职责原则，基础属性存储与网络同步由本组件管理；
 * 修正器演算管线委托至 FGameStatModifierContainer；
 * 按 Tag 订阅与生命周期弱引用委托至 FGameStatSubscriptionHub；
 * 角色生死存活管理请与专职组件 UGameVitalComponent 配合使用。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Types/TGameResult.h"

#include "Stat/FGameStatTypes.h"
#include "Stat/FGameStatAcknowledgedDiff.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/FGameStatSubscriptionHub.h"

#include "UGameStatComponent.generated.h"

/**
 * @brief 通用数值管理组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameStatComponent : public UActorComponent {
    GENERATED_BODY()

    friend class FGameStatReplicationTest;
    friend class FGameStatDefensiveBranchesTest;

public:
    /**
     * @brief 构造函数：关闭组件 Tick、开启组件网络复制
     */
    UGameStatComponent();

    /**
     * @brief 网络属性复制规则注册
     * @param OutLifetimeProps 输出的生命周期属性数组
     */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:
    /**
     * @brief 属性变动动态多播委托
     */
    UPROPERTY(BlueprintAssignable, Category = "Game|Stat")
    FOnGameStatChanged OnStatChanged;

    /**
     * @brief 属性变动原生 C++ 多播委托
     */
    FOnGameStatChangedNative OnStatChangedNative;

public:
    /**
     * @brief 注册（或重设）属性，并立即向既有订阅者广播初始派生值
     * @param InStatTag 属性标签
     * @param InInitialCurrent 初始当前值
     * @param InMax 属性基础上限
     * @param InMin 属性基础下限
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

    /**
     * @brief 注销属性并清理索引与已确认值（蓝图轻量门面）
     * @param InStatTag 属性标签
     * @return 属性存在且已成功移除返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Unregister Stat"))
    bool K2_UnregisterStat(const FGameplayTag& InStatTag);

    /**
     * @brief 注销属性并清理索引与已确认值（强类型契约）
     * @param InStatTag 属性标签
     * @return 属性存在且已成功移除返回 Success，未找到返回 Failure(8001)，非权威端返回 Failure(8002)
     */
    TGameStatusResult<void> UnregisterStat(const FGameplayTag& InStatTag);

    /**
     * @brief 检查属性是否已在组件中注册
     * @param InStatTag 属性标签
     * @return 若已注册返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool HasStat(const FGameplayTag& InStatTag) const;

    /**
     * @brief 输出当前已注册的全部属性标签列表
     * @param OutTags 输出标签数组
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void GetAllStatTags(TArray<FGameplayTag>& OutTags) const;

    /**
     * @brief 读取基础当前值（未注册返回 0.0f）
     * @param InStatTag 属性标签
     * @return 基础当前值数值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatCurrent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 读取基础上限（未注册返回 0.0f）
     * @param InStatTag 属性标签
     * @return 基础上限数值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMax(const FGameplayTag& InStatTag) const;

    /**
     * @brief 获取属性基础下限（未注册返回 0.0f）
     * @param InStatTag 属性标签
     * @return 基础下限数值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMin(const FGameplayTag& InStatTag) const;

    /**
     * @brief 按基础当前值与基础上限计算百分比（钳制在 0.0 ~ 1.0）
     * @param InStatTag 属性标签
     * @return 百分比浮点值（0.0 ~ 1.0）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatPercent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 读取属性的完整基础数值结构体（蓝图轻量门面）
     * @param InStatTag 属性标签
     * @param OutValue 输出的数值结构体
     * @return 属性已注册返回 true，未注册返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat", meta = (DisplayName = "Get Stat Value"))
    bool K2_GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const;

    /**
     * @brief 读取属性的完整基础数值结构体（强类型契约）
     * @param InStatTag 属性标签
     * @return 成功返回数值结构体，未注册返回 Failure(8001)
     */
    [[nodiscard]] TGameStatusResult<FGameStatValue> GetStatValue(const FGameplayTag& InStatTag) const;

    /**
     * @brief 注册一条属性修正器（委托至内部 ModifierContainer）
     * @param InStatTag 目标属性标签
     * @param InModifier 修正器规则数据
     * @return 成功返回分配的唯一句柄，失败返回 Failure
     */
    TGameStatusResult<uint32> AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    /**
     * @brief 按句柄注销一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InHandle 修正器句柄
     * @return 成功注销返回 Success，未找到句柄返回 Failure(8004)
     */
    TGameStatusResult<void> RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle);

    /**
     * @brief 清空指定属性下的全部修正器
     * @param InStatTag 目标属性标签
     * @return 被清除的修正器总数
     */
    int32 ClearStatModifiers(const FGameplayTag& InStatTag);

    /**
     * @brief 读取叠加全部修正器演算后的派生当前值
     * @param InStatTag 目标属性标签
     * @return 计算后的派生当前值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 读取叠加全部修正器演算后的派生上限
     * @param InStatTag 目标属性标签
     * @return 计算后的派生上限
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveMax(const FGameplayTag& InStatTag) const;

    /**
     * @brief 按派生当前值与派生上限计算派生百分比（0.0 ~ 1.0）
     * @param InStatTag 属性标签
     * @return 派生百分比浮点值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectivePercent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 增减属性当前值（蓝图版本，输出实际生效的增减量）
     * @param InStatTag 属性标签
     * @param Delta 期望增减量
     * @param OutActualDelta 实际生效增量（考虑上下限钳制）
     * @return 基础值确实发生变动返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);

    /**
     * @brief C++ 重载便捷方法：增减当前值并忽略实际增量
     * @param InStatTag 属性标签
     * @param Delta 期望增减量
     * @return 基础值确实发生变动返回 true，否则返回 false
     */
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta) {
        float UnusedActualDelta = 0.0f;
        return ModifyStat(InStatTag, Delta, UnusedActualDelta);
    }

    /**
     * @brief 直接设置当前值数值
     * @param InStatTag 属性标签
     * @param NewCurrent 新当前值数值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

    /**
     * @brief 直接设置属性上限
     * @param InStatTag 属性标签
     * @param NewMax 新上限数值
     * @param bKeepRelativePercent 是否按原有百分比同比例缩放当前值
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);

    /**
     * @brief 订阅单个属性的变动通知（原生委托句柄版本）
     * @param InStatTag 目标属性标签
     * @param InDelegate 订阅回调委托
     * @return 委托句柄，用于后续注销
     */
    FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);

    /**
     * @brief 订阅单个属性的变动通知，并仅向本次新订阅者回放当前值（绑定 Owner 生命周期）
     * @param Owner 归属的 UObject 对象指针
     * @param InStatTag 目标属性标签
     * @param InCallback 属性变动时的回调闭包
     * @return 委托句柄，用于后续注销
     */
    FDelegateHandle SubscribeStatChanged(
        UObject* Owner,
        const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback
    );

    /**
     * @brief 按属性标签与句柄取消属性变动订阅
     * @param InStatTag 目标属性标签
     * @param InHandle 注册时返回的委托句柄
     */
    void UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle);

protected:
    /**
     * @brief 属性数组网络复制到达客户端时的回调
     */
    UFUNCTION()
    void OnRep_ReplicatedStats();

private:
    /**
     * @brief 从标签索引表读取属性所在复制数组下标
     * @param InStatTag 目标属性标签
     * @return 属性下标；未注册时返回 INDEX_NONE
     */
    [[nodiscard]] int32 ResolveStatIndex(const FGameplayTag& InStatTag) const;

    /**
     * @brief 查找指定标签的可写基础属性值
     * @param InStatTag 目标属性标签
     * @return 可写属性值指针；未注册时返回 nullptr
     */
    FGameStatValue* FindMutableStat(const FGameplayTag& InStatTag);

    /**
     * @brief 查找指定标签的只读基础属性值
     * @param InStatTag 目标属性标签
     * @return 只读属性值指针；未注册时返回 nullptr
     */
    const FGameStatValue* FindStat(const FGameplayTag& InStatTag) const;

    /**
     * @brief 更新或新增指定属性的复制条目并同步标签索引
     * @param InStatTag 目标属性标签
     * @param InValue 待写入的基础属性值
     */
    void SyncEntryToReplicatedList(const FGameplayTag& InStatTag, const FGameStatValue& InValue);

    /**
     * @brief 按当前复制数组顺序重建标签到下标的映射
     */
    void RebuildStatIndex();

    /**
     * @brief 记录已确认有效值并向动态、原生及按标签订阅者广播属性变化
     * @param InStatTag 目标属性标签
     * @param OldValue 变化前的有效当前值
     * @param NewValue 变化后的有效当前值
     * @param MaxValue 当前有效上限
     */
    void BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue);

    /**
     * @brief 修正器操作后重新计算有效值，并仅在当前值或上限变化时广播
     * @param InStatTag 目标属性标签
     * @param bStatRegistered 操作前属性是否已注册
     * @param OldEffectiveCurrent 操作前的有效当前值
     * @param OldEffectiveMax 操作前的有效上限
     */
    void NotifyIfEffectiveStatChanged(const FGameplayTag& InStatTag, bool bStatRegistered, float OldEffectiveCurrent, float OldEffectiveMax);

    /**
     * @brief 根据宿主网络权限与组件配置判断是否允许写入属性
     * @return 允许写入返回 true，否则返回 false
     */
    bool IsAuthoritativeWriteAllowed() const;

private:
    /**
     * @brief 是否允许非网络权威端写入属性（默认 false，开启后允许客户端预测写入）
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    bool bAllowNonAuthoritativeWrites = false;

    /**
     * @brief 网络复制的属性条目数组
     */
    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedStats)
    TArray<FGameStatEntry> ReplicatedStats;

    /**
     * @brief 属性标签到复制数组下标的快速映射表
     */
    TMap<FGameplayTag, int32> StatIndex;

    /**
     * @brief 属性修正器聚合容器
     */
    FGameStatModifierContainer ModifierContainer;

    /**
     * @brief 按属性标签分发的多播订阅中心
     */
    FGameStatSubscriptionHub SubscriptionHub;

    /**
     * @brief 客户端网络复制到达时的有效数值差分计算器
     */
    FGameStatAcknowledgedDiff AcknowledgedDiff;
};
