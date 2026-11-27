# GameGameplay GAS 引导组件 API 参考

`UGameAbilitySystemBootstrap` 作为 `UActorComponent`（并实现 `IGamePoolable`），将网络握手时序、数据就绪门与复制重试矩阵收敛在框架内部，使业务角色初始化代码保持极简。

头文件：`#include "Ability/UGameAbilitySystemBootstrap.h"`  
基类：`UActorComponent`, `IGamePoolable`

---

## 1. 委托声明

```cpp
DECLARE_DELEGATE_RetVal(UAbilitySystemComponent*, FResolveAbilitySystemComponent);
DECLARE_DELEGATE_OneParam(FWriteInitialAttributes, UAbilitySystemComponent*);
DECLARE_DELEGATE_RetVal(bool, FQueryDataReady);
```

---

## 2. 引导限次重试闸门 `FBootstrapRetryGate`

头文件：`#include "Ability/FBootstrapRetryGate.h"`

纯 C++ 头文件全内联值类型控制闸门（`Configure` 自动钳制非负间隔与非负重试次数上限）：

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

---

## 3. 组件类声明

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

---

## 4. 核心机制

1. **时序幂等门与未绑写初值委托短路**：记录 `CurrentAvatar` 与 `CurrentASC` 弱引用，重复握手短路；即使未注入 `WriteInitialAttributes`，在默认能力授予完毕后 `NotifyDataReady()` 亦直接短路不再重复求值 `DataReadyQuery`。
2. **早于 `BeginPlay` 调用安全**：构造函数与 `TryInitializeFromOwner()` 均自动同步 `ConfigureRetryGates()`，宿主在自身 `BeginPlay` 中早于组件 `BeginPlay` 调用亦能正确按上限重试。
3. **复制就绪重试与无 World 直授**：存在有效 `UWorld` 且 ASC 复制未就绪时，按 `RetryIntervalSeconds` 定时重试直至 `MaxReplicationRetries` 超限强制授予；无 `World` 离线/单测环境下不等待定时器，直接完成能力授予。
4. **对象池 (`IGamePoolable`) 完整闭环**：入池清理定时器、撤销已授予能力句柄并重置所有状态与闸门计数；出池复位重试计数并同步最新重试配置。
