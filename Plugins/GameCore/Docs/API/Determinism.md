# GameCore 确定性物理与联机状态 API 参考

本文档汇总 `GameCore` 提供的确定性物理时间步、状态校验与历史回退快照原语（全部位于 `Determinism/` 目录）：
- `FFixedTimestepAccumulator`：固定时间步长累加器、防螺旋死锁截断与渲染插值 Alpha 平滑计算
- `FStateChecksumBuilder`：64 位零堆分配高雪崩增量流式状态校验器
- `FDesyncDetector`：双端联机与回放状态分叉探测器
- `TRewindSnapshotBuffer<TSnapshot, Capacity>`：定长循环历史快照环形缓冲区与亚帧平滑插值器

---

## 1. 固定时间步长累加器 `FFixedTimestepAccumulator`

头文件：`#include "Determinism/FFixedTimestepAccumulator.h"`

### 核心特性与算法模型
- **时间步与渲染帧率解耦**：基于 Glenn Fiedler "Fix Your Timestep!" 经典确定性物理推进模型，无论渲染帧率（如 30fps、60fps 或 144fps）如何波动，逻辑层与物理层始终以精确恒定的步长（如 60Hz = 0.016667s）离散推进；
- **防螺旋死锁（Spiral of Death）保护**：
  - 当游戏遭遇卡顿或单帧耗时极大（如加载尖刺）时，若无节制补齐子步，会导致下一帧计算量进一步扩大而恶性循环崩溃；
  - 累加器通过 `MaxSubsteps` 限制单帧最大执行步数，超出上限的整步积压时间通过 `FMath::Fmod(AccumulatedTime, FixedDeltaTime)` 自动截断丢弃；
- **渲染插值因子（Alpha）输出**：
  - `GetInterpolationAlpha()` 返回 $[0.0, 1.0]$ 范围内的未消费时间比例；
  - 渲染层利用该 Alpha 在“上一逻辑状态”与“当前逻辑状态”之间进行平滑插值，从视觉上彻底消除低频物理步进产生的卡顿与跳跃感。

```cpp
// 初始化 60Hz 步长，单帧最多补 4 步，最大流逝保护 0.25 秒
FFixedTimestepAccumulator PhysicsTicker(1.0 / 60.0, 4, 0.25);

// 每帧 Tick 驱动（传入引擎渲染 DeltaSeconds）
void Tick(float DeltaSeconds) {
    const int32 Steps = PhysicsTicker.Advance(DeltaSeconds, [](double FixedDt) {
        // 严格以 0.016667s 固定步长执行物理/确定性逻辑更新
        SimulatePhysicsSubstep(FixedDt);
    });

    // 获取插值比例，提交给渲染组件消除视觉顿挫
    const double Alpha = PhysicsTicker.GetInterpolationAlpha();
    RenderMesh->SetWorldLocation(FMath::Lerp(PrevPhysicsLocation, CurrentPhysicsLocation, Alpha));
}
```

---

## 2. 64 位确定性状态校验器 `FStateChecksumBuilder`

头文件：`#include "Determinism/FStateChecksum.h"`

### 核心特性
- **绝对零堆内存分配**：内部仅维护 64 位哈希状态机，单次调用均为纯纳秒级寄存器运算，适合每逻辑步高频全量计算；
- **Mix64 高雪崩位混合**：采用 MurmurHash3 终态散列混淆算法，任意 1-bit 输入变化引发输出哈希完全雪崩；
- **浮点量化（Quantization）抗抖动**：
  - 跨平台、不同编译器版本与 SIMD 优化可能引发尾数微小的舍入差异；
  - 提供 `AddFloatQuantized` 与 `AddVectorQuantized`，按配置精度（如毫米级 `0.001f` 或厘米级 `0.01f`）离散化后再注入哈希，消灭非恶性浮点舍入抖动；
  - 对非法浮点数（`NAN`/`INF`）内建安全防御，避免未定义行为。

```cpp
FStateChecksumBuilder Builder;

// 注入整型状态
Builder.AddUInt32(EntityCount);
Builder.AddUInt64(GlobalSeed);

// 注入按厘米精度量化的空间坐标
Builder.AddVectorQuantized(PlayerPosition, 0.01f);

// 注入原生字节流数据
Builder.AddBytes(InventoryBytes.GetData(), InventoryBytes.Num());

// 产出 64 位校验和（纳秒级）
const uint64 FrameChecksum = Builder.Finalize();
```

---

## 3. 双端联机分叉探测器 `FDesyncDetector`

头文件：`#include "Determinism/FStateChecksum.h"`

### 适用场景
- **帧同步与网络状态回放**：在定长历史滑动窗口内比对本地与远端汇报的校验码；
- **分叉即时报警**：当检测到同一物理帧两端 Checksum 不一致时，立即触发委托回调，输出含帧号、本地码、远端码的 `FDesyncReport`，便于迅速生成内存快照 Dump 协助定位联机 Desync 根因。

```cpp
FDesyncDetector Detector;

// 绑定分叉报警委托
Detector.SetDesyncCallback(FDesyncDetector::FOnDesyncDetected::CreateLambda([](const FDesyncReport& Report) {
    UE_LOG(LogTemp, Error, TEXT("FATAL DESYNC AT FRAME %llu! Local:0x%llX != Remote:0x%llX"),
        Report.Frame, Report.LocalChecksum, Report.RemoteChecksum);
    TriggerDesyncMemoryDump();
}));

// 本地完成物理步更新后记录
Detector.RecordLocalChecksum(CurrentFrame, LocalChecksum);

// 收到远端客户端网络包后比对
FDesyncReport Report;
if (!Detector.VerifyRemoteChecksum(RemoteFrame, RemoteChecksum, Report)) {
    if (Report.bDiverged) {
        // 已分叉，触发纠偏或断开重连
    }
}
```

---

## 4. 定长历史快照环形缓冲区 `TRewindSnapshotBuffer<TSnapshot, Capacity>`

头文件：`#include "Determinism/TRewindSnapshotBuffer.h"`

### 核心特性与算法
- **编译期定长容量**：`Capacity` 必须为 2 的幂且 $\ge 2$，全程零堆内存分配，覆写旧帧；
- **$O(1)$ 直达帧寻址**：
  - 针对正常连续帧序列，通过 `SequenceDelta = Newest.FrameSequence - TargetFrame` 实现一次位运算直达；
  - 针对丢帧或乱序场景，自动回退为定长环形安全扫描；
- **时间戳单调守卫**：写入乱序时间戳时自动钳制为上一帧时间戳，维护环内严格非递减；
- **亚帧插值（Sub-frame Interpolation）**：
  - 采用 $O(\log N)$ 二分查找精确定位目标时间戳前后的快照区间；
  - 接收外部传入的插值器委托 `FInterpolator (A, B, Alpha) -> TSnapshot`，完美契合服务端射击延迟补偿（Lag Compensation）与客户端平滑预测回滚。

```cpp
struct FPlayerSnapshot {
    FVector Position = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
};

// 预留 128 帧历史快照环（零堆内存分配）
TRewindSnapshotBuffer<FPlayerSnapshot, 128> HistoryBuffer;

// 记录物理帧快照
HistoryBuffer.RecordSnapshot(CurrentTimeSeconds, CurrentFrameSeq, PlayerSnapshot);

// 服务端延迟补偿：查询射击者过去特定时间戳处的玩家快照
FPlayerSnapshot RewoundState;
bool bFound = HistoryBuffer.GetInterpolatedSnapshot(
    ShotTimeSeconds,
    RewoundState,
    [](const FPlayerSnapshot& A, const FPlayerSnapshot& B, float Alpha) {
        FPlayerSnapshot Result;
        Result.Position = FMath::Lerp(A.Position, B.Position, Alpha);
        Result.Rotation = FMath::Lerp(A.Rotation, B.Rotation, Alpha);
        return Result;
    }
);
if (bFound) {
    // 使用 RewoundState 进行命中判定检测
}
```
