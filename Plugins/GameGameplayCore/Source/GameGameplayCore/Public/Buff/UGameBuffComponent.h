/**
 * @file UGameBuffComponent.h
 * @brief 通用 Buff / 状态核心组件（零 GAS 依赖 · 自由注册 Buff · 叠层策略 · 属性挂接 · 状态标签聚合）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "Stat/UGameStatComponent.h"
#include "Buff/FGameBuffTypes.h"
#include "Buff/FGameActiveBuffArray.h"
#include "Buff/FGameBuffModifierBridge.h"

#include "UGameBuffComponent.generated.h"

/**
 * @brief 通用 Buff / 状态组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameBuffComponent : public UActorComponent {
    GENERATED_BODY()

    friend class FGameBuffReplicationCallbacksTest;
    friend class FGameBuffDefensiveBranchesTest;

public:
    /**
     * @brief 构造函数：启用 Tick（默认关闭）、开启组件网络复制
     */
    UGameBuffComponent();

    /**
     * @brief 每帧推进到期计时：权威端移除到期实例并回滚修正器，非权威端仅执行本地倒计时
     * @param DeltaTime 帧时间间隔（秒）
     * @param TickType Tick 类型
     * @param ThisTickFunction Tick 函数指针
     */
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /**
     * @brief 网络属性复制规则注册
     * @param OutLifetimeProps 输出的生命周期属性数组
     */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * @brief 属性初始化后生命周期回调：初始化 FastArray 对本组件的反向引用
     */
    void PostInitProperties() override;

    /**
     * @brief 组件结束运行生命周期回调：从原数值组件回滚全部 Buff 修正器
     * @param EndPlayReason 结束运行的原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * @brief 显式绑定数值组件，回滚旧组件修正器并将全部活跃 Buff 迁移至新组件
     * @param InStatComponent 目标数值组件指针；为空时仅解绑并回滚
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    /**
     * @brief 施加 Buff（重复施加时依据 Spec 预设的叠层策略自动处理）
     * @param InSpec Buff 完整描述配置
     * @param InSource 施加来源对象指针（可空）
     * @return 成功生成或刷新的运行实例 ID；若操作被拒绝则返回 0
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    int32 ApplyBuff(const FGameBuffSpec& InSpec, UObject* InSource = nullptr);

    /**
     * @brief 按 Buff 身份标签移除实例
     * @param InBuffTag 目标 Buff 身份标签
     * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
     * @return 成功移除至少一个实例返回 true，未找到匹配实例返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances = true);

    /**
     * @brief 按 Buff 身份标签扣减指定层数（层数归零时自动移除该实例）
     * @param InBuffTag 目标 Buff 身份标签
     * @param StacksToRemove 扣减层数（默认 1 层）
     * @return 成功扣减返回 true，实例不存在或扣减失败返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove = 1);

    /**
     * @brief 按运行实例 ID 精确移除单个 Buff 实例
     * @param InInstanceId 运行实例唯一 ID
     * @return 成功移除返回 true，未找到匹配实例返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffInstance(int32 InInstanceId);

    /**
     * @brief 查询当前是否存在指定标签的 Buff 实例
     * @param InBuffTag 目标 Buff 标签
     * @return 存在至少一个活跃实例返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasBuff(FGameplayTag InBuffTag) const;

    /**
     * @brief 聚合指定标签所有活跃实例的层数总和
     * @param InBuffTag 目标 Buff 标签
     * @return 聚合后的总层数；无实例时返回 0
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] int32 GetBuffStacks(FGameplayTag InBuffTag) const;

    /**
     * @brief 获取指定标签所有实例中的最大剩余时长
     * @param InBuffTag 目标 Buff 标签
     * @return 最大剩余时间（秒）；永久 Buff 或无实例时返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] float GetBuffRemainingTime(FGameplayTag InBuffTag) const;

    /**
     * @brief 输出当前所有激活中的 Buff 身份标签（去重列表）
     * @param OutTags 输出的标签数组
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const;

    /**
     * @brief 聚合当前全部 Buff 所赋予的状态标签集合（如眩晕、减速）
     * @param OutTags 聚合输出的标签容器
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetGrantedTags(FGameplayTagContainer& OutTags) const;

    /**
     * @brief 查询当前是否允许施加或移除 Buff（根据网络权限控制判定）
     * @return 允许操作返回 true，非权威且受限时返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool IsApplyAllowed() const;

private:
    friend struct FGameActiveBuffArray;

    /**
     * @brief 返回显式绑定的数值组件，未绑定时从宿主发现并缓存
     * @return 可用的数值组件；无宿主或未发现时返回 nullptr
     */
    UGameStatComponent* ResolveStatComponent();

    /**
     * @brief 将单个 Buff 实例按当前层数挂接到已解析的数值组件
     * @param InInstance 目标 Buff 运行实例
     */
    void RegisterInstanceModifiers(const FGameBuffInstance& InInstance);

    /**
     * @brief 按实例 ID 回滚桥接器记录的属性修正器
     * @param InInstance 待回滚的 Buff 运行实例
     */
    void UnregisterInstanceModifiers(const FGameBuffInstance& InInstance);

    /**
     * @brief 查找指定标签的首个活跃 Buff 实例
     * @param InBuffTag 目标 Buff 身份标签
     * @return 首个匹配实例下标；不存在时返回 INDEX_NONE
     */
    int32 FindInstanceIndex(const FGameplayTag& InBuffTag) const;

    /**
     * @brief 根据是否仍存在未到期的限时实例启停组件 Tick
     */
    void RefreshTickState();

    /**
     * @brief 对比已确认层数与复制后的条目状态，广播移除及新增、叠层或指定强制变化
     * @param InItems 复制回调当前持有的 Buff 条目
     * @param ExcludedIndices 复制删除前需要从当前状态排除的条目下标
     * @param ForcedChangedTags 即使层数不变也必须广播变更的 Buff 标签
     */
    void PublishReplicatedDiff(
        const TArray<FGameBuffInstance>& InItems,
        TArrayView<const int32> ExcludedIndices = TArrayView<const int32>(),
        TArrayView<const FGameplayTag> ForcedChangedTags = TArrayView<const FGameplayTag>()
    );

    /**
     * @brief 删除后按标签剩余实例广播层数变化或完全移除
     * @param InBuffTag 发生删除的 Buff 身份标签
     */
    void BroadcastTagStateAfterRemoval(const FGameplayTag& InBuffTag);

    /**
     * @brief 记录已确认层数并向蓝图和原生订阅者广播 Buff 变更
     * @param InBuffTag 发生变化的 Buff 身份标签
     * @param InStacks 变更后的聚合层数
     */
    void BroadcastBuffChanged(const FGameplayTag& InBuffTag, int32 InStacks);

    /**
     * @brief 清除已确认层数并向蓝图和原生订阅者广播 Buff 完全移除
     * @param InBuffTag 已完全移除的 Buff 身份标签
     */
    void BroadcastBuffRemoved(const FGameplayTag& InBuffTag);

private:
    /**
     * @brief 是否强制要求网络权威端才能施加或移除 Buff
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Buff")
    bool bRequireAuthority = true;

    /**
     * @brief 活跃 Buff 的 FastArray 增量网络复制容器
     */
    UPROPERTY(Replicated)
    FGameActiveBuffArray ActiveBuffs;

    /**
     * @brief 客户端已确认并分发的 Buff 层数缓存表（用于复制去重与差分计算）
     */
    TMap<FGameplayTag, int32> AcknowledgedStacks;

    /**
     * @brief 关联的通用数值组件弱引用
     */
    TWeakObjectPtr<UGameStatComponent> BoundStatComponent;

    /**
     * @brief 下一个可用的独立 Buff 实例自增 ID
     */
    int32 NextInstanceId = 1;

    /**
     * @brief Buff 属性修正器挂接与回滚桥接器
     */
    FGameBuffModifierBridge ModifierBridge;

public:
    /**
     * @brief Buff 变更动态多播委托（层数或状态变化时广播）
     */
    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffChanged OnBuffChanged;

    /**
     * @brief Buff 移除动态多播委托（标签完全移除时广播）
     */
    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffRemoved OnBuffRemoved;

    /**
     * @brief Buff 变更原生 C++ 多播委托
     */
    FOnGameBuffChangedNative OnBuffChangedNative;

    /**
     * @brief Buff 移除原生 C++ 多播委托
     */
    FOnGameBuffRemovedNative OnBuffRemovedNative;
};
