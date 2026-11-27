# GameGameplay ASC 与 Globals API 参考

头文件：
- `#include "Ability/UGameAbilitySystemComponent.h"`
- `#include "Ability/UGameAbilitySystemGlobals.h"`
- `#include "Damage/FGameGameplayEffectContext.h"`

---

## 1. `UGameAbilitySystemComponent`

通用能力系统组件基类，继承自 `UAbilitySystemComponent` 并实现 `IGamePoolable`。

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemComponent : public UAbilitySystemComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    UGameAbilitySystemComponent();

    void InitAbilityActorInfo(AActor* InOwnerActor, AActor* InAvatarActor) override;
    void OnAcquireFromPool_Implementation() override;
    void OnReleaseToPool_Implementation() override;
};
```

### 核心特性
- 构造函数开启默认网络复制（`SetIsReplicatedByDefault(true)`）并提前向自身注册 `ActiveGameplayEffects` 与 `ActivatableAbilities` 容器拥有者。
- 重写 `InitAbilityActorInfo`：初始化前自动确保 `AbilityActorInfo` 已分配，杜绝组件未注册时握手触发断言崩溃，并向 `LogGameAbility` 输出日志。
- 实现 `IGamePoolable`：归还对象池时自动取消全部能力、权威端清空已授予能力、活跃效果、持续 `GameplayCue` 与复制标签映射（`MinimalReplicationTags`、`ReplicatedLooseTags`）、完整清空显式阻断能力标签（`BlockedAbilityTags`）与松标签计数并安全清理宿主信息（`ClearActorInfo()`）；出库时重新绑定容器拥有者，彻底杜绝池化 Actor 复用时的状态残留。

---

## 2. `UGameAbilitySystemGlobals`

全局能力系统单例。

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemGlobals : public UAbilitySystemGlobals {
    GENERATED_BODY()

public:
    FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
```

### 核心职责
- 重写 `AllocGameplayEffectContext()`，在引擎底层为所有 GameplayEffect 自动分配扩展的 `FGameGameplayEffectContext` 实例（所有权由引擎 `FGameplayEffectContextHandle` 接管）。
- **项目启用配置**：在工程 `Config/DefaultGame.ini` 中配置：
  ```ini
  [/Script/GameplayAbilities.AbilitySystemGlobals]
  AbilitySystemGlobalsClassName=/Script/GameGameplay.GameAbilitySystemGlobals
  ```

---

## 3. `FGameGameplayEffectContext`

全 Tag 驱动的泛型打击反馈上下文结构体，继承自引擎 `FGameplayEffectContext`，与 `FGameDamageResult` 1:1 对齐。

```cpp
USTRUCT()
struct GAMEGAMEPLAY_API FGameGameplayEffectContext : public FGameplayEffectContext {
    GENERATED_BODY()

public:
    static constexpr int32 MaxNetSerializedEntries = 255;

    [[nodiscard]] static FGameGameplayEffectContext* ExtractEffectContext(const FGameplayEffectContextHandle& InHandle);

    [[nodiscard]] FORCEINLINE float GetActualDamage() const noexcept;
    FORCEINLINE void SetActualDamage(float InActualDamage) noexcept;

    [[nodiscard]] FORCEINLINE float GetActualChannelDamage(const FGameplayTag& ChannelTag) const noexcept;
    FORCEINLINE void SetActualChannelDamage(const FGameplayTag& ChannelTag, float Magnitude);
    [[nodiscard]] FORCEINLINE const TArray<FGameDamageChannel>& GetActualChannels() const noexcept;

    [[nodiscard]] FORCEINLINE bool HasFeedbackTag(const FGameplayTag& InTag) const noexcept;
    FORCEINLINE void AddFeedbackTag(const FGameplayTag& InTag);
    FORCEINLINE void RemoveFeedbackTag(const FGameplayTag& InTag);
    [[nodiscard]] FORCEINLINE const FGameplayTagContainer& GetFeedbackTags() const noexcept;

    [[nodiscard]] FORCEINLINE float GetDynamicValue(const FGameplayTag& InTag, float DefaultValue = 0.0f) const noexcept;
    FORCEINLINE void SetDynamicValue(const FGameplayTag& InTag, float InValue);
    [[nodiscard]] FORCEINLINE bool HasDynamicValue(const FGameplayTag& InTag) const noexcept;
    [[nodiscard]] FORCEINLINE const TArray<FGameDamageChannel>& GetDynamicValues() const noexcept;

    [[nodiscard]] FORCEINLINE const FGameDamageResult& ToDamageResult() const noexcept;

    [[nodiscard]] FORCEINLINE uint16 GetFeedbackEventId() const noexcept;
    FORCEINLINE void SetFeedbackEventId(uint16 InFeedbackEventId) noexcept;

    UScriptStruct* GetScriptStruct() const override;
    FGameplayEffectContext* Duplicate() const override;
    bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess) override;
};
```

### 紧凑网络序列化契约 (`NetSerialize`)
- 采用 **5-bit 位掩码**（`FeedbackEventId != 0`、`TotalActualDamage != 0.0f`、`FeedbackTags`、`ActualChannels`、`DynamicValues`）一次打包，无预测 ID（0）与空字段零载荷传输（空上下文较非零 ID 节省 2 字节）；
- 通道明细与动态数值数组各按上限 255 截断传输，超限时通过 `LogGameDamage` 限频告警；
- 全子项采用独立成功标记严格做逻辑与累积并在 `Ar.IsError()` 时立即中止循环，杜绝后续子项覆写前序反序列化错误。
