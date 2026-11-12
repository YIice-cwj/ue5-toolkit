# GameCore 状态机与优先级仲裁 API 参考

本文档汇总 `GameCore` 提供的强类型有限状态机与多源属性优先级仲裁原语（全部位于 `State/` 目录）：
- `TGameStateMachine<TStateEnum>`：强类型事件驱动有限状态机（写时复制规则快照、链式重入队列拉平、死循环熔断与驻留时长统计）
- `TPriorityArbitrator<T>`：泛型多源属性优先级仲裁器（最高优先级覆盖与平局决胜、字典序严格一致累加/累乘与脏标记缓存）

---

## 1. 强类型有限状态机 `TGameStateMachine<TStateEnum>`

头文件：`#include "State/TGameStateMachine.h"`

### 适用场景与核心架构
面向 UI 交互流转、关卡流程推进、机关状态触发、角色能力与武器装填等无需引入 StateTree 重型资产的场景，提供零 UObject 开销、极速且确定性的纯 C++ 状态机设施：

- **编译期枚举概念约束**：通过 C++20 `concept CGameStateEnum = std::is_enum_v<T>` 强制约束状态类型为原生或强类型 `enum class`，类型安全无反射开销；
- **写时复制（COW）规则快照**：
  - 转换规则采用 `TSharedPtr<TArray<FTransitionRule>>` 管理；
  - 推进 `Update(DeltaSeconds)` 评估守卫前制作局部的规则不可变快照，彻底消灭“在条件 Guard 闭包中增删跃迁规则引发的迭代器失效与野指针崩溃”；
- **重入队列拉平与零调用栈溢出**：
  - 在执行 `OnEnter` / `OnExit` 回调时，若业务逻辑在回调内部再次调用 `ChangeState`，状态机通过 `bIsTransitioning` 状态位将跃迁请求压入内联 FIFO 队列 `QueuedStates`；
  - 外层循环按顺序依次解出目标并线性推进，将深层递归调用拉平为平面队列，彻底杜绝深层调用栈溢出（Stack Overflow）；
- **死循环截断熔断保护**：
  - 内建 `MAX_CHAINED_TRANSITIONS = 8` 步长截断机制；
  - 若状态机因规则互锁发生 $A \leftrightarrow B$ 无尽链式死循环，第 8 步自动中断并输出 `LogGameStateMachine` 警告，避免游戏卡死无响应。
- **精确驻留时长与即刻推进契约**：
  - 状态机切换时重置驻留秒数，每帧 `Update` 累加，可通过 `GetTimeInCurrentState()` 便捷用于状态超时检测与动画过渡；
  - 当在 `Update(DeltaSeconds)` 内因守卫触发自动跃迁时，时序执行线为：`旧态OnExit(新态)` -> `新态OnEnter(旧态)` -> `新态OnUpdate(DeltaSeconds)`，新状态当帧无延迟即刻生效。

```cpp
enum class EPlayerStance : uint8 {
    Idle,
    Walking,
    Sprinting,
    Crouching
};

TGameStateMachine<EPlayerStance> StanceFsm;
float Stamina = 100.0f;
bool bCrouchRequested = false;

// 1. 注册状态生命周期
StanceFsm.RegisterState(EPlayerStance::Idle, {
    /* OnEnter */  [](EPlayerStance Prev) { UE_LOG(LogGameStateMachine, Log, TEXT("Enter Idle")); },
    /* OnUpdate */ [](float DeltaTime) {},
    /* OnExit */   [](EPlayerStance Next) {}
});

StanceFsm.RegisterState(EPlayerStance::Sprinting, {
    /* OnEnter */  [](EPlayerStance Prev) {},
    /* OnUpdate */ [&Stamina](float DeltaTime) { Stamina -= DeltaTime * 20.0f; },
    /* OnExit */   [](EPlayerStance Next) {}
});

// 2. 注册带优先级的条件跃迁守卫
// 体力耗尽时自动降级回 Idle（高优先级 10）
StanceFsm.AddTransition(EPlayerStance::Sprinting, EPlayerStance::Idle, [&Stamina]() {
    return Stamina <= 0.0f;
}, /* Priority = */ 10);

// 3. 启动状态机并按帧推进
StanceFsm.Start(EPlayerStance::Idle);

// 显式切态
StanceFsm.ChangeState(EPlayerStance::Sprinting);

// 帧循环驱动
void Tick(float DeltaSeconds) {
    StanceFsm.Update(DeltaSeconds);
    if (StanceFsm.GetTimeInCurrentState() > 5.0f) {
        // 当前姿态已持续超过 5 秒
    }
}
```

---

## 2. 泛型多源属性优先级仲裁器 `TPriorityArbitrator<T>`

头文件：`#include "State/TPriorityArbitrator.h"`

### 适用场景与仲裁模式
在复杂游戏系统中，单个属性（如角色移动速度、最大生命值、受击霸体等级、受控状态）经常同时受到多个系统（装备、Buff、地形、技能施法、Debug 指令）的并发修改请求。`TPriorityArbitrator` 提供工业级的冲突裁决原语：

- **三种仲裁求值模式**：
  1. `EPriorityArbitrationMode::OverrideHighestPriority`（最高优先级覆盖）：
     - 选取请求源中 `Priority` 数值最高者胜出；
     - **平局决胜（Tie-break）保证**：当多个通道优先级相同时，严格根据后入胜出序列号 `Sequence` 裁决，后提交者覆盖先提交者；
  2. `EPriorityArbitrationMode::Additive`（多源累加）：
     - 以传入的 `DefaultValue` 为基准值，累加所有激活通道的值；
     - **字典序严格确定性**：运算前将通道按 `FNameLexicalLess` 强制升序排列，彻底避免多平台哈希表迭代乱序引发的浮点累加误差；
  3. `EPriorityArbitrationMode::Multiplicative`（多源链式累乘）：
     - 以传入的 `DefaultValue` 为基准值，依次累乘各通道数值（同样按字典序稳定执行）；
- **缓存与脏标记机制**：
  - 对满足 `std::equality_comparable` 的值类型自动缓存仲裁结果；
  - 通道无变动且基准值不变时直接返回缓存结果，单次查询微秒级耗时；
- **通道观测能力**：
  - 提供 `GetChannelNames(OutNames)` 与 `HasChannel(Name)`，供 UI 面板与调试工具实时获取当前激活的仲裁源；
- **零额外堆内存分配**：
  - 排序与缓存队列采用 `TInlineAllocator<16>`，在绝大多数常规通道（$\le 16$ 个来源）下全程零堆分配。

```cpp
// 1. 最高优先级覆盖仲裁（如移速被冰冻硬控覆盖）
TPriorityArbitrator<float> SpeedArbitrator(EPriorityArbitrationMode::OverrideHighestPriority);

SpeedArbitrator.SetChannel(TEXT("BaseStats"), 600.0f, /* Priority = */ 0);
SpeedArbitrator.SetChannel(TEXT("SlowDebuff"), 300.0f, /* Priority = */ 10);
SpeedArbitrator.SetChannel(TEXT("FreezeStun"), 0.0f,   /* Priority = */ 100);

// 查询所有生效来源通道列表
TArray<FName> ActiveSources;
SpeedArbitrator.GetChannelNames(ActiveSources); // [BaseStats, SlowDebuff, FreezeStun]

// 胜出通道为 FreezeStun，最终移速为 0.0f
const float FinalSpeed = SpeedArbitrator.GetEffectiveValue(/* DefaultValue = */ 600.0f);
const FName Winner = SpeedArbitrator.GetWinningChannel(); // TEXT("FreezeStun")

// 解除硬控通道
SpeedArbitrator.ClearChannel(TEXT("FreezeStun"));
// 胜出通道自动回退为 SlowDebuff，移速恢复为 300.0f

// 2. 累乘倍率仲裁（如暴击/易伤多源加成）
TPriorityArbitrator<float> DamageMultiplier(EPriorityArbitrationMode::Multiplicative);
DamageMultiplier.SetChannel(TEXT("Buff_Enrage"), 1.5f);
DamageMultiplier.SetChannel(TEXT("Potion_Berserk"), 1.2f);

// 结果为 1.0f * 1.5f * 1.2f = 1.8f
const float TotalMultiplier = DamageMultiplier.GetEffectiveValue(/* DefaultValue = */ 1.0f);
```
