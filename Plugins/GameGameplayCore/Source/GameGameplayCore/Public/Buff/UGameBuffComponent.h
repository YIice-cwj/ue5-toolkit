/**
 * @file UGameBuffComponent.h
 * @brief 通用 Buff / 状态核心组件（零 GAS 依赖 · 自由注册 Buff · 叠层策略 · 原子属性挂接 · 状态标签聚合）
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/ArrayView.h"
#include "Containers/TSortedVectorMap.h"
#include "Core/FGameFastTagKey.h"
#include "Core/GameplayCoreErrorCodes.h"
#include "Core/IGamePoolable.h"
#include "GameplayTagContainer.h"
#include "Types/TGameResult.h"

#include "Buff/FGameActiveBuffArray.h"
#include "Buff/FGameBuffModifierBridge.h"
#include "Buff/FGameBuffTypes.h"

#include "UGameBuffComponent.generated.h"

class UGameStatComponent;

/**
 * @brief 通用 Buff / 状态组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameBuffComponent : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

    friend class FGameBuffReplicationCallbacksTest;
    friend class FGameBuffDefensiveBranchesTest;
    friend class FGameBuffReplicatedDiffPerfTest;

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
     * @brief 组件注册生命周期回调：确保 FastArray 反向引用指针始终绑定当前组件实例
     */
    void OnRegister() override;

    /**
     * @brief 组件结束运行生命周期回调：从原数值组件回滚全部 Buff 修正器
     * @param EndPlayReason 结束运行的原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * @brief 从对象池出库激活回调
     */
    void OnAcquireFromPool_Implementation() override;

    /**
     * @brief 归还对象池休眠回调：静默清空所有 Buff 实例并静默回滚修正器（保持实例 ID 单调递增防联机碰撞，零业务广播）
     */
    void OnReleaseToPool_Implementation() override;

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

public:
    /**
     * @brief 显式绑定数值组件，回滚旧组件修正器并将全部活跃 Buff 迁移至新组件（传入 nullptr 时显式解绑且不再自动回绑宿主数值组件）
     * @param InStatComponent 目标数值组件指针；为空时显式解绑并回滚
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    /**
     * @brief 施加 Buff（蓝图轻量门面）
     * @param InSpec Buff 完整描述配置
     * @return 成功生成或刷新的运行实例 ID；若操作被拒绝则返回 0
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Apply Buff"))
    int32 K2_ApplyBuff(const FGameBuffSpec& InSpec);

    /**
     * @brief 施加 Buff（强类型契约）
     * @param InSpec Buff 完整描述配置（Duration 须为有限浮点）
     * @return 成功返回实例 ID；参数非法返回 InvalidParameter，非权威返回 NotAuthority，达叠层上限返回 MaxBuffStacksReached
     */
    TGameStatusResult<int32> ApplyBuff(const FGameBuffSpec& InSpec);

    /**
     * @brief 按 Buff 身份标签移除实例（蓝图轻量门面）
     * @param InBuffTag 目标 Buff 身份标签
     * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
     * @return 成功移除至少一个实例返回 true，未找到匹配实例返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff"))
    bool K2_RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances = true);

    /**
     * @brief 按 Buff 身份标签移除实例（强类型契约）
     * @param InBuffTag 目标 Buff 身份标签
     * @param bRemoveAllInstances 是否移除该标签下的全部实例（若为 false 则仅移除一个实例）
     * @return 成功移除返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
     */
    TGameStatusResult<void> RemoveBuff(const FGameplayTag& InBuffTag, bool bRemoveAllInstances = true);

    /**
     * @brief 按 Buff 身份标签扣减指定层数（蓝图轻量门面）
     * @param InBuffTag 目标 Buff 身份标签
     * @param StacksToRemove 扣减层数（默认 1 层）
     * @return 成功扣减返回 true，实例不存在或扣减失败返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff Stacks"))
    bool K2_RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove = 1);

    /**
     * @brief 按 Buff 身份标签扣减指定层数（强类型契约）
     * @param InBuffTag 目标 Buff 身份标签
     * @param StacksToRemove 扣减层数（默认 1 层）
     * @return 成功扣减返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
     */
    TGameStatusResult<void> RemoveBuffStacks(const FGameplayTag& InBuffTag, int32 StacksToRemove = 1);

    /**
     * @brief 按运行实例 ID 精确移除单个 Buff 实例（蓝图轻量门面）
     * @param InInstanceId 运行实例唯一 ID
     * @return 成功移除返回 true，未找到匹配实例返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Remove Buff Instance"))
    bool K2_RemoveBuffInstance(int32 InInstanceId);

    /**
     * @brief 按运行实例 ID 精确移除单个 Buff 实例（强类型契约）
     * @param InInstanceId 运行实例唯一 ID
     * @return 成功移除返回 Success；参数非法返回 InvalidParameter，非权威返回 NotAuthority，未找到返回 BuffNotFound
     */
    TGameStatusResult<void> RemoveBuffInstance(int32 InInstanceId);

    /**
     * @brief 批量清除当前全部活跃 Buff 并逐标签广播移除事件（蓝图轻量门面）
     * @return 被清除的 Buff 实例总数；非权威被拒时返回 0
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff", meta = (DisplayName = "Clear All Buffs"))
    int32 K2_ClearAllBuffs();

    /**
     * @brief 批量清除当前全部活跃 Buff 并逐标签广播移除事件（强类型契约）
     * @return 成功返回被清除的 Buff 实例总数；非权威端返回 NotAuthority
     */
    TGameStatusResult<int32> ClearAllBuffs();

    /**
     * @brief 查询当前是否存在指定标签的 Buff 实例
     * @param InBuffTag 目标 Buff 标签
     * @return 存在至少一个活跃实例返回 true，否则返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasBuff(const FGameplayTag& InBuffTag) const;

    /**
     * @brief 零拷贝快速查询当前全部活跃 Buff 是否授予了指定状态标签（支持父标签层级匹配）
     * @param InTag 目标状态标签（如 Status.Stun）
     * @return 任一活跃 Buff 的 GrantedTags 包含该标签返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasGrantedTag(const FGameplayTag& InTag) const;

    /**
     * @brief 聚合指定标签所有活跃实例的层数总和
     * @param InBuffTag 目标 Buff 标签
     * @return 聚合后的总层数；无实例时返回 0
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] int32 GetBuffStacks(const FGameplayTag& InBuffTag) const;

    /**
     * @brief 获取指定标签所有实例中的最大剩余时长
     * @param InBuffTag 目标 Buff 标签
     * @return 最大剩余时间（秒）；永久 Buff 或无实例时返回 0.0f
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] float GetBuffRemainingTime(const FGameplayTag& InBuffTag) const;

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
     * @brief 查询当前是否允许写入（施加、移除或清空）Buff（根据宿主网络权限与组件配置判定）
     * @return 允许操作返回 true，非权威且受限时返回 false
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool IsAuthoritativeWriteAllowed() const;

private:
    friend struct FGameActiveBuffArray;

    /**
     * @brief 非权威端执行本地剩余时间倒计时并在全部计时结束后关闭 Tick
     * @param DeltaTime 帧时间间隔（秒）
     */
    void TickSimulatedCountdown(float DeltaTime);

    /**
     * @brief 权威端推进到期计时、批量移除过期实例并广播变更或移除
     * @param DeltaTime 帧时间间隔（秒）
     */
    void TickAuthoritativeExpiration(float DeltaTime);

    /**
     * @brief 在已有同标签 Buff 实例上叠加层数、刷新或累加时长并原子同步修正器
     * @param Existing 待更新的既有实例引用
     * @param InSpec 新施加的 Buff 规格配置
     * @return 更新后的实例 ID
     */
    int32 ApplyToExistingInstance(FGameBuffInstance& Existing, const FGameBuffSpec& InSpec);

    /**
     * @brief 创建全新的 Buff 运行实例、挂接修正器并广播变更
     * @param InSpec 新施加的 Buff 规格配置
     * @return 新分配的实例 ID
     */
    int32 CreateNewInstance(const FGameBuffSpec& InSpec);

    /**
     * @brief 回滚已绑定数值组件上的全部 Buff 修正器，或在组件已失效时清空句柄映射
     */
    void ClearBoundModifiers();

    /**
     * @brief 静默回滚已绑定数值组件上的全部 Buff 修正器（零业务广播，专供对象池归还使用）
     */
    void ClearBoundModifiersSilent();

    /**
     * @brief 返回显式绑定的数值组件；未显式解绑且未绑定时从宿主自动发现并缓存
     * @return 可用的数值组件；显式解绑、无宿主或未发现时返回 nullptr
     */
    UGameStatComponent* ResolveStatComponent();

    /**
     * @brief 将单个 Buff 实例按当前层数原子挂接或原地更新到已解析的数值组件
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
     * @brief 根据排除指定下标后是否仍存在未到期的限时实例启停组件 Tick
     * @param ExcludedIndices 需要排除的待删除条目下标视图（用于 PreReplicatedRemove）
     */
    void RefreshTickState(TConstArrayView<int32> ExcludedIndices = TConstArrayView<int32>());

    /**
     * @brief 对比已确认层数与复制后的条目状态，广播移除及新增、叠层或指定强制变化
     * @param InItems 复制回调当前持有的 Buff 条目
     * @param ExcludedIndices 复制删除前需要从当前状态排除的条目下标
     * @param ForcedChangedTags 即使层数不变也必须广播变更的 Buff 标签
     */
    void PublishReplicatedDiff(
        const TArray<FGameBuffInstance>& InItems,
        TConstArrayView<int32> ExcludedIndices = TConstArrayView<int32>(),
        TConstArrayView<FGameplayTag> ForcedChangedTags = TConstArrayView<FGameplayTag>()
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
     * @brief 是否允许非网络权威端写入 Buff（默认 false，与 UGameStatComponent 权限契约对齐）
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|Buff")
    bool bAllowNonAuthoritativeWrites = false;

    /**
     * @brief 活跃 Buff 的 FastArray 增量网络复制容器
     */
    UPROPERTY(Replicated)
    FGameActiveBuffArray ActiveBuffs;

    /**
     * @brief 客户端已确认并分发的 Buff 层数缓存表（栈内联 8 槽零堆分配 + FastLess 整型二分）
     */
    TSortedVectorMap<FGameFastTagKey, int32, 8> AcknowledgedStacks;

    /**
     * @brief 是否曾显式传入 nullptr 解绑数值组件（阻止后续 ResolveStatComponent 自动回绑宿主数值组件）
     */
    bool bExplicitlyUnbound = false;

    /**
     * @brief 关联的通用数值组件弱引用
     */
    TWeakObjectPtr<UGameStatComponent> BoundStatComponent;

    /**
     * @brief 下一个可用的独立 Buff 实例单调自增 ID
     */
    int32 NextInstanceId = 1;

    /**
     * @brief Buff 属性修正器挂接与回滚桥接器
     */
    FGameBuffModifierBridge ModifierBridge;
};
