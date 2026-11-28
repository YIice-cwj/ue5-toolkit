# GameCore 状态机与优先级仲裁 API 参考

> **一句话**：强类型有限状态机与多源属性优先级仲裁器的公共 API。
> **头文件**：`State/TGameStateMachine.h`、`State/TPriorityArbitrator.h` · **线程**：两者均非线程安全，需由持有线程串行驱动 · **失败语义**：`ChangeState` 在未运行、同状态或队列溢出时返回 `false`；链式跃迁超过 `MAX_CHAINED_TRANSITIONS` 会截断并置 `WasTransitionChainTruncated()`

## 30 秒速览

- **状态机**：`TGameStateMachine<TStateEnum>` 以 C++20 `concept CGameStateEnum` 约束枚举，零 UObject 依赖。
- **重入拉平**：回调内再次 `ChangeState` 会进入内联 FIFO 队列，由外层循环顺序消费，不递归展开调用栈。
- **熔断上限**：`MAX_CHAINED_TRANSITIONS = 8`，超出后中断并输出 `LogGameStateMachine` 警告。
- **仲裁模式**：`TPriorityArbitrator<T>` 支持最高优先级覆盖、多源累加、多源累乘三种求值方式。
- **确定性**：累加与累乘按 `FNameLexicalLess` 字典序稳定执行；最高优先级平局按最近一次 `SetChannel` 的序号决胜。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `TGameStateMachine::RegisterState` | 注册或覆盖状态回调 | 非线程安全；覆盖该状态的 `OnEnter` / `OnUpdate` / `OnExit` |
| `TGameStateMachine::AddTransition` | 注册自动转换规则 | 空守卫或自转移被忽略；按优先级降序插入 |
| `TGameStateMachine::Start` | 启动状态机并触发初始 `OnEnter` | 若已在运行先 `Stop()` |
| `TGameStateMachine::ChangeState` | 请求切换状态 | 未运行、同状态或队列满返回 `false`；超 8 条链截断 |
| `TGameStateMachine::Update` | 求值守卫、跃迁并执行 `OnUpdate` | 新状态当帧生效；仅 `DeltaSeconds > 0` 累加驻留时长 |
| `TGameStateMachine::Reset` | 清空状态、规则、观察者与运行态 | 可选先触发当前状态 `OnExit` |
| `TPriorityArbitrator::SetChannel` | 注册或更新通道值与优先级 | 非线程安全；更新已有通道会刷新 `Sequence` |
| `TPriorityArbitrator::GetEffectiveValue` | 按模式计算最终值 | `DefaultValue` 兼作空集回退值与累加/累乘基准 |
| `TPriorityArbitrator::GetWinningChannel` | 取最高优先级胜出通道 | 无通道返回 `NAME_None` |
| `TPriorityArbitrator::SetMode` | 切换仲裁模式 | 切换使缓存失效；`InPriority` 仅在覆盖模式参与仲裁 |

## 有限状态机 `TGameStateMachine<TStateEnum>`

头文件：`#include "State/TGameStateMachine.h"`

```cpp
template <typename T>
concept CGameStateEnum = std::is_enum_v<T>;

template <CGameStateEnum TStateEnum>
class TGameStateMachine {
public:
    using StateType = TStateEnum;
    using FEnterCallback = TFunction<void(StateType PreviousState)>;
    using FUpdateCallback = TFunction<void(float DeltaSeconds)>;
    using FExitCallback = TFunction<void(StateType NextState)>;
    using FTransitionGuard = TFunction<bool()>;
    using FStateChangedObserver = TFunction<void(StateType PreviousState, StateType NewState)>;

    struct FStateCallbacks {
        FEnterCallback OnEnter;
        FUpdateCallback OnUpdate;
        FExitCallback OnExit;
    };

    static constexpr int32 MAX_CHAINED_TRANSITIONS = 8;

    void RegisterState(StateType State, FStateCallbacks Callbacks);
    void AddTransition(StateType FromState, StateType ToState, FTransitionGuard Guard, int32 Priority = 0);
    void Start(StateType InitialState);
    void Stop();
    void Update(float DeltaSeconds);
    bool ChangeState(StateType NewState);

    [[nodiscard]] bool HasState(StateType State) const noexcept;
    [[nodiscard]] int32 GetStateCount() const noexcept;
    int32 GetRegisteredStates(TArray<StateType>& OutStates) const;
    [[nodiscard]] int32 GetTransitionCount(StateType State) const noexcept;
    void ClearTransitions(StateType State);
    void SetOnStateChanged(FStateChangedObserver InObserver);
    void Reset(bool bFireExitIfRunning = false);

    [[nodiscard]] StateType GetCurrentState() const noexcept;
    [[nodiscard]] StateType GetPreviousState() const noexcept;
    [[nodiscard]] float GetTimeInCurrentState() const noexcept;
    [[nodiscard]] bool IsRunning() const noexcept;
    [[nodiscard]] bool WasTransitionChainTruncated() const noexcept;
};
```

<details>
<summary>展开方法语义</summary>

| 接口 | 参数 | 返回 / 行为 |
| :--- | :--- | :--- |
| `RegisterState` | 状态、`FStateCallbacks` | 注册或覆盖该状态的 `OnEnter` / `OnUpdate` / `OnExit` |
| `AddTransition` | 源状态、目标状态、守卫、优先级 | 按优先级降序插入规则；空守卫或自转移被忽略 |
| `Start` | 初始状态 | 若已在运行先 `Stop()`；触发初始态 `OnEnter` 并消费重入队列 |
| `Stop` | 无 | 置停止态并触发当前状态 `OnExit`；停止态下重复调用为空操作 |
| `Update` | `DeltaSeconds` | 先按规则快照求值守卫并跃迁，再累加驻留时长并执行 `OnUpdate` |
| `ChangeState` | 目标状态 | 未运行、同状态或队列溢出返回 `false`；重入中入队后返回 `true` |
| `HasState` / `GetStateCount` / `GetRegisteredStates` | 状态或输出数组 | 查询已注册回调的状态 |
| `GetTransitionCount` / `ClearTransitions` | 源状态 | 查询或清除该状态的自动转换规则 |
| `SetOnStateChanged` | 观察者 | 每次跃迁完成后回调 `(PreviousState, NewState)` |
| `Reset` | 是否先触发 `OnExit` | 清空状态、规则、观察者与运行态 |
| `GetCurrentState` / `GetPreviousState` / `GetTimeInCurrentState` | 无 | 当前态、前一态与当前态驻留秒数 |
| `IsRunning` / `WasTransitionChainTruncated` | 无 | 运行态 / 最近一次跃迁是否被熔断截断 |

</details>

- **线程**：非线程安全，应在同一线程驱动 `Update` / `ChangeState`。
- **生命周期**：`Start` 前状态机为停止态；`Reset` 清空全部注册与运行态。
- **跃迁时序**：`Update` 中守卫触发跃迁时依次执行旧态 `OnExit(新态)` → 新态 `OnEnter(旧态)` → 观察者 → 新态 `OnUpdate(DeltaSeconds)`，新状态当帧生效。
- **驻留计时**：跃迁时 `TimeInCurrentState` 归零；`Update` 仅在 `DeltaSeconds > 0` 时累加。
- **失败语义**：未 `Start`、目标等于当前态、或重入队列已满时 `ChangeState` 返回 `false`；仅队列溢出会置 `WasTransitionChainTruncated()`。
- **回调复制**：状态机在调用前复制回调与规则快照，回调内增删规则不会使迭代器失效。

```cpp
enum class EPlayerStance : uint8 {
    Idle,
    Sprinting
};

TGameStateMachine<EPlayerStance> StanceFsm;
float Stamina = 100.0f;

StanceFsm.RegisterState(EPlayerStance::Idle, {
    [](EPlayerStance Prev) {},
    [](float DeltaTime) {},
    [](EPlayerStance Next) {}
});

StanceFsm.RegisterState(EPlayerStance::Sprinting, {
    [](EPlayerStance Prev) {},
    [&Stamina](float DeltaTime) { Stamina -= DeltaTime * 20.0f; },
    [](EPlayerStance Next) {}
});

StanceFsm.AddTransition(EPlayerStance::Sprinting, EPlayerStance::Idle, [&Stamina]() {
    return Stamina <= 0.0f;
}, 10);

StanceFsm.Start(EPlayerStance::Idle);
StanceFsm.ChangeState(EPlayerStance::Sprinting);

void Tick(float DeltaSeconds) {
    StanceFsm.Update(DeltaSeconds);
    if (StanceFsm.GetTimeInCurrentState() > 5.0f) {
        // 当前姿态已持续超过 5 秒
    }
}
```

## 优先级仲裁 `TPriorityArbitrator<T>`

头文件：`#include "State/TPriorityArbitrator.h"`

```cpp
enum class EPriorityArbitrationMode : uint8 {
    OverrideHighestPriority,
    Additive,
    Multiplicative
};

template <typename T>
class TPriorityArbitrator {
public:
    using ValueType = T;

    constexpr explicit TPriorityArbitrator(
        EPriorityArbitrationMode InMode = EPriorityArbitrationMode::OverrideHighestPriority
    ) noexcept;

    [[nodiscard]] constexpr EPriorityArbitrationMode GetMode() const noexcept;
    void SetMode(EPriorityArbitrationMode InMode) noexcept;

    void SetChannel(const FName& ChannelName, const ValueType& InValue, int32 InPriority = 0);
    bool ClearChannel(const FName& ChannelName);
    [[nodiscard]] bool HasChannel(const FName& ChannelName) const noexcept;
    void GetChannelNames(TArray<FName>& OutChannelNames) const;
    [[nodiscard]] TOptional<ValueType> GetChannelValue(const FName& ChannelName) const;
    [[nodiscard]] TOptional<int32> GetChannelPriority(const FName& ChannelName) const;

    [[nodiscard]] ValueType GetEffectiveValue(const ValueType& DefaultValue) const;
    [[nodiscard]] ValueType GetEffectiveValue(
        const ValueType& DefaultValue,
        EPriorityArbitrationMode Mode
    ) const;
    [[nodiscard]] FName GetWinningChannel() const noexcept;

    void Reset();
    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
};
```

| 接口 | 说明 |
| :--- | :--- |
| `SetChannel` | 注册或更新通道值、优先级；更新已有通道会刷新 `Sequence` |
| `ClearChannel` / `HasChannel` | 移除通道 / 查询通道是否激活 |
| `GetChannelNames` | 输出当前全部活跃通道名 |
| `GetChannelValue` / `GetChannelPriority` | 可选语义查询，通道不存在返回未设置 |
| `GetEffectiveValue` | 按配置模式或显式模式计算最终值；`DefaultValue` 同时作为空集回退值与累加/累乘基准 |
| `GetWinningChannel` | 最高优先级模式下的胜出通道；无通道返回 `NAME_None` |
| `SetMode` / `GetMode` | 切换或读取仲裁模式，切换会使缓存失效 |
| `Reset` / `Num` / `IsEmpty` | 清空通道 / 活跃通道数 / 是否为空 |

- **线程**：非线程安全；内部胜出缓存与聚合缓存均为可变状态，不可并发调用。
- **覆盖模式**：比较 `Priority` 取最高值；平局时比较 `Sequence`，最后写入者胜出。
- **累加/累乘**：以 `DefaultValue` 为基准，通道按 `FNameLexicalLess` 升序依次运算，保证跨平台结果一致。
- **缓存条件**：值类型满足 `std::equality_comparable` 且通道、模式与 `DefaultValue` 未变化时直接返回缓存结果。
- **优先级语义**：`InPriority` 仅在 `OverrideHighestPriority` 模式下参与仲裁。
- **内存**：排序与指针缓存使用 `TInlineAllocator<16>`，通道数不超过 16 时这部分不额外分配堆内存；通道存储本身仍在 `TMap` 中。
- **性能**：缓存命中时单次查询为微秒级以内（历史 Development 构建参考值），未缓存时需线性扫描通道。

```cpp
TPriorityArbitrator<float> SpeedArbitrator(EPriorityArbitrationMode::OverrideHighestPriority);

SpeedArbitrator.SetChannel(TEXT("BaseStats"), 600.0f, 0);
SpeedArbitrator.SetChannel(TEXT("SlowDebuff"), 300.0f, 10);
SpeedArbitrator.SetChannel(TEXT("FreezeStun"), 0.0f, 100);

TArray<FName> ActiveSources;
SpeedArbitrator.GetChannelNames(ActiveSources);

const float FinalSpeed = SpeedArbitrator.GetEffectiveValue(600.0f);
const FName Winner = SpeedArbitrator.GetWinningChannel();

SpeedArbitrator.ClearChannel(TEXT("FreezeStun"));

TPriorityArbitrator<float> DamageMultiplier(EPriorityArbitrationMode::Multiplicative);
DamageMultiplier.SetChannel(TEXT("Buff_Enrage"), 1.5f);
DamageMultiplier.SetChannel(TEXT("Potion_Berserk"), 1.2f);
const float TotalMultiplier = DamageMultiplier.GetEffectiveValue(1.0f);
```

## 常见错误与边界

- **未启动**：`Start` 之前调用 `ChangeState` 返回 `false`，不会触发任何回调。
- **重入洪泛**：回调内连续 `ChangeState` 超过 8 条时，队列溢出返回 `false` 且 `WasTransitionChainTruncated()` 为 `true`。
- **规则忽略**：`AddTransition` 对空 `Guard` 与 `FromState == ToState` 静默忽略；同优先级规则按注册顺序先入先查。
- **当帧生效**：`Update` 中守卫触发跃迁后，新状态在同一帧继续执行 `OnUpdate`，驻留计时从 0 开始。
- **优先级误用**：`Additive` / `Multiplicative` 模式下 `InPriority` 不参与计算，混用会产生与预期不符的累加/累乘结果。
- **缓存前提**：累加/累乘缓存依赖 `DefaultValue` 相等比较；值类型不满足 `std::equality_comparable` 时每次都会重算。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [性能基准](../Architecture/Performance.md)
- [快速上手](../Guides/QuickStart.md)
