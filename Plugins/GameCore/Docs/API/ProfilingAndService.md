# GameCore 性能计时与服务定位 API 参考

> **一句话**：高精度 RAII CPU 计时、固定窗口滑动平均、纯 C++ 线程安全服务定位器与模块关停排空的公共 API。
> **头文件**：`Profiling/FScopedCpuTimer.h`、`Profiling/FMovingAverageSampler.h`、`Service/FGameServiceLocator.h` · **线程**：计时器与采样器非线程安全；`FGameServiceLocator` 由 `FRWLock` 保护，可多线程解析 · **失败语义**：`Resolve` 未注册或类型指纹不匹配返回 `nullptr`；`ResolveRequired` 断言；计时与采样无失败路径

## 30 秒速览

- **计时**：`FScopedCpuTimer` 基于 `FPlatformTime::Cycles64()`，析构时通过回调传出微秒耗时；`GAME_SCOPED_CPU_TIMER` 在 `GAME_ENABLE_CPU_PROFILER` 关闭时完全擦除。
- **平滑统计**：`FMovingAverageSampler<WindowSize>` 用固定栈数组维护滑动平均与历史极值。
- **服务定位**：`FGameServiceLocator` 以 64 位编译期类型指纹寻址，零 RTTI / 零反射 / 零 UObject 依赖，支持类型单例与 `FName` 具名服务双寻址。
- **类型守卫**：具名服务在 `Resolve` 时校验类型指纹，不匹配返回 `nullptr` 并输出 `LogGameServiceLocator` 警告。
- **关停顺序**：`FGameCoreModule::ShutdownModule` 依次取消排队任务、排空在飞任务（1.0 秒超时）、关闭配置注册表、重置服务定位器。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FScopedCpuTimer` | RAII 高精度 CPU 计时，析构时回调微秒耗时 | 非线程安全；回调仅析构触发一次；不可复制 |
| `GAME_SCOPED_CPU_TIMER` | 作用域计时宏 | `GAME_ENABLE_CPU_PROFILER` 关闭（Shipping）时完全擦除 |
| `FScopedCpuTimer::GetElapsedMicroseconds` | 读取自构造或 `Reset()` 起的流逝微秒 | 非线程安全，实例不可跨线程共享 |
| `FMovingAverageSampler::AddSample` | 追加采样并维护滑动窗口 | 非线程安全；窗口满后覆写最旧项 |
| `FMovingAverageSampler::GetAverage` | 当前窗口算术平均 | 无采样返回 `0.0` |
| `FMovingAverageSampler::GetMin` / `GetMax` | 返回历史极值 | 是自上次 `Reset()` 的历史极值，非当前窗口极值 |
| `FGameServiceLocator::RegisterService` | 注册类型单例或 `FName` 具名服务 | 持写锁；同键覆盖；`NAME_None` 走类型单例 |
| `FGameServiceLocator::Resolve` | 解析服务共享指针 | 持读锁；未注册或类型指纹不匹配返回 `nullptr` |
| `FGameServiceLocator::ResolveRequired` | 解析系统关键依赖 | 未注册触发 `checkf`，仅用于不可恢复路径 |
| `FGameCoreModule::ShutdownModule` | 模块关停排空 | 取消排队任务→1.0 秒超时排空→关闭配置→重置定位器 |

## CPU 计时 `FScopedCpuTimer`

头文件：`#include "Profiling/FScopedCpuTimer.h"`

```cpp
class FScopedCpuTimer {
public:
    using FCompleteCallback = TFunction<void(double ElapsedMicroseconds)>;

    FScopedCpuTimer() noexcept;
    explicit FScopedCpuTimer(FCompleteCallback InCallback) noexcept;

    [[nodiscard]] double GetElapsedSeconds() const noexcept;
    [[nodiscard]] double GetElapsedMilliseconds() const noexcept;
    [[nodiscard]] double GetElapsedMicroseconds() const noexcept;
    [[nodiscard]] uint64 GetStartCycles() const noexcept;
    void Reset() noexcept;
};

#if GAME_ENABLE_CPU_PROFILER != 0
    #define GAME_SCOPED_CPU_TIMER(TimerName, OnCompleteLambda) FScopedCpuTimer TimerName(OnCompleteLambda)
#else
    #define GAME_SCOPED_CPU_TIMER(TimerName, OnCompleteLambda)
#endif
```

| 接口 / 宏 | 说明 |
| :--- | :--- |
| `FScopedCpuTimer()` | 仅启动计时，不注册回调 |
| `FScopedCpuTimer(Callback)` | 析构时调用回调，参数为微秒耗时 |
| `GetElapsedSeconds` / `GetElapsedMilliseconds` / `GetElapsedMicroseconds` | 读取自构造或 `Reset()` 起的流逝时间 |
| `GetStartCycles` | 起始 `Cycles64` 计数值 |
| `Reset` | 将起始时间戳重置为当前时刻 |
| `GAME_SCOPED_CPU_TIMER(Name, Lambda)` | 展开为 `FScopedCpuTimer Name(Lambda)`；宏关闭时展开为空 |

- **线程**：非线程安全，实例不应跨线程共享。
- **生命周期**：回调只在析构时触发一次；拷贝构造与拷贝赋值已删除，不可复制。
- **失败语义**：无失败路径；`Reset()` 不重置回调。
- **编译开关**：`GAME_ENABLE_CPU_PROFILER` 默认定义为 `(UE_BUILD_SHIPPING == 0)`，Shipping 构建下宏擦除，回调不会执行。

```cpp
void PerformComplexCalculation() {
    GAME_SCOPED_CPU_TIMER(ScopeTimer, [](double ElapsedUs) {
        if (ElapsedUs > 500.0) {
            UE_LOG(LogTemp, Warning, TEXT("计算超时: %.2f us"), ElapsedUs);
        }
    });
}
```

## 滑动平均 `FMovingAverageSampler<WindowSize>`

头文件：`#include "Profiling/FMovingAverageSampler.h"`

```cpp
template <int32 WindowSize = 64>
class FMovingAverageSampler {
public:
    FMovingAverageSampler() noexcept;

    void AddSample(double SampleValue) noexcept;
    [[nodiscard]] double GetAverage() const noexcept;
    [[nodiscard]] double GetMin() const noexcept;
    [[nodiscard]] double GetMax() const noexcept;
    [[nodiscard]] int32 GetSampleCount() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] static constexpr int32 GetCapacity() noexcept;
    void Reset() noexcept;
};
```

| 接口 | 说明 |
| :--- | :--- |
| `AddSample` | 追加一个采样；窗口未满时追加，满后覆写最旧项并维护 `RunningSum` |
| `GetAverage` | 当前窗口算术平均；无采样返回 `0.0` |
| `GetMin` / `GetMax` | 自上次 `Reset()` 以来的历史极值；无采样返回 `0.0` |
| `GetSampleCount` | 当前有效采样数，上限为 `WindowSize` |
| `GetCapacity` | 编译期窗口容量 |
| `Reset` | 清空采样、游标、累加和与极值 |

- **线程**：非线程安全，单线程持有或外部加锁。
- **复杂度**：常规路径均摊 O(1)；每写满一整圈额外执行一次 O(WindowSize) 求和重算，抑制浮点累计误差。
- **语义边界**：`GetMin` / `GetMax` 是历史极值，不是当前窗口内的极值；`WindowSize` 必须大于 0，否则编译期 `static_assert` 失败。

```cpp
FMovingAverageSampler<64> FrameCostSampler;
FrameCostSampler.AddSample(ElapsedMicroseconds);

const double AvgUs = FrameCostSampler.GetAverage();
const double PeakUs = FrameCostSampler.GetMax();
```

## 服务定位 `FGameServiceLocator`

头文件：`#include "Service/FGameServiceLocator.h"`

```cpp
class GAMECORE_API FGameServiceLocator {
public:
    static FGameServiceLocator& Get();

    template <typename TInterface>
    void RegisterService(
        TSharedRef<TInterface, ESPMode::ThreadSafe> ServiceInstance,
        FName ServiceName = NAME_None
    );

    template <typename TInterface>
    [[nodiscard]] TSharedPtr<TInterface, ESPMode::ThreadSafe> Resolve(FName ServiceName = NAME_None) const;

    template <typename TInterface>
    [[nodiscard]] TSharedRef<TInterface, ESPMode::ThreadSafe> ResolveRequired(FName ServiceName = NAME_None) const;

    template <typename TInterface>
    [[nodiscard]] bool HasService() const;
    [[nodiscard]] bool HasService(FName ServiceName) const;

    template <typename TInterface>
    bool UnregisterService();
    bool UnregisterService(FName ServiceName);

    void Reset();
    [[nodiscard]] int32 Num() const;
    [[nodiscard]] bool IsEmpty() const;
    int32 GetNamedServiceNames(TArray<FName>& OutNames) const;
};
```

| 接口 | 参数 | 返回 / 行为 |
| :--- | :--- | :--- |
| `RegisterService<T>` | 线程安全 `TSharedRef<TInterface>`，可选 `ServiceName` | `NAME_None` 注册类型单例，否则注册具名服务；写锁内覆盖同键条目 |
| `Resolve<T>` | 可选 `ServiceName` | 命中且类型指纹一致返回共享指针，否则返回 `nullptr` |
| `ResolveRequired<T>` | 可选 `ServiceName` | 解析失败触发 `checkf`，返回 `TSharedRef` |
| `HasService<T>()` / `HasService(Name)` | 类型或名称 | 已注册返回 `true` |
| `UnregisterService<T>()` / `UnregisterService(Name)` | 类型或名称 | 成功移除返回 `true` |
| `Reset` | 无 | 清空全部类型与具名服务 |
| `Num` / `IsEmpty` | 无 | 服务总数 / 是否为空 |
| `GetNamedServiceNames` | 输出数组 | 返回具名服务数量 |

- **线程**：读写分离；注册、注销、`Reset` 持写锁，`Resolve`、`HasService` 持读锁。
- **生命周期**：持有 `TSharedPtr` 强引用，直到注销或 `Reset()`；`Get()` 返回进程级单例。
- **类型安全**：具名服务额外校验 64 位类型指纹，不匹配返回 `nullptr` 并记录 `LogGameServiceLocator` 警告，不会执行错误强转。
- **使用约束**：`TInterface` 按抽象接口类型使用；具名服务为空名时按类型服务路径处理。

```cpp
struct ICombatCalculatorService {
    virtual ~ICombatCalculatorService() = default;
    virtual int32 CalculateDamage(int32 BaseAtk, int32 TargetDef) const = 0;
};

FGameServiceLocator::Get().RegisterService<ICombatCalculatorService>(
    MakeShared<FStandardCombatCalculator, ESPMode::ThreadSafe>()
);

if (TSharedPtr<ICombatCalculatorService> Calculator =
        FGameServiceLocator::Get().Resolve<ICombatCalculatorService>()) {
    const int32 FinalDamage = Calculator->CalculateDamage(100, 40);
}
```

## 模块关停 `FGameCoreModule`

实现位置：`Source/GameCore/Private/GameCore.cpp`

```cpp
void FGameCoreModule::ShutdownModule() {
    FGameThreadTaskRunner::CancelAllPendingTasks();
    FGameThreadTaskRunner::FlushInFlightTasks(1.0);
    FConfigRegistry::Shutdown();
    FGameServiceLocator::Get().Reset();
}
```

- **线程**：由引擎模块管理器在关停阶段调用，业务代码不应主动调用。
- **生命周期**：先取消尚未开始的后台任务，再以 1.0 秒超时排空在飞任务，最后解绑配置注册表并清空服务定位器，避免模块卸载后的悬垂回调。
- **失败语义**：`FlushInFlightTasks(1.0)` 超时后不再无限等待，防止关停挂死。

## 常见错误与边界

- **类型指纹不匹配**：具名服务用错误接口 `Resolve` 返回 `nullptr`，不会抛出或强转；检查 `LogGameServiceLocator` 警告定位。
- **断言接口**：`ResolveRequired` 在未注册时触发 `checkf`，只用于系统关键依赖，可恢复路径请用 `Resolve`。
- **单例残留**：`FGameServiceLocator::Get()` 为进程级单例，测试或会话切换时需显式 `Reset()`。
- **Shipping 擦除**：`GAME_SCOPED_CPU_TIMER` 在 Shipping 下不产生计时对象，回调不会执行，勿在回调中承载业务副作用。
- **极值语义**：`FMovingAverageSampler::GetMin` / `GetMax` 是历史极值，窗口回绕不会淘汰旧极值。
- **锁开销**：`Num()` / `IsEmpty()` 也会获取读锁；每帧热路径可缓存结果，避免重复加锁。

## 相关文档

<details>
<summary>自动化测试用例与基准</summary>

- `Game.Core.Profiling.CpuTimer.Basic`
- `Game.Core.Profiling.CpuTimer.MovingAverageSampler`
- `Game.Core.Service.ServiceLocator.TypeAndNamed`
- `Game.Core.Service.ServiceLocator.Concurrency`（5000 次 `ParallelFor` 并发解析）
- `Game.Core.Perf.VersionedCacheFind`（200,000 次，预算 < 2000 ns/op，实测 ~10 ns/op，历史 Development 构建参考值）
- `Game.Core.Perf.ListenerHubThroughput`（100,000 次，预算 < 5000 ns/op，实测 ~17 ns/op，历史 Development 构建参考值）

```bash
python3 .scripts/run_automation_tests.py --case Game.Core.Profiling
python3 .scripts/run_automation_tests.py --case Game.Core.Service
python3 .scripts/run_automation_tests.py --case Game.Core.Perf
```

</details>

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [性能基准](../Architecture/Performance.md)
- [快速上手](../Guides/QuickStart.md)
