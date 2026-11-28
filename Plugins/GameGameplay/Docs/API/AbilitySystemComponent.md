# GameGameplay ASC 与 Globals API 参考

> **一句话**：GAS 能力组件基类、全局上下文工厂与 Tag 驱动打击反馈上下文的公共 API。
> **头文件**：`Ability/UGameAbilitySystemComponent.h`、`Ability/UGameAbilitySystemGlobals.h`、`Damage/FGameGameplayEffectContext.h`
> **线程**：能力与上下文操作仅限 GameThread；`NetSerialize` 由 GAS 复制链路调用。
> **失败语义**：构造与握手不返回错误；`ExtractEffectContext` 在句柄为空或类型不符时返回 `nullptr`；访问器对缺失数据返回 `0` 或空容器。

## 30 秒速览

- **握手时序**：`UGameAbilitySystemBootstrap` 按「解析 ASC → 解析 Avatar → 调用 `InitAbilityActorInfo(EffectiveOwner, Avatar)`」推进；本类重写保证 `AbilityActorInfo` 已分配后再转交基类。
- **ASC 复制与池化**：构造期调用 `SetIsReplicatedByDefault(true)` 并注册 `ActiveGameplayEffects` / `ActivatableAbilities` 容器拥有者；实现 `IGamePoolable` 复位运行态。
- **全局上下文工厂**：`UGameAbilitySystemGlobals::AllocGameplayEffectContext()` 返回 `FGameGameplayEffectContext`，需在 `DefaultGame.ini` 显式启用。
- **上下文载荷**：反馈标签与动态数值均由 GameplayTag 驱动；网络序列化使用 5-bit 位掩码，通道数组单次上限 `255`。
- **提取失败**：`ExtractEffectContext` 对空句柄或非本工程上下文返回 `nullptr`，调用方必须判空。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameAbilitySystemComponent::InitAbilityActorInfo` | 分配 `AbilityActorInfo` 后完成 Owner / Avatar 握手 | 仅 GameThread；无返回值错误码 |
| `UGameAbilitySystemComponent::OnReleaseToPool_Implementation` | 入池取消能力并清理权威运行态与标签 | 非权威端同样清理标签计数；出池需 `OnAcquireFromPool` 重新绑定容器 |
| `UGameAbilitySystemGlobals::AllocGameplayEffectContext` | 分配 `FGameGameplayEffectContext` 扩展上下文 | 需在 `DefaultGame.ini` 显式启用；所有权由引擎上下文句柄接管 |
| `FGameGameplayEffectContext::ExtractEffectContext` | 从 GE 句柄提取本工程上下文 | 空句柄或类型不符返回 `nullptr`，调用方必须判空 |
| `FGameGameplayEffectContext::NetSerialize` | 基类序列化后按 5-bit 掩码传输扩展载荷 | 通道与动态数值数组单次上限 `255`，超限截断 |
| `FGameGameplayEffectContext::GetActualChannelDamage` | 读取指定通道伤害 | 精确标签匹配；未找到返回 `0.0f` |
| `FGameGameplayEffectContext::HasFeedbackTag` | 判断反馈标签是否存在 | 存在或父标签匹配即返回 `true` |
| `FGameGameplayEffectContext::ToDamageResult` | 取统一打击反馈契约 | 返回内部结构常引用，不复制 |
| `FGameGameplayEffectContext::Duplicate` | 深复制上下文 | 调用方负责删除返回对象 |

## `UGameAbilitySystemComponent`

### 接口

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

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `UGameAbilitySystemComponent()` | 无 | 实例 | 开启默认复制，并把 GE 与能力容器注册到自身拥有者 |
| `InitAbilityActorInfo` | `InOwnerActor`、`InAvatarActor` | `void` | 确保 `AbilityActorInfo` 已分配后调用基类握手，并写 `LogGameAbility` 日志 |
| `OnAcquireFromPool_Implementation` | 无 | `void` | 出池时重新把容器拥有者绑定到当前组件 |
| `OnReleaseToPool_Implementation` | 无 | `void` | 入池时取消能力、清理权威运行态与标签计数、清理宿主信息 |

### 线程 / 生命周期 / 失败语义

- 构造期执行 `SetIsReplicatedByDefault(true)`，并分别对 `ActiveGameplayEffects`、`ActivatableAbilities` 调用 `RegisterWithOwner(this)`。
- `InitAbilityActorInfo` 先检查 `AbilityActorInfo.IsValid()`，为空时通过 `UGameAbilitySystemGlobals::Get().AllocAbilityActorInfo()` 分配，再调用基类实现。
- 入池时始终执行 `CancelAllAbilities()`；仅权威端执行 `ClearAllAbilities()`、`RemoveActiveEffects(FGameplayEffectQuery())`、`RemoveAllGameplayCues()`，并把最小复制标签与复制松标签逐项 `SetTagCount(Tag, 0)`。
- 非权威端与权威端都会清空显式阻断能力标签、已持有松标签，并调用 `ResetTagMap()`；`AbilityActorInfo` 有效时再调用 `ClearActorInfo()`。
- 所有方法均无返回值错误码；握手日志通过 `GetNameSafe` 输出 Owner/Avatar 名称。

### 最小示例

```cpp
AbilitySystemComponent = CreateDefaultSubobject<UGameAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
AbilitySystemComponent->InitAbilityActorInfo(this, this);
```

正常业务接入由 `UGameAbilitySystemBootstrap::EnsureInitialized` 驱动上述握手；直接调用适用于无需数据门与重试编排的场景。

## `UGameAbilitySystemGlobals`

### 接口

```cpp
UCLASS()
class GAMEGAMEPLAY_API UGameAbilitySystemGlobals : public UAbilitySystemGlobals {
    GENERATED_BODY()

public:
    FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
```

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `AllocGameplayEffectContext` | 无 | `FGameplayEffectContext*` | 新建 `FGameGameplayEffectContext`，所有权由引擎 `FGameplayEffectContextHandle` 接管 |

### 线程 / 生命周期 / 失败语义

- `AllocGameplayEffectContext` 是引擎为 GameplayEffect 分配上下文的扩展点，在 GE 施加链路中调用，须位于 GameThread。
- 仅启用插件不会改变引擎默认上下文；必须通过项目配置显式指向本类（opt-in）。
- 返回对象由引擎上下文句柄管理，调用方不得手动释放。

### 最小示例

```ini
[/Script/GameplayAbilities.AbilitySystemGlobals]
AbilitySystemGlobalsClassName=/Script/GameGameplay.GameAbilitySystemGlobals
```

## `FGameGameplayEffectContext`

### 接口

```cpp
USTRUCT()
struct GAMEGAMEPLAY_API FGameGameplayEffectContext : public FGameplayEffectContext {
    GENERATED_BODY()

public:
    static constexpr int32 MaxNetSerializedEntries = 255;

    FGameGameplayEffectContext() = default;

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

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `ExtractEffectContext` | `InHandle` | `FGameGameplayEffectContext*` | 句柄为空或类型不匹配返回 `nullptr` |
| `GetActualDamage` / `SetActualDamage` | `InActualDamage`（写入） | `float` / `void` | 实际生效总伤害 |
| `GetActualChannelDamage` | `ChannelTag` | `float` | 精确标签匹配；未找到返回 `0.0f` |
| `SetActualChannelDamage` | `ChannelTag`、`Magnitude` | `void` | 写入或覆盖指定通道伤害 |
| `GetActualChannels` | 无 | `const TArray<FGameDamageChannel>&` | 全部通道伤害明细 |
| `HasFeedbackTag` | `InTag` | `bool` | 存在或父标签匹配返回 `true` |
| `AddFeedbackTag` / `RemoveFeedbackTag` | `InTag` | `void` | 增删反馈标签 |
| `GetFeedbackTags` | 无 | `const FGameplayTagContainer&` | 全部反馈标签 |
| `GetDynamicValue` | `InTag`、`DefaultValue = 0.0f` | `float` | 精确标签匹配；未找到返回默认值 |
| `SetDynamicValue` | `InTag`、`InValue` | `void` | 写入或覆盖动态数值 |
| `HasDynamicValue` | `InTag` | `bool` | 是否存在对应动态数值 |
| `GetDynamicValues` | 无 | `const TArray<FGameDamageChannel>&` | 全部动态数值 |
| `ToDamageResult` | 无 | `const FGameDamageResult&` | 统一打击反馈契约常引用 |
| `GetFeedbackEventId` / `SetFeedbackEventId` | `InFeedbackEventId`（写入） | `uint16` / `void` | 跨端事件 ID；`0` 表示无预测身份 |
| `GetScriptStruct` | 无 | `UScriptStruct*` | 返回本结构体反射信息 |
| `Duplicate` | 无 | `FGameplayEffectContext*` | 深复制，调用方负责删除 |
| `NetSerialize` | `Ar`、`Map`、`bOutSuccess` | `bool` | 基类序列化后按 5-bit 掩码传输扩展载荷 |

### 线程 / 生命周期 / 失败语义

- 载荷统一存放在 `FGameDamageResult DamageResult` 与 `uint16 FeedbackEventId` 中，与 `FGameDamageResult` 对齐；`ToDamageResult()` 直接返回该结构的常引用。所有访问器为头文件内联，`TStructOpsTypeTraits` 启用 `WithNetSerializer` 与 `WithCopy`。
- `NetSerialize` 先执行基类序列化，再用 5-bit 位掩码按需传输事件 ID、总伤害、反馈标签、通道明细与动态数值；空字段不写载荷。
- 事件 ID 为 `0` 时省略 `uint16` 写入；相比始终写入非零事件 ID 的实现，约省 2 字节（历史参考值，实际收益随字段与归档状态变化）。
- 通道明细与动态数值数组按 `255` 截断；超限时通过 `LogGameDamage` 限频告警。
- 各子项使用独立成功标记做逻辑与累积；`Ar.IsError()` 时立即中止，避免后续子项覆盖前序错误。
- `ExtractEffectContext` 通过 `GetScriptStruct()->IsChildOf(FGameGameplayEffectContext::StaticStruct())` 判定类型，失败返回 `nullptr`。

### 最小示例

```cpp
if (FGameGameplayEffectContext* Context = FGameGameplayEffectContext::ExtractEffectContext(EffectContextHandle)) {
    Context->SetActualDamage(42.0f);
    Context->AddFeedbackTag(HitFeedbackTag);
    const float HitStopSeconds = Context->GetDynamicValue(HitStopTag);
}
```

## 常见错误与边界

- **未启用 Globals**：只安装插件不会替换 GE 上下文；缺少 `DefaultGame.ini` 配置时引擎仍分配原生 `FGameplayEffectContext`，`ExtractEffectContext` 返回 `nullptr`。
- **空句柄或原生上下文**：`ExtractEffectContext` 不做类型转换兜底，调用前必须判空并确认上下文来自本工程。
- **通道查询标签不一致**：`GetActualChannelDamage` 与 `SetActualChannelDamage` 均按精确标签匹配，写入与读取必须使用同一通道 Tag。
- **超限截断**：单次复制超过 `255` 条通道或动态数值会被截断，业务层不应依赖超限条目跨端到达。
- **所有权边界**：`AllocGameplayEffectContext` 返回值由引擎句柄管理；`Duplicate` 返回值由调用方删除。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [GAS 引导生命周期时序](../Architecture/BootstrapAndLifecycle.md)
- [快速上手指南](../Guides/QuickStart.md)
