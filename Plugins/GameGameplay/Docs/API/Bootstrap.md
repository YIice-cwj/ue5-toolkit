# GameGameplay GAS 引导组件 API 参考

> **一句话**：把 ASC 握手、数据就绪门、复制重试与默认能力授予收敛到组件内部的 GAS 引导 API。
> **头文件**：`Ability/UGameAbilitySystemBootstrap.h`、`Ability/FBootstrapRetryGate.h`
> **线程**：仅限 GameThread；重试定时器与委托回调均在 GameThread 执行。
> **失败语义**：未绑定委托、空 Avatar、空 ASC 均安全短路；初始化重试超限输出 `Warning` 并中止，自愈失败不抛异常。

## 30 秒速览

- **全时序握手**：解析 ASC → 解析或接收 Avatar → `ASC->InitAbilityActorInfo(EffectiveOwner, Avatar)` → 数据就绪门 → 写初值 → 权威授予默认能力。
- **幂等门**：以 `CurrentASC` 与 `CurrentAvatar` 弱引用短路重复握手；ASC 变化时撤销旧能力并重置状态。
- **数据就绪门**：`FQueryDataReady` 未注入视为恒就绪；注入后返回 `false` 时等待 `NotifyDataReady()` 推进。
- **限次重试**：初始化默认 `60` 次、复制默认 `8` 次，间隔默认 `0.05s`；无 `UWorld` 时复制阶段直接授予，不等待定时器。
- **池化回调**：入池清理定时器、撤销已授予能力并复位全部状态；出池复位重试计数并同步最新配置。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `UGameAbilitySystemBootstrap::EnsureInitialized` | 统一幂等初始化入口，推进握手与时序 | 空 Avatar、委托未绑定或解析为 `nullptr` 直接返回 |
| `UGameAbilitySystemBootstrap::SetResolveAbilitySystemComponentDelegate` | 注入取 ASC 委托 | 必须在引导前注入；后续注入不会被等待 |
| `UGameAbilitySystemBootstrap::SetWriteInitialAttributesDelegate` | 注入写初值委托 | 未注入时跳过写初值，并视为属性阶段完成 |
| `UGameAbilitySystemBootstrap::SetDataReadyQueryDelegate` | 注入数据就绪判定 | 可选；未注入视为恒就绪 |
| `UGameAbilitySystemBootstrap::SetDefaultAbilities` | 配置权威端默认能力类列表 | 空列表直接标记已授予；仅权威端授予 |
| `UGameAbilitySystemBootstrap::ConfigureRetryPolicy` | 调整重试间隔与次数上限 | 间隔下限 `0.01s`、次数下限 `0`；立即同步内部闸门 |
| `UGameAbilitySystemBootstrap::NotifyDataReady` | 唤醒等待数据就绪的引导 | 握手完成前调用无效果 |
| `UGameAbilitySystemBootstrap::TryInitializeFromOwner` | 从组件拥有者解析 Avatar 并兜底自愈 | 超过 `MaxInitRetries`（默认 `60`）输出 `Warning` 并停止 |
| `UGameAbilitySystemBootstrap::IsInitialAttributesWritten` / `IsDefaultAbilitiesGranted` | 查询初值写入与默认能力授予状态 | 无错误返回值，仅反映本组件状态 |
| `FBootstrapRetryGate::Advance` | 推进重试计数并判断是否可重试 | 先自增再比较，超过上限返回 `false` |

## 委托

```cpp
DECLARE_DELEGATE_RetVal(UAbilitySystemComponent*, FResolveAbilitySystemComponent);
DECLARE_DELEGATE_OneParam(FWriteInitialAttributes, UAbilitySystemComponent*);
DECLARE_DELEGATE_RetVal(bool, FQueryDataReady);
```

| 委托 | 签名 | 语义 |
| :--- | :--- | :--- |
| `FResolveAbilitySystemComponent` | `UAbilitySystemComponent*()` | 返回宿主 ASC；返回 `nullptr` 表示尚未就绪 |
| `FWriteInitialAttributes` | `void(UAbilitySystemComponent*)` | 注册属性集并写入初值；未注入时跳过写初值，视为属性阶段完成 |
| `FQueryDataReady` | `bool()` | 返回数据是否就绪；未注入时视为恒就绪 |

取 ASC 与写初值委托须在触发引导前注入；数据就绪委托可选，未注入视为恒就绪。

## `FBootstrapRetryGate`

### 接口

```cpp
struct FBootstrapRetryGate {
    FORCEINLINE void Configure(float InIntervalSeconds, int32 InMaxRetries) noexcept;
    [[nodiscard]] FORCEINLINE bool Advance() noexcept;
    FORCEINLINE void Reset() noexcept;
    [[nodiscard]] FORCEINLINE int32 GetAttempt() const noexcept;
    [[nodiscard]] FORCEINLINE int32 GetMaxRetries() const noexcept;
    [[nodiscard]] FORCEINLINE float GetIntervalSeconds() const noexcept;
};
```

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `Configure` | `InIntervalSeconds`、`InMaxRetries` | `void` | 间隔与次数上限分别钳制为非负；不重置已累计次数 |
| `Advance` | 无 | `bool` | 先自增 `Attempt`，再返回 `Attempt <= MaxRetries` |
| `Reset` | 无 | `void` | 仅把 `Attempt` 归零，保留间隔与上限配置 |
| `GetAttempt` / `GetMaxRetries` / `GetIntervalSeconds` | 无 | `int32` / `int32` / `float` | 状态查询 |

### 线程 / 生命周期 / 失败语义

- 纯 C++ 头文件值类型，无 UObject 生命周期；仅限 GameThread 使用。
- `Configure` 中 `IntervalSeconds = FMath::Max(0.0f, InIntervalSeconds)`，`MaxRetries = FMath::Max(0, InMaxRetries)`。
- `Advance` 超过上限后返回 `false`，调用方应停止调度下一次重试。
- 默认值：间隔 `0.05s`、上限 `0`、累计 `0`；组件会在构造与配置变更时同步闸门参数。

### 最小示例

```cpp
FBootstrapRetryGate Gate;
Gate.Configure(0.05f, 8);
const bool bCanRetry = Gate.Advance();
const float NextDelay = Gate.GetIntervalSeconds();
```

## `UGameAbilitySystemBootstrap`

### 接口

```cpp
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAY_API UGameAbilitySystemBootstrap : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    UGameAbilitySystemBootstrap();

    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void OnAcquireFromPool_Implementation() override;
    void OnReleaseToPool_Implementation() override;

    void SetResolveAbilitySystemComponentDelegate(const FResolveAbilitySystemComponent& InDelegate);
    void SetWriteInitialAttributesDelegate(const FWriteInitialAttributes& InDelegate);
    void SetDataReadyQueryDelegate(const FQueryDataReady& InDelegate);
    void SetDefaultAbilities(TArray<TSubclassOf<UGameplayAbility>> InDefaultAbilities);
    void ConfigureRetryPolicy(float InRetryIntervalSeconds, int32 InMaxInitRetries, int32 InMaxReplicationRetries);

    void EnsureInitialized(AActor* InAvatar);
    void NotifyDataReady();
    void TryInitializeFromOwner();

    [[nodiscard]] bool IsInitialAttributesWritten() const noexcept;
    [[nodiscard]] bool IsDefaultAbilitiesGranted() const noexcept;

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS")
    TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxReplicationRetries = 8;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxInitRetries = 60;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0.01"))
    float RetryIntervalSeconds = 0.05f;
};
```

### 参数与返回

| 方法 | 参数 | 返回 | 说明 |
| :--- | :--- | :--- | :--- |
| `SetResolveAbilitySystemComponentDelegate` | `InDelegate` | `void` | 注入取 ASC 委托，须在引导前完成 |
| `SetWriteInitialAttributesDelegate` | `InDelegate` | `void` | 注入写初值委托，须在引导前完成 |
| `SetDataReadyQueryDelegate` | `InDelegate` | `void` | 注入数据就绪判定；不注入视为恒就绪 |
| `SetDefaultAbilities` | `InDefaultAbilities` | `void` | 配置权威端授予的默认能力类列表 |
| `ConfigureRetryPolicy` | `InRetryIntervalSeconds`、`InMaxInitRetries`、`InMaxReplicationRetries` | `void` | 间隔下限 `0.01s`，次数下限 `0`，立即同步内部闸门 |
| `EnsureInitialized` | `InAvatar` | `void` | 统一幂等初始化入口；服务器与本地客户端均可调用 |
| `NotifyDataReady` | 无 | `void` | 唤醒已完成 ActorInfo 握手且等待数据就绪的引导 |
| `TryInitializeFromOwner` | 无 | `void` | 从组件拥有者解析 Pawn 并兜底自愈 |
| `IsInitialAttributesWritten` | 无 | `bool` | 初值是否已写入 |
| `IsDefaultAbilitiesGranted` | 无 | `bool` | 默认能力是否已授予 |

### 线程 / 生命周期 / 失败语义

- **握手入口**：`EnsureInitialized` 先校验取 ASC 委托已绑定且 `InAvatar != nullptr`，再调用委托解析 ASC；委托未绑定、Avatar 为空或解析结果为 `nullptr` 时直接返回。`InitializeWithResolvedASC` 计算 `EffectiveOwner` 的顺序为 ASC 拥有者 → 组件拥有者 → Avatar，然后调用 `InitAbilityActorInfo(EffectiveOwner, InAvatar)`。
- **幂等与换源**：同一 ASC 且同一 Avatar 重复握手直接返回；ASC 变化时先撤销旧 ASC 上由本组件授予的能力，重置初值/能力标记与重试闸门，并清理复制重试定时器。仅 Avatar 变化时重新握手，但保留已写入初值与已授予状态。
- **权威分流**：`InitAbilityActorInfo` 在权威端与客户端都会执行；`AdvanceAuthoritativeInitialization` 以组件拥有者（缺失时回退 ASC 拥有者）判定权威，非权威端直接返回，不写初值、不授予能力。
- **兜底自愈**：`TryInitializeFromOwner` 先从拥有者解析 Avatar（拥有者为 `APawn` 时取自身，为 `APlayerState` 时取 `GetPawn()`）；非权威且 Avatar 非本地控制时返回。Avatar 或 ASC 为空时调用 `ScheduleInitRetry`，按 `InitRetryGate` 限次调度；超过 `MaxInitRetries` 输出 `Warning` 并停止。
- **数据门与写初值**：`AdvanceAuthoritativeInitialization` 在数据未就绪且 `FQueryDataReady` 已绑定时直接返回；写初值仅在 `FWriteInitialAttributes` 已绑定且尚未写入时执行一次。当属性阶段完成（已写入，或写初值委托未绑定）且默认能力已授予后，再次 `NotifyDataReady` 会在求值数据门前短路。
- **复制重试**：存在 `UWorld` 且 `ASC->IsReadyForReplication()` 为 `false` 时，按 `RetryIntervalSeconds` 重试直至 `MaxReplicationRetries`；超限后输出 `Warning` 并强制授予。无 `UWorld` 时不等待复制，直接授予。
- **默认能力**：空列表直接标记已授予；非空列表逐项跳过空类与已存在的同类能力，成功授予的句柄记录在组件内。
- **生命周期**：构造函数与 `TryInitializeFromOwner()` 都会同步 `ConfigureRetryGates()`，因此宿主在自身 `BeginPlay` 中早于组件 `BeginPlay` 调用时重试上限仍然生效；组件 `BeginPlay` 再次同步配置并调用 `TryInitializeFromOwner`。`EndPlay` 先执行入池清理，再在 `HasBegunPlay()` 为真时调用 `Super::EndPlay`。
- **池化**：`OnReleaseToPool_Implementation` 清理两个定时器、撤销已授予能力、重置弱引用/状态位/闸门；`OnAcquireFromPool_Implementation` 重置闸门累计次数并同步最新配置。
- **失败语义**：所有入口无错误返回值；缺少委托、缺少 Avatar/ASC、非权威端均安全返回，不抛异常、不产生空指针解引用。

### 最小示例

```cpp
BootstrapComponent->SetResolveAbilitySystemComponentDelegate(
    FResolveAbilitySystemComponent::CreateUObject(this, &AHeroCharacter::GetAbilitySystemComponent));

BootstrapComponent->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda(
    [this](UAbilitySystemComponent* InASC) {
        if (AttributeSet == nullptr) {
            AttributeSet = NewObject<UMyHeroAttributeSet>(this, TEXT("AttributeSet"));
            InASC->AddAttributeSetSubobject(AttributeSet);
        }
    }));

BootstrapComponent->EnsureInitialized(this);
```

## 常见错误与边界

- **委托注入晚于引导**：`EnsureInitialized` 与 `TryInitializeFromOwner` 不会等待后续注入；必须在握手前完成 `SetResolveAbilitySystemComponentDelegate` 等注入。
- **空 Avatar 调用**：`EnsureInitialized(nullptr)` 直接返回，不会记录待处理请求；宿主需在 `PossessedBy` / `OnRep_PlayerState` 等有效时机重新调用。
- **非权威端期待写初值**：模拟端只执行 ActorInfo 握手，不会写初值或授予默认能力；相关逻辑只在权威端运行。
- **无 World 且 ASC/Avatar 缺失**：`ScheduleInitRetry` 在没有 `UWorld` 时直接返回，不会创建定时器；离线环境需保证握手时 ASC 与 Avatar 已就绪。
- **握手完成前调用 `NotifyDataReady`**：`CurrentASC` 或 `CurrentAvatar` 为空时不会产生效果，必须先完成 `InitAbilityActorInfo`。
- **池化复用后未重新引导**：入池会撤销能力并清空状态；出池后仍需再次调用 `EnsureInitialized` 才能恢复初值与默认能力。

## 相关文档

- [插件 README](../../README.md)
- [GAS 引导生命周期时序](../Architecture/BootstrapAndLifecycle.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手指南](../Guides/QuickStart.md)
