# GameCore 性能基准与预算

本文件记录 `GameCore` 核心设施基准测试（`Game.Core.Perf.*`）及关联自动化基线的实测结果、预算阈值与复现方式。预算刻意放宽，只拦截数量级退化，不把机器抖动当作失败。

---

## 1. 基准清单（UE 5.8 Mac arm64 Development 实测）

| 测试用例路径 | 覆盖原语与算法 | 压测规模 | 预算门禁 | 实测中位数参考 |
| :--- | :--- | :--- | :--- | :--- |
| `Game.Core.Perf.ByteSpanWriterReader` | `FByteSpanWriter/Reader` 连续编解码 | 100,000 次往返 | < 300 ns/op | **~0.6 ns/op** |
| `Game.Core.Perf.SlotMapFindAndIterate` | `TSlotMap::Find` 代际句柄查找 | 200,000 次 | < 100 ns/op | **~1.3 ns/op** (745 Mops/s) |
| `Game.Core.Perf.CachedConfigHandleVsRegistry` | `TCachedConfigHandle<float>::Get()` (无锁标量原子) | 200,000 次 | < 300 ns/op | **~2.3 ns/op** (**2.80×** vs Registry 6.5 ns) |
| `Game.Core.Perf.SortedVectorMapFind` | `TSortedVectorMap::Find` 扁平二分查找 | 200,000 次 | < 150 ns/op | **~2.7 ns/op** (370 Mops/s) |
| `Game.Core.Perf.LockFreeRingBuffer` | `TLockFreeRingBuffer` MPMC 入队/出队往返 | 200,000 次 | < 200 ns/op | **~3.6 ns/op** (278 Mops/s) |
| `Game.Core.Perf.FixedSizePoolVsMalloc` | `TFixedSizePool` New/Free 对比原生堆分配 | 200,000 次 | < 200 ns/op | **~5.1 ns/op** (194 Mops/s) |
| `Game.Core.Perf.WeightedPickerPick` | `TWeightedPicker` (32 项 Vose 别名表) | 200,000 次 | < 150 ns/op | **~6.7 ns/op** (150 Mops/s) |
| `Game.Core.Perf.PriorityArbitrator` | `TPriorityArbitrator` (8 通道更新与累加仲裁) | 100,000 次 | < 500 ns/op | **~7.3 ns/op** (137 Mops/s) |
| `Game.Core.Perf.VersionedCacheFind` | `FVersionedCache::FindShared` (Eager 快照) | 4096 键 / 200,000 次 | < 2000 ns/op | **~8.9 ns/op** (112 Mops/s) |
| `Game.Core.Perf.RewindSnapshotInterpolation` | `TRewindSnapshotBuffer` (128 帧二分插值) | 100,000 次 | < 300 ns/op | **~9.3 ns/op** (108 Mops/s) |
| `Game.Core.Perf.ListenerHubThroughput` | `FListenerHub::Emit` (COW 零锁派发) | 100,000 次单订阅广播 | < 5000 ns/op | **~13.9 ns/op** (72 Mops/s) |
| `Game.Core.Perf.StateChecksumMix64` | `FStateChecksumBuilder` (8 字节小端定序块混入) | 100,000 次综合状态 | < 500 ns/op | **~25.8 ns/op** (38.8 Mops/s) |
| `Game.Core.Perf.ScratchpadVsHeapArray` | `TScratchArray` (128 元素全周期 vs 堆 `TArray`) | 20,000 次 | < 2000 ns/op | **~79.9 ns/op** (**2.96×** vs Heap 236.5 ns) |
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
