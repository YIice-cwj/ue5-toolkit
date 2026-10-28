/**
 * @file UGameStatComponent.h
 * @brief 通用数值管理组件（Tag 驱动 · 任意属性注册 · 动静双轨委托 · 代理读写契约 · 按 Tag 订阅）
 *
 * 设计思路：
 * 彻底消除“生命值”等固定属性硬编码，由开发者通过 GameplayTag 自由注册与管理任意游戏属性
 * （生命、护盾、体力、法力、躯干值、暴击、闪避、冷却等），提供统一的查询、增减、钳制、双轨广播、
 * 修正器叠加、按 Tag 订阅与代理源接入；代理模式默认只读，写入必须显式绑定外部写委托，
 * 杜绝本地缓存与外部数据源之间的读写撕裂。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtrTemplates.h"

#include "Combat/IGameStatValueSource.h"

#include "UGameStatComponent.generated.h"

/**
 * @brief 通用数值配置与实时状态
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatValue {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Current = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Max = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Min = 0.0f;

    /** 百分比（按 [Min, Max] 区间归一到 0~1，区间非法时返回 0） */
    [[nodiscard]] FORCEINLINE float GetPercent() const noexcept {
        const float Range = Max - Min;
        return (Range > 0.0f) ? FMath::Clamp((Current - Min) / Range, 0.0f, 1.0f) : 0.0f;
    }
};

/**
 * @brief 用于网络复制的属性条目
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatEntry {
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Game|Stat")
    FGameplayTag Tag;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Stat")
    FGameStatValue Value;

    FGameStatEntry() = default;

    /**
     * @brief 构造：以标签与数值创建复制条目
     * @param InTag 属性标签
     * @param InValue 属性数值
     */
    FGameStatEntry(const FGameplayTag& InTag, const FGameStatValue& InValue)
        : Tag(InTag), Value(InValue) {}
};

/**
 * @brief 数值源模式
 */
UENUM(BlueprintType)
enum class EGameStatSourceMode : uint8 {
    Standalone UMETA(DisplayName = "自持模式（内部驱动）"),
    Proxy UMETA(DisplayName = "代理模式（外部源驱动）")
};

/**
 * @brief 属性修正操作
 */
UENUM(BlueprintType)
enum class EGameStatModifierOp : uint8 {
    Add UMETA(DisplayName = "加算"),
    Multiply UMETA(DisplayName = "乘算"),
    Override UMETA(DisplayName = "覆写")
};

/**
 * @brief 单条属性修正（Buff / 装备 / 天赋等外部系统注册，叠加于基础值之上）
 */
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameStatModifier {
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    EGameStatModifierOp Op = EGameStatModifierOp::Add;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    float Value = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToCurrent = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Stat")
    bool bApplyToMax = true;

    FGameStatModifier() = default;

    /**
     * @brief 构造：以指定数值与默认加算语义创建修正器
     * @param InValue 修正数值
     * @param InOp 修正操作
     * @param bInApplyToCurrent 是否作用于当前值
     * @param bInApplyToMax 是否作用于最大值
     */
    explicit FGameStatModifier(float InValue, EGameStatModifierOp InOp = EGameStatModifierOp::Add, bool bInApplyToCurrent = true, bool bInApplyToMax = true)
        : Op(InOp), Value(InValue), bApplyToCurrent(bInApplyToCurrent), bApplyToMax(bInApplyToMax) {}

    /**
     * @brief 构造乘算修正器
     * @param InMultiplier 乘算倍率
     * @param bInApplyToCurrent 是否作用于当前值
     * @param bInApplyToMax 是否作用于最大值
     * @return 乘算修正器
     */
    static FGameStatModifier MakeMultiplier(float InMultiplier, bool bInApplyToCurrent = true, bool bInApplyToMax = true) {
        return FGameStatModifier(InMultiplier, EGameStatModifierOp::Multiply, bInApplyToCurrent, bInApplyToMax);
    }
};

/**
 * @brief 代理模式外部写操作类型
 */
UENUM(BlueprintType)
enum class EGameStatWriteOp : uint8 {
    Modify UMETA(DisplayName = "增量修改"),
    SetCurrent UMETA(DisplayName = "设置当前值"),
    SetMax UMETA(DisplayName = "设置上限")
};

DECLARE_DELEGATE_RetVal_OneParam(float, FGameStatQueryDelegate, const FGameplayTag&);

/**
 * @brief 代理模式外部写委托（InStatTag / 写操作 / 写入值 / 实际生效值）
 */
DECLARE_DELEGATE_RetVal_FourParams(bool, FGameStatWriteDelegate, const FGameplayTag&, EGameStatWriteOp, float, float&);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChanged,
    FGameplayTag, StatTag,
    float, OldValue,
    float, NewValue,
    float, MaxValue
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChanged,
    bool, bIsAlive,
    const FGameplayTag&, VitalTag
);

DECLARE_MULTICAST_DELEGATE_FourParams(
    FOnGameStatChangedNative,
    const FGameplayTag&,
    float,
    float,
    float
);

DECLARE_MULTICAST_DELEGATE_TwoParams(
    FOnGameVitalStateChangedNative,
    bool,
    const FGameplayTag&
);

/**
 * @brief 单条已注册修正器（句柄供注册方精确注销）
 */
struct FGameStatModifierEntry {
    uint32 Handle = 0;
    FGameStatModifier Modifier;
};

/**
 * @brief 通用数值管理组件
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAYCORE_API UGameStatComponent : public UActorComponent {
    GENERATED_BODY()

public:
    /** 构造：关闭组件 Tick、开启组件复制 */
    UGameStatComponent();

    /** 复制注册：`ReplicatedStats` 与 `bVitalAlive` 两个字段 */
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /**
     * @brief 注册（或重设）属性，并立即向既有订阅者广播初始派生值
     * @param InStatTag 属性标签
     * @param InInitialCurrent 初始当前值（按 Min/Max 钳制）
     * @param InMax 上限（小于 Min 时取 Min）
     * @param InMin 下限
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

    /**
     * @brief 注销属性并清理索引与已确认值（该标签的修正器表保留，重新注册后仍生效）
     * @param InStatTag 属性标签
     * @return 属性存在且已移除返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool UnregisterStat(const FGameplayTag& InStatTag);

    /** 属性是否已注册 */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool HasStat(const FGameplayTag& InStatTag) const;

    /**
     * @brief 输出当前已注册的全部属性标签（按存储顺序）
     * @param OutTags 输出容器（内部先 Reset）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void GetAllStatTags(TArray<FGameplayTag>& OutTags) const;

    /** 读取当前值（代理模式优先原生查询委托，其次外部源接口；未注册返回 0） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatCurrent(const FGameplayTag& InStatTag) const;

    /** 读取上限（代理模式优先原生查询委托，其次外部源接口；未注册返回 0） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMax(const FGameplayTag& InStatTag) const;

    /**
     * @brief 获取属性下限（代理模式下仍取本地注册值，IGameStatValueSource 契约不含下限）
     * @param InStatTag 属性标签
     * @return 下限数值，未注册返回 0
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMin(const FGameplayTag& InStatTag) const;

    /** 按基础当前值与基础上限计算百分比（区间无效时返回 0） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatPercent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 读取属性基础值（代理模式下 Current/Max 改由代理源提供）
     * @param InStatTag 属性标签
     * @param OutValue 输出值
     * @return 属性已注册返回 true；未注册返回 false 且不写入 OutValue
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool GetStatValue(const FGameplayTag& InStatTag, FGameStatValue& OutValue) const;

    /**
     * @brief 注册一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InModifier 修正规则（加算 / 乘算 / 覆写）
     * @return 修正器句柄，标签非法时返回 0
     */
    uint32 AddStatModifier(const FGameplayTag& InStatTag, const FGameStatModifier& InModifier);

    /**
     * @brief 按句柄注销一条属性修正器
     * @param InStatTag 目标属性标签
     * @param InHandle 修正器句柄
     * @return 是否成功注销
     */
    bool RemoveStatModifier(const FGameplayTag& InStatTag, uint32 InHandle);

    /**
     * @brief 清空指定属性的全部修正器
     * @param InStatTag 目标属性标签
     * @return 被清除的修正器数量
     */
    int32 ClearStatModifiers(const FGameplayTag& InStatTag);

    /** 读取叠加修正器后的派生当前值（有适用修正器时结果不低于本地 Min；无修正器直接返回基础当前值） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const;

    /** 读取叠加修正器后的派生上限（有适用修正器时结果不低于本地 Min；无修正器直接返回基础上限） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveMax(const FGameplayTag& InStatTag) const;

    /** 按派生当前值与派生上限计算百分比（区间无效时返回 0） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectivePercent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 增减当前值（按 Min/Max 钳制，派生值变化时广播；代理模式下转发写委托）
     * @param InStatTag 属性标签
     * @param Delta 增减量（为 0 时直接返回 false）
     * @param OutActualDelta 实际生效增量（被钳制时为钳制后的差值）
     * @return 基础值确实发生变化返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);

    /**
     * @brief C++ 重载便利方法：增减当前值并丢弃实际生效增量
     * @param InStatTag 属性标签
     * @param Delta 增量（负值为扣减）
     * @return 是否实际发生变更
     */
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta) {
        float UnusedActualDelta = 0.0f;
        return ModifyStat(InStatTag, Delta, UnusedActualDelta);
    }

    /** 设置当前值（按 Min/Max 钳制；值未变化则不广播；代理模式下转发写委托） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

    /**
     * @brief 设置上限（按 Min 钳制；与旧值相同则直接返回）
     * @param InStatTag 属性标签
     * @param NewMax 新上限
     * @param bKeepRelativePercent 为 true 时按原百分比等比重算当前值，否则当前值按新上限截断
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);

    /** 设置主生命属性标签并立即重新评估生死状态 */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    /** 读取主生命属性标签（未设置时为空标签） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept {
        return PrimaryVitalTag;
    }

    /** 主生命是否存活（未设置主生命标签时恒为 true） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool IsVitalAlive() const noexcept;

    /**
     * @brief 按百分比恢复主生命数值（未设置主生命标签或 Max ≤ Min 时不生效，写入走 SetStatCurrent 同路径）
     * @param FillPercent 填充比例（自动钳制到 0~1；目标值 = Min + (Max - Min) × FillPercent；默认 1.0 即回满）
     */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void ReviveVital(float FillPercent = 1.0f);

    /** 绑定外部数值源并切换到代理模式（查询走源接口，写入须另行绑定写委托） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void BindStatSource(TScriptInterface<IGameStatValueSource> InSource);

    /** 绑定原生查询委托并切换到代理模式（与源接口可并存，查询时原生委托优先） */
    void BindNativeStatQueries(FGameStatQueryDelegate InCurrentQuery, FGameStatQueryDelegate InMaxQuery);

    /**
     * @brief 绑定代理模式外部写委托（未绑定时代理模式禁止任何写操作）
     * @param InWriter 外部写委托
     */
    void BindNativeStatWriter(FGameStatWriteDelegate InWriter);

    /** 解绑代理源并回落到自持模式（回落前把代理当前值/上限快照进本地属性表） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void UnbindStatSource();

    /** 读取当前数值来源模式（Standalone 自持 / Proxy 代理） */
    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FORCEINLINE EGameStatSourceMode GetSourceMode() const noexcept {
        return SourceMode;
    }

    /**
     * @brief 当前是否允许写入数值（代理模式需绑定写委托；非权威端默认拒绝）
     * @return 允许写入返回 true
     */
    UFUNCTION(BlueprintPure, Category = "Game|Stat")
    [[nodiscard]] bool CanWriteStat() const;

    /**
     * @brief 订阅单个属性的变化（成员函数请用 FOnGameStatChangedNative::FDelegate::CreateUObject）
     * @param InStatTag 目标属性标签
     * @param InDelegate 变化委托（Tag / 旧值 / 新值 / 上限）
     * @return 订阅句柄，属性未注册时仍返回有效句柄（后续注册/变化时触发）
     *
     * 实现思路：订阅时若属性已注册，立即以当前生效值回调一次（旧值等于新值），随后每次变化再回调。
     */
    FDelegateHandle SubscribeStatChanged(const FGameplayTag& InStatTag, FOnGameStatChangedNative::FDelegate InDelegate);

    /**
     * @brief 订阅单个属性的变化（Owner 感知 Lambda 版：Owner 销毁后不再回调，失效条目在下次广播时清理）
     * @param Owner 订阅归属对象（为空表示不绑定生命周期）
     * @param InStatTag 目标属性标签
     * @param InCallback 变化回调（Tag / 旧值 / 新值 / 上限）
     * @return 订阅句柄；Owner 已失效时返回空句柄
     */
    FDelegateHandle SubscribeStatChanged(UObject* Owner, const FGameplayTag& InStatTag,
        TFunction<void(const FGameplayTag&, float, float, float)> InCallback);

    /**
     * @brief 取消按 Tag 订阅
     * @param InStatTag 目标属性标签
     * @param InHandle 订阅句柄
     */
    void UnsubscribeStatChanged(const FGameplayTag& InStatTag, FDelegateHandle InHandle);

public:
    UPROPERTY(BlueprintAssignable, Category = "Game|Stat")
    FOnGameStatChanged OnStatChanged;

    UPROPERTY(BlueprintAssignable, Category = "Game|Stat")
    FOnGameVitalStateChanged OnVitalStateChanged;

    FOnGameStatChangedNative OnStatChangedNative;

    FOnGameVitalStateChangedNative OnVitalStateChangedNative;

protected:
    /** 复制到达回调：重建索引、按 `AcknowledgedStats` 差分广播、清理已移除标签并重评生死 */
    UFUNCTION()
    void OnRep_ReplicatedStats();

    /** 存活位复制到达回调：广播生死状态与主生命标签 */
    UFUNCTION()
    void OnRep_VitalAlive();

private:
    /** Owner 感知订阅条目：标签 + Owner 弱引用 + 委托句柄 */
    struct FStatOwnerSubscription {
        FGameplayTag Tag;
        TWeakObjectPtr<UObject> Owner;
        FDelegateHandle Handle;
    };

    [[nodiscard]] int32 ResolveStatIndex(const FGameplayTag& InStatTag) const;
    FGameStatValue* FindMutableStat(const FGameplayTag& InStatTag);
    const FGameStatValue* FindStat(const FGameplayTag& InStatTag) const;
    void SyncEntryToReplicatedList(const FGameplayTag& InStatTag, const FGameStatValue& InValue);
    void EvaluateVitalState();
    void RebuildStatIndex();
    void BroadcastStatChanged(const FGameplayTag& InStatTag, float OldValue, float NewValue, float MaxValue);
    void NotifyIfEffectiveStatChanged(const FGameplayTag& InStatTag, bool bStatRegistered, float OldEffectiveCurrent, float OldEffectiveMax);
    bool ExecuteExternalWrite(const FGameplayTag& InStatTag, EGameStatWriteOp Op, float InValue, float& OutAppliedDelta);
    bool IsAuthoritativeWriteAllowed() const;
    float EvaluateModifiers(const FGameplayTag& InStatTag, float BaseValue, bool bForMax = false) const;
    void PurgeOwnerSubscriptions(const FGameplayTag& InStatTag);

private:
    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    EGameStatSourceMode SourceMode = EGameStatSourceMode::Standalone;

    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    FGameplayTag PrimaryVitalTag;

    UPROPERTY(EditDefaultsOnly, Category = "Game|Stat")
    bool bAllowNonAuthoritativeWrites = false;

    UPROPERTY(ReplicatedUsing = OnRep_VitalAlive)
    bool bVitalAlive = true;

    UPROPERTY(ReplicatedUsing = OnRep_ReplicatedStats)
    TArray<FGameStatEntry> ReplicatedStats;

    UPROPERTY(Transient)
    TScriptInterface<IGameStatValueSource> ProxySource;

    FGameStatQueryDelegate NativeCurrentQuery;
    FGameStatQueryDelegate NativeMaxQuery;
    FGameStatWriteDelegate NativeStatWriter;

    TMap<FGameplayTag, int32> StatIndex;
    TMap<FGameplayTag, FGameStatValue> AcknowledgedStats;
    TMap<FGameplayTag, TArray<FGameStatModifierEntry>> StatModifiers;
    TMap<FGameplayTag, FOnGameStatChangedNative> TaggedStatChanged;
    TArray<FStatOwnerSubscription> OwnerStatSubscriptions;
    uint32 NextModifierHandle = 1;
};
