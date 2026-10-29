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
#include "UObject/WeakObjectPtrTemplates.h"

#include "Stat/IGameStatValueSource.h"
#include "Stat/FGameStatTypes.h"
#include "Stat/FGameStatModifierContainer.h"
#include "Stat/FGameStatSubscriptionHub.h"

#include "UGameStatComponent.generated.h"

/**
 * @brief 通用数值管理组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameStatComponent : public UActorComponent {
    GENERATED_BODY()

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
     * @brief 注销属性并清理索引与已确认值
     * @param InStatTag 属性标签
     * @return 属性存在且已成功移除返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool UnregisterStat(const FGameplayTag& InStatTag);

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
     * @brief 读取属性的完整基础数值结构体
     * @param InStatTag 属性标签
     * @param OutValue 输出的数值结构体
     * @return 属性已注册返回 true，未注册返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const;

    /**
     * @brief 注册一条属性修正器（委托至内部 ModifierContainer）
     * @param InStatTag 目标属性标签
     * @param InModifier 修正器规则数据
     * @return 分配的唯一修正器句柄；注册失败返回 0
     */
    uint32 AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    /**
     * @brief 按句柄注销一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InHandle 修正器句柄
     * @return 成功注销返回 true，未找到句柄返回 false
     */
    bool RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle);

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
     * @brief 绑定外部数值源接口并切换到代理模式
     * @param InSource 实现了 IGameStatValueSource 的外部数值源对象接口
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void BindStatSource(TScriptInterface<IGameStatValueSource> InSource);

    /**
     * @brief 绑定原生 C++ 查询委托并切换到代理模式
     * @param InCurrentQuery 读取当前值的委托
     * @param InMaxQuery 读取上限值的委托
     */
    void BindNativeStatQueries(FGameStatQueryDelegate InCurrentQuery, FGameStatQueryDelegate InMaxQuery);

    /**
     * @brief 绑定代理模式下的外部写委托
     * @param InWriter 执行写入操作的委托
     */
    void BindNativeStatWriter(FGameStatWriteDelegate InWriter);

    /**
     * @brief 解绑外部代理源并回落到自持独立模式
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void UnbindStatSource();

    /**
     * @brief 读取当前组件所处的数值来源模式（自持模式或代理模式）
     * @return 当前数值来源枚举
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FORCEINLINE EGameStatSourceMode GetSourceMode() const noexcept {
        return SourceMode;
    }

    /**
     * @brief 查询当前端是否允许执行数值写入操作
     * @return 允许写入返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] bool CanWriteStat() const;

    /**
     * @brief 订阅单个属性的变动通知（原生委托句柄版本）
     * @param InStatTag 目标属性标签
     * @param InDelegate 订阅回调委托
     * @return 委托句柄，用于后续注销
     */
    FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);

    /**
     * @brief 订阅单个属性的变动通知（绑定 Owner 生命周期的 Lambda 闭包版本）
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

    /**
     * @brief 获取内部修正器容器的常量只读引用
     * @return 修正器容器只读引用
     */
    [[nodiscard]] const FGameStatModifierContainer& GetModifierContainer() const noexcept {
        return ModifierContainer;
    }

    /**
     * @brief 获取内部修正器容器的可变引用
     * @return 修正器容器可变引用
     */
    [[nodiscard]] FGameStatModifierContainer& GetModifierContainer() noexcept {
        return ModifierContainer;
    }

    /**
     * @brief 获取内部订阅中心的常量只读引用
     * @return 订阅中心只读引用
     */
    [[nodiscard]] const FGameStatSubscriptionHub& GetSubscriptionHub() const noexcept {
        return SubscriptionHub;
    }

    /**
     * @brief 获取内部订阅中心的可变引用
     * @return 订阅中心可变引用
     */
    [[nodiscard]] FGameStatSubscriptionHub& GetSubscriptionHub() noexcept {
        return SubscriptionHub;
    }

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

protected:
    /**
     * @brief 属性数组网络复制到达客户端时的回调
     */
    UFUNCTION()
    void OnRep_ReplicatedStats();

private:
    [[nodiscard]] int32 ResolveStatIndex(const FGameplayTag& InStatTag) const;
    FGameStatValue* FindMutableStat(const FGameplayTag& InStatTag);
    const FGameStatValue* FindStat(const FGameplayTag& InStatTag) const;
    void SyncEntryToReplicatedList(const FGameplayTag& InStatTag, const FGameStatValue& InValue);
    void RebuildStatIndex();
    void BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue);
    void NotifyIfEffectiveStatChanged(const FGameplayTag& InStatTag, bool bStatRegistered, float OldEffectiveCurrent, float OldEffectiveMax);
    bool ExecuteExternalWrite(const FGameplayTag& InStatTag, EGameStatWriteOp Op, float InValue, float& OutAppliedDelta);
    bool IsAuthoritativeWriteAllowed() const;

private:
    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    EGameStatSourceMode SourceMode = EGameStatSourceMode::Standalone;

    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    bool bAllowNonAuthoritativeWrites = false;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedStats)
    TArray<FGameStatEntry> ReplicatedStats;

    UPROPERTY(Transient)
    TScriptInterface<IGameStatValueSource> ProxySource;

    FGameStatQueryDelegate NativeCurrentQuery;
    FGameStatQueryDelegate NativeMaxQuery;
    FGameStatWriteDelegate NativeStatWriter;

    TMap<FGameplayTag, int32> StatIndex;
    TMap<FGameplayTag, FGameStatValue> AcknowledgedStats;

    FGameStatModifierContainer ModifierContainer;
    FGameStatSubscriptionHub SubscriptionHub;
};
