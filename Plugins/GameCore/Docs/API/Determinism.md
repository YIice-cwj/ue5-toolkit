# GameCore 确定性 API 参考

> **一句话**：固定时间步推进、64 位状态校验、双端分叉检测与历史快照回滚的公共 API。
> **头文件**：`Determinism/FFixedTimestepAccumulator.h` · `Determinism/FStateChecksumBuilder.h` · `Determinism/FDesyncDetector.h` · `Determinism/TRewindSnapshotBuffer.h`
> **线程**：全部类型均非线程安全，按单仿真线程契约使用；`FDesyncDetector` 的报警委托在调用 `VerifyRemoteChecksum` 的线程同步触发
> **失败语义**：无错误码，以 `bool` / `nullptr` / `false` 返回；输入非法时钳制、回退或返回未命中，不抛异常

## 30 秒速览

- **固定步长**：`FFixedTimestepAccumulator` 将渲染 DeltaTime 按固定步长离散推进，`MaxSubsteps` 限制单帧补步数以防螺旋死锁。
- **状态校验**：`FStateChecksumBuilder` 零堆分配、Mix64 高雪崩混入，提供整型、量化浮点、量化向量与字节流注入。
- **分叉检测**：`FDesyncDetector` 保存 128 帧本地校验码，`VerifyRemoteChecksum` 比对并输出 `FDesyncReport`。
- **历史回滚**：`TRewindSnapshotBuffer<TSnapshot, Capacity>` 编译期定长环形缓冲，`Capacity` 必须为 2 的幂且 ≥2，支持 O(1) 帧寻址与 O(log N) 亚帧插值。
- **约束**：快照与校验器均无堆分配；所有时间戳以秒为单位，非有限值按 0 或未命中等约定处理。

## API 速查

| 接口 | 用途 | 关键约束 |
| :--- | :--- | :--- |
| `FFixedTimestepAccumulator::Advance` | 按固定步长推进并执行子步回调 | 非线程安全；非有限或 ≤0 输入返回 0；达 `MaxSubsteps` 丢弃积压时间 |
| `FFixedTimestepAccumulator::GetInterpolationAlpha` | 取未消费时间比例供渲染插值 | 返回 `[0.0, 1.0]` |
| `FStateChecksumBuilder::AddVectorQuantized` | 量化注入三维向量 | 零堆分配；非线程安全 |
| `FStateChecksumBuilder::Finalize` | 输出最终 64 位校验码 | `const` 可多次读取；复用前须 `Reset` |
| `FDesyncDetector::RecordLocalChecksum` | 记录本地帧校验码 | 历史容量固定 128 帧，超出环形覆盖 |
| `FDesyncDetector::VerifyRemoteChecksum` | 比对远端校验码并输出分叉报告 | 分叉或未找到返回 `false`；回调在调用线程同步执行 |
| `FDesyncDetector::SetDesyncCallback` | 绑定分叉报警回调 | 在 `VerifyRemoteChecksum` 调用线程同步触发 |
| `TRewindSnapshotBuffer::RecordSnapshot` | 写入历史快照 | `Capacity` 须为 2 的幂且 ≥2；非线程安全 |
| `TRewindSnapshotBuffer::PeekSnapshotAtFrame` | 按帧序号取只读快照 | 未命中返回 `nullptr` |
| `TRewindSnapshotBuffer::GetInterpolatedSnapshot` | 按时间戳插值查询快照 | 空缓冲或 `NaN` 时间戳返回 `false`；越界钳制到边界快照 |

## 1. 固定时间步长累加器 `FFixedTimestepAccumulator`

```cpp
class GAMECORE_API FFixedTimestepAccumulator {
public:
    explicit FFixedTimestepAccumulator(
        double InFixedDeltaTime = 1.0 / 60.0,
        int32 InMaxSubsteps = 4,
        double InMaxDeltaTime = 0.25
    );

    int32 Advance(double InDeltaSeconds, const TFunctionRef<void(double FixedDt)>& SubstepCallback);

    [[nodiscard]] double GetInterpolationAlpha() const noexcept;
    [[nodiscard]] double GetFixedDeltaTime() const noexcept;
    void SetFixedDeltaTime(double InFixedDeltaTime);
    [[nodiscard]] int32 GetMaxSubsteps() const noexcept;
    void SetMaxSubsteps(int32 InMaxSubsteps);
    [[nodiscard]] bool HasPendingSubstep() const noexcept;
    [[nodiscard]] double GetAccumulatedTime() const noexcept;
    void Reset() noexcept;
};
```

| 成员 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| 构造函数 | 固定步长（默认 `1/60` 秒）、最大子步数（默认 4）、单帧最大流逝保护（默认 `0.25` 秒） | 初始化累加器 |
| `Advance` | 本帧真实流逝秒数、固定子步回调（入参为 `FixedDt`） | 本次实际执行的子步次数 |
| `GetInterpolationAlpha` | 无 | `[0.0, 1.0]` 的未消费时间比例 |
| `HasPendingSubstep` | 无 | 累计时间足够触发至少一次子步返回 `true` |
| `GetAccumulatedTime` | 无 | 当前未消费的累计秒数 |

- **线程**：非线程安全，固定步进循环应在单一逻辑线程驱动。
- **生命周期**：`Reset` 清空未消费余量；`SetFixedDeltaTime` 对非有限或 ≤0 的输入静默忽略，`SetMaxSubsteps` 将小于 1 的输入钳制为 1。
- **失败语义**：`Advance` 对非有限或 ≤0 的 `InDeltaSeconds` 返回 0 且不推进；单帧触达 `MaxSubsteps` 后，超出整步的积压时间按 `FMath::Fmod(AccumulatedTime, FixedDeltaTime)` 截断丢弃，不补步、不报错。
- **行为事实**：`Advance` 先按有效最大帧时间 `max(FixedDeltaTime, InMaxDeltaTime)` 截断单帧输入，再以固定步长循环调用回调；渲染层用 `GetInterpolationAlpha` 在上一逻辑状态与当前逻辑状态间插值。

### 最小示例

```cpp
// 60Hz 步长，单帧最多补 4 步，最大流逝保护 0.25 秒
FFixedTimestepAccumulator PhysicsTicker(1.0 / 60.0, 4, 0.25);

void Tick(float DeltaSeconds) {
    const int32 Steps = PhysicsTicker.Advance(DeltaSeconds, [](double FixedDt) {
        SimulatePhysicsSubstep(FixedDt);
    });

    const double Alpha = PhysicsTicker.GetInterpolationAlpha();
    RenderMesh->SetWorldLocation(
        FMath::Lerp(PrevPhysicsLocation, CurrentPhysicsLocation, Alpha));
}
```

## 2. 状态校验器 `FStateChecksumBuilder`

```cpp
class GAMECORE_API FStateChecksumBuilder {
public:
    FStateChecksumBuilder() noexcept;

    void AddBool(bool bValue) noexcept;
    void AddInt32(int32 Value) noexcept;
    void AddInt64(int64 Value) noexcept;
    void AddUInt32(uint32 Value) noexcept;
    void AddUInt64(uint64 Value) noexcept;
    void AddString(FStringView Str) noexcept;
    void AddName(const FName& Name) noexcept;

    void AddFloatQuantized(float Value, float Precision = 0.001f) noexcept;
    void AddDoubleQuantized(double Value, double Precision = 0.001) noexcept;
    void AddVectorQuantized(const FVector& Vec, double Precision = 0.01) noexcept;
    void AddBytes(const void* Data, size_t Size) noexcept;

    [[nodiscard]] uint64 Finalize() const noexcept;
    void Reset() noexcept;
};
```

| 方法 | 参数 | 说明 |
| :--- | :--- | :--- |
| `AddBool` / `AddInt32` / `AddInt64` / `AddUInt32` / `AddUInt64` | 整型或布尔值 | 直接混入哈希状态机 |
| `AddString` | `FStringView` | 按字符底层字节连续注入，各端一致 |
| `AddName` | `FName` | 转为完整字符串内容注入，规避各端资产加载顺序不同导致的索引伪分叉 |
| `AddFloatQuantized` | 浮点值、精度（默认 `0.001f`） | 量化截断后注入，消除微小舍入抖动 |
| `AddDoubleQuantized` | 双精度值、精度（默认 `0.001`） | 保留大世界坐标精度 |
| `AddVectorQuantized` | 三维向量、精度（默认 `0.01`） | 原生双精度处理 LWC 坐标 |
| `AddBytes` | 数据首地址、字节数 | 注入原生二进制流 |
| `Finalize` | 无 | 输出最终 64 位校验码 |
| `Reset` | 无 | 重置哈希状态机以供复用 |

- **线程**：非线程安全，单线程内增量构建。
- **生命周期**：`Finalize` 为 `const`，不消耗状态，可多次读取；复用同一实例前应调用 `Reset`。
- **失败语义**：非法浮点（`NAN` / `INF`）使用内建非零 Sentinel 防御，避免与正常 `0.0` 产生伪同步碰撞；所有注入接口不抛异常。
- **性能**：全程零堆内存分配，单次计算为纳秒级寄存器运算（历史 Development 构建参考值见 [性能基准](../Architecture/Performance.md)），适合每逻辑步高频全量计算。
- **算法**：Mix64 终态散列混淆，任意 1-bit 输入变化引发输出哈希完全雪崩。

### 最小示例

```cpp
FStateChecksumBuilder Builder;

Builder.AddUInt32(EntityCount);
Builder.AddUInt64(GlobalSeed);
Builder.AddDoubleQuantized(WorldTimeSeconds, 0.001);
Builder.AddVectorQuantized(PlayerPosition, 0.01);
Builder.AddBytes(InventoryBytes.GetData(), InventoryBytes.Num());

const uint64 FrameChecksum = Builder.Finalize();
```

## 3. 分叉探测器 `FDesyncDetector`

```cpp
struct FDesyncReport {
    uint64 Frame = 0ULL;
    uint64 LocalChecksum = 0ULL;
    uint64 RemoteChecksum = 0ULL;
    bool bDiverged = false;
};

class GAMECORE_API FDesyncDetector {
public:
    DECLARE_DELEGATE_OneParam(FOnDesyncDetected, const FDesyncReport&);

    static constexpr uint32 HistoryCapacity = 128;

    void RecordLocalChecksum(uint64 Frame, uint64 Checksum) noexcept;
    bool VerifyRemoteChecksum(uint64 Frame, uint64 RemoteChecksum, FDesyncReport& OutReport);
    void SetDesyncCallback(FOnDesyncDetected InCallback);

    [[nodiscard]] constexpr uint32 GetCapacity() const noexcept;
    [[nodiscard]] bool HasFrame(uint64 Frame) const noexcept;
    [[nodiscard]] bool GetLocalChecksum(uint64 Frame, uint64& OutChecksum) const noexcept;
    void Reset() noexcept;
};
```

| 方法 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| `RecordLocalChecksum` | 帧序号、本地校验码 | 写入本地历史环 |
| `VerifyRemoteChecksum` | 远端帧序号、远端校验码、输出报告 | 本地存在对应帧且未分叉返回 `true`；分叉或未找到返回 `false` |
| `SetDesyncCallback` | `FOnDesyncDetected` 委托 | 绑定分叉报警回调 |
| `GetLocalChecksum` | 帧序号、输出校验码 | 存在有效记录返回 `true` |
| `HasFrame` | 帧序号 | 存在有效记录返回 `true` |
| `GetCapacity` / `Reset` | 无 | 历史容量 128 / 清空全部记录 |

- **线程**：非线程安全；回调在 `VerifyRemoteChecksum` 调用线程同步执行。
- **生命周期**：历史容量固定为 128 帧，超出后旧帧被环形覆盖；`Reset` 清空全部记录。
- **失败语义**：远端帧早于本地历史窗口或校验码不一致时返回 `false`；分叉时 `OutReport.bDiverged == true`，并输出帧号、本地码与远端码。
- **拷贝约束**：`FDesyncDetector` 拷贝构造与拷贝赋值被删除，允许移动。

### 最小示例

```cpp
FDesyncDetector Detector;

Detector.SetDesyncCallback(FDesyncDetector::FOnDesyncDetected::CreateLambda(
    [](const FDesyncReport& Report) {
        UE_LOG(LogTemp, Error, TEXT("DESYNC AT FRAME %llu! Local:0x%llX != Remote:0x%llX"),
            Report.Frame, Report.LocalChecksum, Report.RemoteChecksum);
        TriggerDesyncMemoryDump();
    }));

Detector.RecordLocalChecksum(CurrentFrame, LocalChecksum);

FDesyncReport Report;
if (!Detector.VerifyRemoteChecksum(RemoteFrame, RemoteChecksum, Report)) {
    if (Report.bDiverged) {
        // 已分叉：触发纠偏或断开重连
    }
}
```

## 4. 历史快照环形缓冲区 `TRewindSnapshotBuffer<TSnapshot, Capacity>`

```cpp
template <typename TSnapshot, uint32 Capacity = 128>
class TRewindSnapshotBuffer {
    static_assert((Capacity >= 2) && ((Capacity & (Capacity - 1)) == 0),
        "Capacity must be a power of two and >= 2");

public:
    using FInterpolator = TFunctionRef<TSnapshot(const TSnapshot& A, const TSnapshot& B, float Alpha)>;

    struct FSnapshotEntry {
        double Timestamp = 0.0;
        uint64 FrameSequence = 0ULL;
        TSnapshot State{};
        bool bValid = false;
    };

    void RecordSnapshot(double Timestamp, uint64 FrameSequence, const TSnapshot& InSnapshot);
    void RecordSnapshot(double Timestamp, uint64 FrameSequence, TSnapshot&& InSnapshot);

    template <typename... Args>
    TSnapshot& EmplaceSnapshot(double Timestamp, uint64 FrameSequence, Args&&... InArgs);

    [[nodiscard]] const TSnapshot* PeekSnapshotAtFrame(uint64 FrameSequence) const;
    [[nodiscard]] bool GetSnapshotAtFrame(uint64 FrameSequence, TSnapshot& OutSnapshot) const;
    [[nodiscard]] const TSnapshot* PeekLatestSnapshot() const;
    [[nodiscard]] bool GetLatestSnapshot(TSnapshot& OutSnapshot) const;
    [[nodiscard]] const TSnapshot* PeekOldestSnapshot() const;
    [[nodiscard]] bool GetOldestSnapshot(TSnapshot& OutSnapshot) const;

    [[nodiscard]] bool GetInterpolatedSnapshot(
        double TargetTimestamp,
        TSnapshot& OutSnapshot,
        FInterpolator Interpolator
    ) const;

    [[nodiscard]] int32 Num() const noexcept;
    [[nodiscard]] bool IsEmpty() const noexcept;
    [[nodiscard]] constexpr uint32 GetCapacity() const noexcept;
    void Clear() noexcept;
};
```

| 方法 | 参数 | 返回 / 说明 |
| :--- | :--- | :--- |
| `RecordSnapshot` | 时间戳（秒）、帧序号、快照（拷贝或移动） | 写入环形槽位，覆盖最旧帧 |
| `EmplaceSnapshot` | 时间戳、帧序号、构造参数包 | 槽位内原位构造的快照引用 |
| `PeekSnapshotAtFrame` / `GetSnapshotAtFrame` | 帧序号（与输出容器） | 命中返回只读指针或 `true`；否则 `nullptr` / `false` |
| `PeekLatestSnapshot` / `GetLatestSnapshot` | 无（与输出容器） | 最新快照只读指针或 `true`；无有效快照返回 `nullptr` / `false` |
| `PeekOldestSnapshot` / `GetOldestSnapshot` | 无（与输出容器） | 最旧快照只读指针或 `true`；无有效快照返回 `nullptr` / `false` |
| `GetInterpolatedSnapshot` | 目标时间戳、输出快照、插值委托 | 成功返回 `true`；空缓冲或时间戳非有限返回 `false` |
| `Num` / `IsEmpty` / `GetCapacity` / `Clear` | 无 | 有效快照数 / 是否为空 / 容量 / 清空 |

- **容量约束**：`Capacity` 必须为 2 的幂且 ≥2，默认 128；编译期 `static_assert` 校验，全程零堆分配。
- **线程**：非线程安全，单线程写入与查询。
- **生命周期**：环形覆写旧帧；`Clear` 将全部槽位置为无效并重置写游标。
- **时间戳守卫**：写入乱序且更小的时间戳会被钳制为上一帧时间戳，维持环内严格非递减。
- **寻址与插值**：连续帧序列下 `PeekSnapshotAtFrame` 通过 `SequenceDelta` 位运算 O(1) 直达，非连续帧回退环形扫描；`GetInterpolatedSnapshot` 以 O(log N) 二分定位区间，目标时间早于最旧或晚于最新时钳制到边界快照。
- **失败语义**：`GetInterpolatedSnapshot` 对 `NaN` 时间戳或空缓冲返回 `false`；插值区间时间差 ≤`1e-7` 时 `Alpha` 取 `0`。

### 最小示例

```cpp
struct FPlayerSnapshot {
    FVector Position = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
};

TRewindSnapshotBuffer<FPlayerSnapshot, 128> HistoryBuffer;
HistoryBuffer.RecordSnapshot(CurrentTimeSeconds, CurrentFrameSeq, PlayerSnapshot);

// 服务端延迟补偿：查询射击时刻的插值快照
FPlayerSnapshot RewoundState;
const bool bFound = HistoryBuffer.GetInterpolatedSnapshot(
    ShotTimeSeconds,
    RewoundState,
    [](const FPlayerSnapshot& A, const FPlayerSnapshot& B, float Alpha) {
        FPlayerSnapshot Result;
        Result.Position = FMath::Lerp(A.Position, B.Position, Alpha);
        Result.Rotation = FMath::Lerp(A.Rotation, B.Rotation, Alpha);
        return Result;
    });
```

## 常见错误与边界

- **期望补齐全部积压**：`Advance` 达到 `MaxSubsteps` 后会丢弃超出整步的积压时间，这是防螺旋死锁的既定行为，不是丢帧 Bug。
- **跨线程共享累加器**：`FFixedTimestepAccumulator`、`FStateChecksumBuilder`、`FDesyncDetector`、`TRewindSnapshotBuffer` 均无内部锁，跨线程并发访问会产生数据竞争。
- **校验器未重置**：同一 `FStateChecksumBuilder` 复用前必须 `Reset`，否则新状态会叠加到旧哈希上。
- **历史窗口过短**：`FDesyncDetector` 仅保留 128 帧，远端包延迟超过窗口时 `VerifyRemoteChecksum` 因找不到本地帧返回 `false`，不能据此判定分叉。
- **非单调时间戳**：`TRewindSnapshotBuffer` 会把更小时间戳钳制为上一帧时间戳，原始乱序时间点无法再被精确查询。
- **容量非法**：`TRewindSnapshotBuffer` 的 `Capacity` 非 2 的幂或小于 2 时编译期 `static_assert` 失败。

## 相关文档

- [插件 README](../../README.md)
- [架构概览](../Architecture/Overview.md)
- [快速上手](../Guides/QuickStart.md)
- [性能基准](../Architecture/Performance.md)
- [容器 API](Containers.md)
