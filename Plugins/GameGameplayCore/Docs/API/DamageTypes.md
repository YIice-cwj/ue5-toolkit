# GameGameplayCore 伤害数据类型参考

> **一句话**：伤害管线的三组纯数据类型——通道条目、输入上下文与打击反馈结果。
> **头文件**：`Damage/FGameDamageChannel.h`、`Damage/FGameDamageContext.h`、`Damage/FGameDamageResult.h` · **线程**：纯值类型，按 GameThread 调用约定同步组装与结算
> **失败语义**：不返回错误码；未命中通道返回 `0.0f` / `nullptr` / 无效 Tag

## 30 秒速览

- **通道**：`FGameDamageChannel` 是 `Tag + Magnitude` 对，提供针对 `TConstArrayView` 与 `TArray` 的静态查找、求和、增删算法。
- **输入**：`FGameDamageContext` 承载多通道伤害、命中信息、发起者 / 造成者与两组标签，由攻击方组装。
- **反馈**：`FGameDamageResult` 由受击方同步回填实际通道伤害、反馈标签与动态数值。
- **匹配**：`GetChannelDamage` 等按父标签层级求和，`*Exact` 系列只匹配精确标签。
- **数据纯度**：均为 `USTRUCT(BlueprintType)`，无 UObject 所有权，`Instigator` / `DamageCauser` 为弱引用。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FGameDamageChannel::FindExact` | 精确查找通道，返回只读指针 | 无效 Tag 或未命中返回 `nullptr` |
| `FGameDamageChannel::GetExact` | 精确读取单通道数值 | 未命中返回 `DefaultValue` |
| `FGameDamageChannel::GetSum` | 按父标签层级累计通道数值 | 无效 Tag 返回 `0.0f` |
| `FGameDamageChannel::GetDominantChannel` | 取数值最大的有效通道标签 | 空视图或全为非有限值返回无效 Tag |
| `FGameDamageChannel::AddExact` | 精确累加，不存在则追加 | 无效 Tag 为空操作 |
| `FGameDamageContext::GetChannelDamage` | 父标签层级匹配后的通道伤害之和 | 未命中返回 `0.0f` |
| `FGameDamageContext::AddChannelDamage` | 累加或新增输入通道伤害 | 无错误码，无效标签被忽略 |
| `FGameDamageResult::SetActualDamage` | 回填实际生效总伤害 | 字段只读，蓝图侧只能经方法修改 |
| `FGameDamageResult::AddFeedbackTag` | 追加命中 / 免疫等反馈标签 | 无错误码；免疫以 `TotalActualDamage = 0` 表达 |
| `FGameDamageResult::GetDynamicValue` | 精确读取扩展动态数值 | 未命中返回 `DefaultValue` |

## 伤害通道 `FGameDamageChannel`

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageChannel {
    GENERATED_BODY()

public:
    FGameDamageChannel() = default;
    FGameDamageChannel(const FGameplayTag& InTag, float InMag) : ChannelTag(InTag), Magnitude(InMag) {}

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTag ChannelTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Game|Damage")
    float Magnitude = 0.0f;

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

| 静态函数 | 匹配方式 | 返回 / 边界 |
| :--- | :--- | :--- |
| `FindExact` | `MatchesTagExact` | 命中返回只读指针；无效 Tag 或未命中返回 `nullptr` |
| `FindExactMutable` | `MatchesTagExact` | 命中返回可写指针；未命中返回 `nullptr` |
| `GetExact` | `MatchesTagExact` | 命中返回值；未命中返回 `DefaultValue` |
| `GetSum` | `MatchesTag`（父标签含全部子标签） | 匹配通道数值之和；无效 Tag 返回 `0.0f` |
| `GetTotalMagnitude` | 不过滤 | 全部通道原始数值之和 |
| `GetDominantChannel` | 取最大有限值 | 数值最大的有效通道标签；空视图或全为非有限值返回无效 Tag |
| `SetExact` | 精确 | 存在则覆盖，不存在则追加；无效 Tag 为空操作 |
| `AddExact` | 精确 | 存在则累加，不存在则以 `InDelta` 追加；无效 Tag 为空操作 |
| `RemoveExact` | 精确 | 移除全部精确匹配条目；无效 Tag 或未命中返回 `false` |

### 最小示例

```cpp
const FGameplayTag DamagePhysical = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Physical"));
const FGameplayTag DamagePhysicalSlash = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Physical.Slash"));

TArray<FGameDamageChannel> Channels;
FGameDamageChannel::AddExact(Channels, DamagePhysical, 30.0f);
FGameDamageChannel::AddExact(Channels, DamagePhysicalSlash, 12.0f);

const float PhysicalTotal = FGameDamageChannel::GetSum(Channels, DamagePhysical);
const float SlashOnly = FGameDamageChannel::GetExact(Channels, DamagePhysicalSlash);
const FGameplayTag Dominant = FGameDamageChannel::GetDominantChannel(Channels);
```

## 输入上下文 `FGameDamageContext`

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageContext {
    GENERATED_BODY()

public:
    FGameDamageContext() = default;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TArray<FGameDamageChannel> Channels;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FHitResult HitResult;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> Instigator = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    TWeakObjectPtr<AActor> DamageCauser = nullptr;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer TraitTags;

    UPROPERTY(BlueprintReadWrite, Category = "Game|Damage")
    FGameplayTagContainer ContextTags;

    [[nodiscard]] float GetChannelDamage(const FGameplayTag& InChannelTag) const noexcept;
    [[nodiscard]] float GetChannelDamageExact(const FGameplayTag& InChannelTag) const noexcept;
    void SetChannelDamage(const FGameplayTag& InChannelTag, float InMagnitude);
    void AddChannelDamage(const FGameplayTag& InChannelTag, float InDelta);
    bool RemoveChannel(const FGameplayTag& InChannelTag);
    [[nodiscard]] float GetTotalBaseDamage() const noexcept;
    [[nodiscard]] FGameplayTag GetDominantChannel() const noexcept;
};
```

| 字段 | 说明 |
| :--- | :--- |
| `Channels` | Tag 驱动的稀疏伤害通道，支持多通道同时结算 |
| `HitResult` | 底层物理命中上下文（命中点、法线、受击骨骼等） |
| `Instigator` | 伤害发起者（逻辑主体，如 Controller / Character） |
| `DamageCauser` | 伤害造成者（物理实体，如子弹、法术投射物、近战武器） |
| `TraitTags` | 伤害特质标签（如 `Damage.Trait.Crit`、`Damage.Trait.Backstab`） |
| `ContextTags` | 附带环境 / 攻击上下文标签（如 `Environment.Underwater`、`Attack.Heavy`） |

| 方法 | 参数 | 返回 |
| :--- | :--- | :--- |
| `GetChannelDamage` | `InChannelTag` | 父标签层级匹配后的通道伤害之和 |
| `GetChannelDamageExact` | `InChannelTag` | 精确匹配的通道伤害；未命中返回 `0.0f` |
| `SetChannelDamage` | 标签、数值 | `void`；覆盖或新增通道 |
| `AddChannelDamage` | 标签、增量 | `void`；累加或新增通道 |
| `RemoveChannel` | 标签 | 移除成功返回 `true` |
| `GetTotalBaseDamage` | 无 | 全部通道基础伤害总和 |
| `GetDominantChannel` | 无 | 基础伤害最大的通道；无通道返回无效 Tag |

### 最小示例

```cpp
const FGameplayTag DamagePhysical = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Physical"));
const FGameplayTag DamageFire = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Fire"));
const FGameplayTag DamageTraitCrit = FGameplayTag::RequestGameplayTag(TEXT("Damage.Trait.Crit"));

FGameDamageContext Context;
Context.Instigator = GetInstigator();
Context.DamageCauser = this;
Context.AddChannelDamage(DamagePhysical, 35.0f);
Context.AddChannelDamage(DamageFire, 8.0f);
Context.TraitTags.AddTag(DamageTraitCrit);
```

## 打击反馈 `FGameDamageResult`

```cpp
USTRUCT(BlueprintType)
struct GAMEGAMEPLAYCORE_API FGameDamageResult {
    GENERATED_BODY()

public:
    FGameDamageResult() = default;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    float TotalActualDamage = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> ActualChannels;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    FGameplayTagContainer FeedbackTags;

    UPROPERTY(BlueprintReadOnly, Category = "Game|Damage")
    TArray<FGameDamageChannel> DynamicValues;

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

| 字段 | 说明 |
| :--- | :--- |
| `TotalActualDamage` | 实际生效扣除的总伤害，由受击方结算后写入 |
| `ActualChannels` | 各通道实际生效伤害明细 |
| `FeedbackTags` | 反馈状态标签（命中、致死、暴击、硬直、格挡、弹反、受击材质等） |
| `DynamicValues` | 扩展动态数值（卡肉时长、护盾吸收量、架势削弱量等） |

| 方法 | 返回 / 边界 |
| :--- | :--- |
| `GetActualChannelDamage` | 父标签层级匹配的实际通道伤害之和 |
| `GetActualChannelDamageExact` | 精确匹配；未命中 `0.0f` |
| `SetActualChannelDamage` / `AddActualChannelDamage` / `RemoveActualChannel` | 写入 / 累加 / 移除实际通道明细 |
| `GetTotalActualChannelDamage` | `ActualChannels` 全部数值之和 |
| `GetDominantActualChannel` | 最大有效实际通道；无通道返回无效 Tag |
| `GetActualDamage` / `SetActualDamage` | 读写 `TotalActualDamage` |
| `HasFeedbackTag` | 支持父标签层级匹配 |
| `AddFeedbackTag` / `RemoveFeedbackTag` | 增删反馈标签 |
| `GetDynamicValue` | 精确读取动态数值；未命中返回 `DefaultValue` |
| `SetDynamicValue` / `HasDynamicValue` / `RemoveDynamicValue` | 写入 / 查询 / 移除动态数值 |

### 最小示例

```cpp
const FGameplayTag DamagePhysical = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Physical"));
const FGameplayTag FeedbackHitCrit = FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.Hit.Crit"));
const FGameplayTag FeedbackHitStop = FGameplayTag::RequestGameplayTag(TEXT("Damage.Feedback.HitStop"));

FGameDamageResult Result;
Result.SetActualDamage(35.0f);
Result.AddActualChannelDamage(DamagePhysical, 35.0f);
Result.AddFeedbackTag(FeedbackHitCrit);
Result.SetDynamicValue(FeedbackHitStop, 0.08f);

const float HitStop = Result.GetDynamicValue(FeedbackHitStop);
const bool bCrit = Result.HasFeedbackTag(FeedbackHitCrit);
```

## 常见错误与边界

- **精确与层级混用**：`GetSum` / `GetChannelDamage` 会包含子标签，`GetExact` / `GetChannelDamageExact` 只匹配完全相同标签；按父标签统计时不要误用 `*Exact`。
- **无效标签**：查找类函数返回 `nullptr` 或 `0.0f`，`SetExact` / `AddExact` 为空操作，`RemoveExact` 返回 `false`。
- **非有限值**：`GetDominantChannel` 会跳过 `NaN` / `±Inf` 条目；`GetTotalMagnitude` 与 `SetExact` / `AddExact` 不做有限性校验，脏值会直接参与求和。
- **总伤害与通道明细不自动对齐**：`TotalActualDamage` 与 `ActualChannels` 相互独立，受击方需自行保证两者语义一致。
- **只读边界**：`FGameDamageResult` 的字段为 `BlueprintReadOnly`，蓝图侧只能通过方法修改；C++ 可直接写。
- **弱引用**：`Instigator` / `DamageCauser` 不延长 Actor 生命周期，Actor 销毁后自动失效。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [闭环伤害管线](../Architecture/DamagePipeline.md)
- [可受击接口 API](./IDamageable.md)
- [快速上手](../Guides/QuickStart.md)
