# GameCore 性能评测、服务定位器与模块生命周期 API 参考

本文档是 `Plugins/GameCore` 性能分析与服务定位层（Profiling & Service）的唯一权威规范（SSOT），系统涵盖高精度 CPU 耗时采样、滑动平均统计、纯 C++ 线程安全服务定位容器与核心模块安全关停排空生命周期。

---

## 一、 架构总览与核心设计

```
+-----------------------------------------------------------------------------------------+
|                                    业务与系统依赖层                                     |
+-----------------------------------------------------------------------------------------+
                                          |
                +-------------------------+-------------------------+
                |                                                   |
                v                                                   v
+-------------------------------+                   +-------------------------------+
|       FScopedCpuTimer         |                   |      FGameServiceLocator      |
|  (RAII 零开销高精度计时器)      |                   |  (纯 C++ 线程安全依赖注入容器) |
|   + FMovingAverageSampler     |                   |  + 64 位编译期指纹零 RTTI 寻址  |
+-------------------------------+                   +-------------------------------+
                |                                                   |
                +-------------------------+-------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------------+
|                     FGameCoreModule (主模块生命周期协同排空与对称注销)                     |
+-----------------------------------------------------------------------------------------+
```

### 核心红线与设计原则
1. **Shipping 零 CPU 与二进制开销**：`FScopedCpuTimer` 纯头文件内联实现，配以 `GAME_SCOPED_CPU_TIMER` 宏；在 `UE_BUILD_SHIPPING` 构建下完全宏擦除为空，零汇编指令残留。
2. **零 RTTI、零反射、零 UObject 依赖**：`FGameServiceLocator` 专为脱离 `UWorld`/`UGameInstance` 约束的纯逻辑架构设计，采用 `Game::Core::GetTypeFingerprint<T>()` 64 位编译期指纹寻址，消除动态库跨边界 RTTI 符号冲突与虚函数反射开销。
3. **读写分离与高频并发安全**：服务定位器由 `FRWLock` 读写锁保护，读取路径持轻量读锁，在 5000 次 `ParallelFor` 多线程并发解析下零数据撕裂与零死锁。
4. **模块关停强保障排空 (Graceful Shutdown)**：模块卸载时严格执行“取消排队任务 -> 超时等待后台在飞任务退出 -> 对称注销注册项”，杜绝模块卸载后后台线程回调引发悬垂野指针崩溃。

---

## 二、 模块 API 规格

### 1. `FScopedCpuTimer` (高精度 RAII 作用域计时器)

头文件：`#include "Profiling/FScopedCpuTimer.h"`

#### 核心接口
```cpp
class FScopedCpuTimer {
public:
    using FCompleteCallback = TFunction<void(double ElapsedMicroseconds)>;

    // 纯计时构造（不注册回调，用于手动按需查询）
    FScopedCpuTimer() noexcept;

    // 作用域析构回调构造（析构时自动传出微秒耗时）
    explicit FScopedCpuTimer(FCompleteCallback InCallback) noexcept;

    // 获取当前流逝时间（秒/毫秒/微秒）
    [[nodiscard]] double GetElapsedSeconds() const noexcept;
    [[nodiscard]] double GetElapsedMilliseconds() const noexcept;
    [[nodiscard]] double GetElapsedMicroseconds() const noexcept;

    // 重置起始时间戳
    void Reset() noexcept;
};
```

#### 生产采样宏用法
```cpp
#include "Profiling/FScopedCpuTimer.h"

void PerformComplexCalculation()
{
    // 非 Shipping 下统计微秒耗时，Shipping 下完全擦除
    GAME_SCOPED_CPU_TIMER(ScopeTimer, [](double ElapsedUs) {
        if (ElapsedUs > 500.0) {
            UE_LOG(LogTemp, Warning, TEXT("计算超时: %.2f us"), ElapsedUs);
        }
    });

    // 执行重型算法...
}
```

---

### 2. `FMovingAverageSampler<WindowSize>` (固定窗口连续滑动平均采样器)

头文件：`#include "Profiling/FScopedCpuTimer.h"`

#### 算法与时间复杂度
- **O(1) 增量滑动更新**：内部使用连续固定大小栈数组 `double Samples[WindowSize]`，通过环形写游标 `WriteIndex` 覆写最旧项并更新 `RunningSum`，单次 `AddSample()` 仅涉及 2 次加减法与 1 次取模。
- **运行极值跟踪**：同步维护历史最小值（`GetMin()`）与历史最大值（`GetMax()`）。

```cpp
FMovingAverageSampler<64> FrameCostSampler;

// 每次任务迭代后压入微秒耗时
FrameCostSampler.AddSample(ElapsedMicroseconds);

// 获取平滑后的算术平均值
double AvgUs = FrameCostSampler.GetAverage();
double PeakUs = FrameCostSampler.GetMax();
```

---

### 3. `FGameServiceLocator` (纯 C++ 线程安全服务定位器)

头文件：`#include "Service/FGameServiceLocator.h"`

#### 核心接口
```cpp
class GAMECORE_API FGameServiceLocator {
public:
    // 获取全局单例引用
    static FGameServiceLocator& Get();

    // 1. 基于抽象接口类型的单例服务注册与解析
    template <typename TInterface>
    void RegisterService(TSharedRef<TInterface, ESPMode::ThreadSafe> ServiceInstance);

    template <typename TInterface>
    [[nodiscard]] TSharedPtr<TInterface, ESPMode::ThreadSafe> Resolve() const;

    template <typename TInterface>
    [[nodiscard]] bool HasService() const;

    template <typename TInterface>
    bool UnregisterService();

    // 2. 基于 FName 具名与接口类型的服务注册与解析
    template <typename TInterface>
    void RegisterService(FName ServiceName, TSharedRef<TInterface, ESPMode::ThreadSafe> ServiceInstance);

    template <typename TInterface>
    [[nodiscard]] TSharedPtr<TInterface, ESPMode::ThreadSafe> Resolve(FName ServiceName) const;

    bool UnregisterService(FName ServiceName);

    // 3. 生命周期与统计
    void Reset();
    [[nodiscard]] int32 Num() const;
};
```

#### 典型解耦使用示例
```cpp
// 1. 定义纯 C++ 抽象业务契约接口（零 UObject 依赖）
struct ICombatCalculatorService {
    virtual ~ICombatCalculatorService() = default;
    virtual int32 CalculateDamage(int32 BaseAtk, int32 TargetDef) const = 0;
};

// 2. 实现具体服务并注入定位器
class FStandardCombatCalculator : public ICombatCalculatorService {
public:
    int32 CalculateDamage(int32 BaseAtk, int32 TargetDef) const override {
        return FMath::Max(1, BaseAtk - TargetDef / 2);
    }
};

// 在模块启动或游戏初始化时注册
FGameServiceLocator::Get().RegisterService<ICombatCalculatorService>(
    MakeShared<FStandardCombatCalculator, ESPMode::ThreadSafe>()
);

// 3. 在任意多线程或逻辑层中优雅解析并调用
if (TSharedPtr<ICombatCalculatorService> Calculator = FGameServiceLocator::Get().Resolve<ICombatCalculatorService>()) {
    const int32 FinalDamage = Calculator->CalculateDamage(100, 40);
}
```

---

### 4. `FGameCoreModule` 模块生命周期排空

源文件：`Private/GameCore.cpp`

```cpp
void FGameCoreModule::ShutdownModule() {
    // 1. 取消所有处于队列中尚未开始执行的后台任务
    FGameThreadTaskRunner::CancelAllPendingTasks();

    // 2. 协同等待当前所有在飞工作线程任务排空退出（超时 1.0 秒防挂死）
    FGameThreadTaskRunner::FlushInFlightTasks(1.0);

    // 3. 对称注销所有全局编译开关注册项
    FGameCoreRegistration::UnregisterAll();
}
```

---

## 三、 自动化测试套件

本组件全部测试用例统一定义于 `Game.Core.Profiling.*`、`Game.Core.Service.*` 与 `Game.Core.Perf.*`：
- `Game.Core.Profiling.CpuTimer.Basic`：测试基础计时与 RAII 作用域析构回调；
- `Game.Core.Profiling.CpuTimer.MovingAverageSampler`：测试滑动窗口回绕与极值计算；
- `Game.Core.Service.ServiceLocator.TypeAndNamed`：测试类型服务、具名服务寻址与注销；
- `Game.Core.Service.ServiceLocator.Concurrency`：测试 5000 次多线程并发高频无锁解析；
- `Game.Core.Perf.VersionedCacheFind`：200,000 次缓存查找基准（< 2000ns/op，实测 ~10ns/op）；
- `Game.Core.Perf.ListenerHubThroughput`：100,000 次事件广播基准（< 5000ns/op，实测 ~17ns/op）。

执行指令：
```bash
python3 .scripts/run_automation_tests.py --case Game.Core.Profiling
python3 .scripts/run_automation_tests.py --case Game.Core.Service
python3 .scripts/run_automation_tests.py --case Game.Core.Perf
```
