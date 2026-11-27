# GameGameplayCore 伤害数据类型参考

本文档介绍 `GameGameplayCore` 战斗管线中的核心数据结构。

头文件：
- `#include "Damage/FGameDamageChannel.h"`
- `#include "Damage/FGameDamageContext.h"`
- `#include "Damage/FGameDamageResult.h"`

---

## 1. `FGameDamageChannel` (伤害通道条目)

表示单一类型的伤害或属性数值条目，并提供针对通道切片（`TConstArrayView<FGameDamageChannel>`）与数组的静态辅助算法函数：

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageChannel {
    GENERATED_BODY()

public:
    FGameDamageChannel() = default;
    FGameDamageChannel(const FGameplayTag& InTag, float InMag) : ChannelTag(InTag), Magnitude(InMag) {}

    /** 伤害通道标签（如 Damage.Channel.Physical, Damage.Channel.Fire, Damage.Feedback.HitStop） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTag ChannelTag;

    /** 伤害通道数值大小 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    float Magnitude = 0.0f;

    // --- 针对通道视图与数组的静态辅助函数 ---
    [[nodiscard]] static const FGameDamageChannel* FindExact(TConstArrayView<FGameDamageChannel> InChannels, const FGameplayTag& InTag) noexcept;
    [[nodiscard]] static FGameDamageChannel* FindExactMutable(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag) noexcept;
    [[nodiscard]] static float GetExact(TConstArrayView<FGameDamageChannel> InChannels, const FGameplayTag& InTag, float DefaultValue = 0.0f) noexcept;
    [[nodiscard]] static float GetSum(TConstArrayView<FGameDamageChannel> InChannels, const FGameplayTag& InTag) noexcept;
    [[nodiscard]] static float GetTotalMagnitude(TConstArrayView<FGameDamageChannel> InChannels) noexcept;
    [[nodiscard]] static FGameplayTag GetDominantChannel(TConstArrayView<FGameDamageChannel> InChannels) noexcept;
    static void SetExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InMagnitude);
    static void AddExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag, float InDelta);
    static bool RemoveExact(TArray<FGameDamageChannel>& InChannels, const FGameplayTag& InTag);
};
```

---

## 2. `FGameDamageContext` (伤害输入上下文)

攻击发起方传递给受击目标的参数包。

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageContext {
    GENERATED_BODY()

public:
    FGameDamageContext() = default;

    /** 伤害通道容器（Tag 驱动稀疏通道） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TArray<FGameDamageChannel> Channels;

    /** 物理命中上下文 */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FHitResult HitResult;

    /** 伤害发起者（逻辑主体，如 Controller / Character） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    /** 伤害造成者（物理实体，如子弹、法术投射物、近战武器） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    /** 伤害特质标签（约定标签集，如 Damage.Trait.Crit, Damage.Trait.Backstab，框架零题材假定） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer TraitTags;

    /** 附带上下文标签（如 Environment.Underwater, Attack.Heavy） */
    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer ContextTags;

    // --- 方法 ---
    [[nodiscard]] float GetChannelDamage(const FGameplayTag& InChannelTag) const noexcept;
    [[nodiscard]] float GetChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept;
    void SetChannelDamage(const FGameplayTag& InChannelTag, float InMagnitude);
    void AddChannelDamage(const FGameplayTag& InChannelTag, float InDelta);
    bool RemoveChannel(const FGameplayTag& InChannelTag);
    [[nodiscard]] float GetTotalBaseDamage() const noexcept;
    [[nodiscard]] FGameplayTag GetDominantChannel() const noexcept;
};
```

---

## 3. `FGameDamageResult` (打击反馈结果)

受击方在完成属性结算后，同步返回给攻击发起方的纯粹通用结构体（零特定玩法硬编码）：

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageResult {
    GENERATED_BODY()

public:
    FGameDamageResult() = default;

    /** 实际生效扣除的总伤害数值 */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    float TotalActualDamage = 0.0f;

    /** 各通道实际生效伤害明细 */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> ActualChannels;

    /** 反馈状态标签容器（开发者自由注册：命中、致死、暴击、硬直、格挡、弹反、受击材质等） */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    FGameplayTagContainer FeedbackTags;

    /** 动态数值参数容器（开发者自由注入：卡肉时长、护盾吸收量、架势削弱量等） */
    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> DynamicValues;

    // --- 方法与便捷访问器 ---
    [[nodiscard]] float GetActualChannelDamage(const FGameplayTag& InChannelTag) const noexcept;
    [[nodiscard]] float GetActualChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept;
    void SetActualChannelDamage(const FGameplayTag& InChannelTag, float InMagnitude);
    void AddActualChannelDamage(const FGameplayTag& InChannelTag, float InDelta);
    bool RemoveActualChannel(const FGameplayTag& InChannelTag);
    [[nodiscard]] float GetTotalActualChannelDamage() const noexcept;
    [[nodiscard]] FGameplayTag GetDominantActualChannel() const noexcept;

    [[nodiscard]] FORCEINLINE float GetActualDamage() const noexcept;
    FORCEINLINE void SetActualDamage(float InActualDamage) noexcept;

    [[nodiscard]] FORCEINLINE bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept;
    FORCEINLINE void AddFeedbackTag(const FGameplayTag& InTag);
    FORCEINLINE void RemoveFeedbackTag(const FGameplayTag& InTag);

    [[nodiscard]] float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept;
    void SetDynamicValue(const FGameplayTag& InTag, float InValue);
    [[nodiscard]] bool HasDynamicValue(const FGameplayTag& InTag) const noexcept;
    bool RemoveDynamicValue(const FGameplayTag& InTag);
};
```
