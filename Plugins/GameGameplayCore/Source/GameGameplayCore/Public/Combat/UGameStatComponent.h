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
    explicit FGameStatModifier(float InValue, EGameStatModifierOp InOp = EGameStatModifierOp::Add, bool bInApplyToCurrent = true, bool bInApplyToMax = true)
        : Op(InOp), Value(InValue), bApplyToCurrent(bInApplyToCurrent), bApplyToMax(bInApplyToMax) {}

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
    UGameStatComponent();

    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void RegisterStat(const FGameplayTag& InStatTag, float InInitialCurrent, float InMax = 100.0f, float InMin = 0.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool UnregisterStat(const FGameplayTag& InStatTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool HasStat(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void GetAllStatTags(TArray<FGameplayTag>& OutTags) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatMin(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatPercent(const FGameplayTag& InStatTag) const;

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

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveCurrent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectiveMax(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] float GetStatEffectivePercent(const FGameplayTag& InStatTag) const;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    bool ModifyStat(const FGameplayTag& InStatTag, float Delta, float& OutActualDelta);

    bool ModifyStat(const FGameplayTag& InStatTag, float Delta) {
        float UnusedActualDelta = 0.0f;
        return ModifyStat(InStatTag, Delta, UnusedActualDelta);
    }

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatCurrent(const FGameplayTag& InStatTag, float NewCurrent);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetStatMax(const FGameplayTag& InStatTag, float NewMax, bool bKeepRelativePercent = true);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void SetPrimaryVitalTag(const FGameplayTag& InTag);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] FORCEINLINE FGameplayTag GetPrimaryVitalTag() const noexcept {
        return PrimaryVitalTag;
    }

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    [[nodiscard]] bool IsVitalAlive() const noexcept;

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void ReviveVital(float FillPercent = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void BindStatSource(TScriptInterface<IGameStatValueSource> InSource);

    void BindNativeStatQueries(FGameStatQueryDelegate InCurrentQuery, FGameStatQueryDelegate InMaxQuery);

    /**
     * @brief 绑定代理模式外部写委托（未绑定时代理模式禁止任何写操作）
     * @param InWriter 外部写委托
     */
    void BindNativeStatWriter(FGameStatWriteDelegate InWriter);

    UFUNCTION(BlueprintCallable, Category = "Game|Stat")
    void UnbindStatSource();

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
    UFUNCTION()
    void OnRep_ReplicatedStats();

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
