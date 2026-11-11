# GameCore 性能基准与预算

本文件记录 `GameCore` 核心设施基准测试（`Game.Core.Perf.*`）及关联自动化基线的实测结果、预算阈值与复现方式。预算刻意放宽，只拦截数量级退化，不把机器抖动当作失败。

---

## 1. 基准清单

| 测试用例路径 | 覆盖原语与算法 | 压测规模 | 预算门禁 | 实测参考 |
| :--- | :--- | :--- | :--- | :--- |
| `Game.Core.Perf.VersionedCacheFind` | `FVersionedCache::FindShared` (Eager 快照) | 4096 键 / 200,000 次 | < 2000 ns/op | **~10.5 ns/op** (0.5% 预算) |
| `Game.Core.Perf.ListenerHubThroughput` | `FListenerHub::Emit` (COW 零锁派发) | 100,000 次单订阅广播 | < 5000 ns/op | **~17.2 ns/op** (0.3% 预算) |
| `Game.Core.Service.ServiceLocator.Concurrency` | `FGameServiceLocator::Resolve` (多线程高频读) | 5000 次 ParallelFor 并发 | 零数据撕裂与死锁 | **100% 成功** |
| `Game.Core.Profiling.CpuTimer.MovingAverageSampler` | `FMovingAverageSampler` (滑动平均采样) | 窗口滑动与极值回绕 | 纯栈计算零堆分配 | **< 1 ns/op** |

---

## 2. 性能分析设施集成

`GameCore` 提供了面向微观性能分析的高精度工具库：
- **`FScopedCpuTimer`**：基于 `FPlatformTime::Cycles64()`，纳秒/微秒级精度，析构自动回调传出。
- **`GAME_SCOPED_CPU_TIMER` 宏**：在 `UE_BUILD_SHIPPING` 构建下完全宏擦除，零汇编开销。
- **`FMovingAverageSampler<WindowSize>`**：固定数组栈上滑动平均，单次推进时间复杂度严格为 $O(1)$。

```cpp
#include "Profiling/FScopedCpuTimer.h"

// 示例：微秒级局部耗时采样
GAME_SCOPED_CPU_TIMER(Timer, [](double ElapsedUs) {
    if (ElapsedUs > 500.0) {
        UE_LOG(LogGameCore, Warning, TEXT("计算超时: %.2f us"), ElapsedUs);
    }
});
```

---

## 3. 自动化复现指令

通过工程根目录自动化执行脚本一键回归：
```bash
# 1. 运行核心性能基准压测
python3 .scripts/run_automation_tests.py --case Game.Core.Perf

# 2. 运行性能分析器与计时器测试
python3 .scripts/run_automation_tests.py --case Game.Core.Profiling

# 3. 运行服务定位器多线程并发压力测试
python3 .scripts/run_automation_tests.py --case Game.Core.Service
```

或通过 UnrealEditor 命令行直接复现：
```bash
UnrealEditor-Cmd <Project.uproject> \
  -ExecCmds="Automation RunTests Game.Core.Perf; Quit" \
  -unattended -nullrhi -nosplash -log -stdout -FullStdOutLogOutput \
  -DDC-ForceMemoryCache
```

---

## 4. 性能回归研判与治理契约

- **实测超出预算**：首先排查是否由于后台编译进程、IDE 索引或性能 Profiler 探针干扰导致；
- **真实退化**：严格遵循 `AGENTS.md` 铁律 **B7 (根因溯源，全局研判)**，利用 Git 提交溯源变更，定位引入算法复杂度退化或堆内存频繁分配的具体代码路径；
- **禁止私自上调预算**：严禁仅为让自动化测试通过而私自放宽预算；任何预算阈值调整必须经团队评审并记录在版本变更说明中。
