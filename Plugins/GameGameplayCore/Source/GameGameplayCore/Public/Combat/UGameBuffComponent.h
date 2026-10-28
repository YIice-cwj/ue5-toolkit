/**
 * @file UGameBuffComponent.h
 * @brief 通用 Buff / 状态核心组件（零 GAS 依赖 · 由开发者自由注册 Buff 类型）
 *
 * 设计思路：
 * 框架层不定义任何具体 Buff（中毒、加速、护盾、冷却等一律由开发者用 GameplayTag 与 Spec 描述），
 * 仅提供一套与 GAS 无关的通用运行时：叠层策略、持续时长、状态标签聚合与属性修正器挂接。
 * 冷却不作为独立硬编码类，而是“带持续时长与状态标签的 Buff”的一种普通用法；
 * 属性修正器通过 FGameStatModifier 叠加到 UGameStatComponent 的派生数值上，移除时精确回滚。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"

#include "Combat/UGameStatComponent.h"

#include "UGameBuffComponent.generated.h"

class UGameBuffComponent;

/**
 * @brief 重复施加同 Tag Buff 时的叠层策略
 */
UENUM(BlueprintType)
enum class EGameBuffStackPolicy : uint8 {
    RefreshDuration UMETA(DisplayName = "刷新持续时长"),
    AddDuration UMETA(DisplayName = "累加持续时长"),
    Independent UMETA(DisplayName = "独立实例")
};

/**
 * @brief 单条属性修正声明（Buff 激活期间挂接到目标属性）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatModifierSpec {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag StatTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameStatModifier Modifier;
};

/**
 * @brief 通用 Buff 描述（开发者自由构造，框架不预置任何具体 Buff）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffSpec {
    GENERATED_BODY()

    /** Buff 身份标签（同一 Tag 视为同一种 Buff，用于叠层与查询） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTag BuffTag;

    /** 激活期间对外聚合的状态标签（如眩晕、无敌、冷却中） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    FGameplayTagContainer GrantedTags;

    /** 持续时长（秒；<= 0 表示永久） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    float Duration = 0.0f;

    /** 最大叠层数（<= 0 表示不限层数） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    int32 MaxStacks = 1;

    /** 重复施加时的叠层策略 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    EGameBuffStackPolicy StackPolicy = EGameBuffStackPolicy::RefreshDuration;

    /** 激活期间挂接到 UGameStatComponent 的属性修正器 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Buff")
    TArray<FGameStatModifierSpec> Modifiers;
};

/**
 * @brief 运行中的 Buff 实例（FastArray 复制条目）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameBuffInstance : public FFastArraySerializerItem {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 InstanceId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    FGameBuffSpec Spec;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    int32 Stacks = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Buff")
    float RemainingSeconds = 0.0f;
};

/**
 * @brief 活动 Buff 的增量复制容器（仅同步变更条目与删除索引）
 */
USTRUCT()
struct GAMEGAMEPLAYCORE_API FGameActiveBuffArray : public FFastArraySerializer {
    GENERATED_BODY()

    UPROPERTY()
    TArray<FGameBuffInstance> Items;

    UPROPERTY(NotReplicated)
    TObjectPtr<UGameBuffComponent> OwnerComponent = nullptr;

    /**
     * @brief 增量复制入口：委托引擎 FastArrayDeltaSerialize 同步变更条目与删除索引
     * @param DeltaParms 增量序列化参数
     * @return 是否成功序列化
     */
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms) {
        return FFastArraySerializer::FastArrayDeltaSerialize<FGameBuffInstance, FGameActiveBuffArray>(Items, DeltaParms, *this);
    }

    /**
     * @brief 客户端收到新增条目后：挂接修正器、刷新 Tick 状态并广播差异
     * @param AddedIndices 新增条目下标
     * @param FinalSize 复制后的条目总数
     */
    void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);

    /**
     * @brief 客户端收到条目变更后：按新叠层重挂修正器、刷新 Tick 状态并广播差异
     * @param ChangedIndices 变更条目下标
     * @param FinalSize 复制后的条目总数
     */
    void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);

    /**
     * @brief 客户端移除前：先回滚被删条目的属性修正器，再广播差异（引擎移除前回调）
     * @param RemovedIndices 待移除条目下标
     * @param FinalSize 移除后的条目总数
     */
    void PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize);
};

template<>
struct TStructOpsTypeTraits<FGameActiveBuffArray> : public TStructOpsTypeTraitsBase2<FGameActiveBuffArray> {
    enum { WithNetDeltaSerializer = true };
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChanged, FGameplayTag, BuffTag, int32, Stacks);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemoved, FGameplayTag, BuffTag);

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnGameBuffChangedNative, const FGameplayTag&, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGameBuffRemovedNative, const FGameplayTag&);

/**
 * @brief 已挂接的属性修正器记录（保留标签以支持精确回滚）
 */
struct FGameBuffAppliedModifier {
    FGameplayTag StatTag;
    uint32 Handle = 0;
};

/**
 * @brief 通用 Buff / 状态组件（非 GAS 项目直接使用；GAS 项目见 GameGameplay 的桥接层）
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameBuffComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /** 构造：启用 Tick 但默认关闭、开启组件复制（无 Buff 时不产生 Tick 开销） */
    UGameBuffComponent();

    /** 每帧推进到期计时：权威端移除到期实例并回滚修正器，非权威端只做本地倒计时 */
    void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** 复制注册：`ActiveBuffs`（FastArray 增量复制） */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /** 初始化 FastArray 的所属组件反向引用（客户端复制回调据此回写修正器） */
    void PostInitProperties() override;

    /**
     * @brief 显式绑定数值组件（不调用时从 Owner 自动查找）
     * @param InStatComponent 目标数值组件
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    void BindStatComponent(UGameStatComponent* InStatComponent);

    /**
     * @brief 施加 Buff（重复施加按 Spec 的叠层策略处理）
     * @param InSpec Buff 描述
     * @param InSource 施加来源（仅用于事件回调，可为空）
     * @return 运行实例 Id，失败返回 0
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    int32 ApplyBuff(const FGameBuffSpec& InSpec, UObject* InSource = nullptr);

    /**
     * @brief 按 Buff 标签移除实例
     * @param InBuffTag Buff 身份标签
     * @param bRemoveAllInstances 是否移除该标签的全部实例
     * @return 是否移除成功
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuff(FGameplayTag InBuffTag, bool bRemoveAllInstances = true);

    /**
     * @brief 按 Buff 标签扣减指定层数（层数归零时自动移除实例）
     * @param InBuffTag Buff 身份标签
     * @param StacksToRemove 扣减层数
     * @return 是否成功扣减
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffStacks(FGameplayTag InBuffTag, int32 StacksToRemove = 1);

    /**
     * @brief 按运行实例 Id 精确移除
     * @param InInstanceId 实例 Id
     * @return 是否移除成功
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Buff")
    bool RemoveBuffInstance(int32 InInstanceId);

    /** 是否存在该标签的 Buff 实例 */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool HasBuff(FGameplayTag InBuffTag) const;

    /** 聚合该标签所有实例的层数之和 */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] int32 GetBuffStacks(FGameplayTag InBuffTag) const;

    /** 该标签所有实例中的最大剩余时长（永久 Buff 恒为 0） */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] float GetBuffRemainingTime(FGameplayTag InBuffTag) const;

    /**
     * @brief 输出当前激活的 Buff 身份标签（去重）
     * @param OutTags 输出容器（内部先 Reset）
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetActiveBuffTags(TArray<FGameplayTag>& OutTags) const;

    /**
     * @brief 聚合当前全部 Buff 授予的状态标签
     * @param OutTags 聚合结果
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    void GetGrantedTags(FGameplayTagContainer& OutTags) const;

    /**
     * @brief 当前是否允许施加/移除 Buff（服务端权威或显式关闭权威校验）
     * @return 允许操作返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|Buff")
    [[nodiscard]] bool IsApplyAllowed() const;

public:
    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffChanged OnBuffChanged;

    UPROPERTY(BlueprintAssignable, Category = "Game|Buff")
    FOnGameBuffRemoved OnBuffRemoved;

    FOnGameBuffChangedNative OnBuffChangedNative;

    FOnGameBuffRemovedNative OnBuffRemovedNative;

private:
    friend struct FGameActiveBuffArray;

    UGameStatComponent* ResolveStatComponent();
    void RegisterInstanceModifiers(FGameBuffInstance& InInstance);
    void UnregisterInstanceModifiers(const FGameBuffInstance& InInstance);
    int32 FindInstanceIndex(const FGameplayTag& InBuffTag) const;
    void RefreshTickState();
    void PublishReplicatedDiff(const TArray<FGameBuffInstance>& InItems, TArrayView<const int32> ExcludedIndices = TArrayView<const int32>());
    void BroadcastTagStateAfterRemoval(const FGameplayTag& InBuffTag);
    void BroadcastBuffChanged(const FGameplayTag& InBuffTag, int32 InStacks);
    void BroadcastBuffRemoved(const FGameplayTag& InBuffTag);

private:
    UPROPERTY(EditDefaultsOnly, Category = "Game|Buff")
    bool bRequireAuthority = true;

    UPROPERTY(Replicated)
    FGameActiveBuffArray ActiveBuffs;

    TMap<int32, TArray<FGameBuffAppliedModifier>> ModifierHandlesByInstance;
    TMap<FGameplayTag, int32> AcknowledgedStacks;
    TWeakObjectPtr<UGameStatComponent> BoundStatComponent;
    int32 NextInstanceId = 1;
};
