/**
 * @file TRewindSnapshotBuffer.h
 * @brief 定长循环历史快照环形缓冲区与亚帧平滑插值器
 */

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"

/**
 * @brief 模板化定长历史快照环形缓冲区
 *
 * 专为联机网络预测、回滚对齐（Reconciliation）与射击延迟补偿（Lag Compensation）设计：
 * 1. 编译期确定容量且必须为 2 的幂次，基于位掩码实现严格 O(1) 环形写入、首尾寻址与连续帧号直达查询；
 * 2. 0 运行时动态堆内存分配，覆写旧帧，零内存抖动；
 * 3. 内置亚帧（Sub-frame）连续时间戳定位与插值委托。
 *
 * @tparam TSnapshot 快照数据载荷类型
 * @tparam Capacity 环形缓冲区槽位上限（必须为 2 的幂且 >= 2）
 */
template <typename TSnapshot, uint32 Capacity = 128>
class TRewindSnapshotBuffer {
    static_assert((Capacity >= 2) && ((Capacity & (Capacity - 1)) == 0), "Capacity must be a power of two and >= 2");

public:
    using FInterpolator = TFunctionRef<TSnapshot(const TSnapshot& A, const TSnapshot& B, float Alpha)>;

    /**
     * @brief 快照槽位元数据包
     */
    struct FSnapshotEntry {
        double Timestamp = 0.0;
        uint64 FrameSequence = 0ULL;
        TSnapshot State{};
        bool bValid = false;
    };

public:
    TRewindSnapshotBuffer() = default;
    ~TRewindSnapshotBuffer() = default;

    TRewindSnapshotBuffer(const TRewindSnapshotBuffer&) = default;
    TRewindSnapshotBuffer& operator=(const TRewindSnapshotBuffer&) = default;
    TRewindSnapshotBuffer(TRewindSnapshotBuffer&&) noexcept = default;
    TRewindSnapshotBuffer& operator=(TRewindSnapshotBuffer&&) noexcept = default;

    /**
     * @brief 写入新的帧快照（拷贝语义，自动保证环内时间戳单调非递减不变式，乱序较小时间戳钳制为上一帧时间戳）
     * @param Timestamp 仿真物理时间戳（秒）
     * @param FrameSequence 递增帧序号
     * @param InSnapshot 目标快照数据
     */
    void RecordSnapshot(double Timestamp, uint64 FrameSequence, const TSnapshot& InSnapshot) {
        const uint32 Slot = PrepareNextSlot(Timestamp, FrameSequence);
        Entries[Slot].State = InSnapshot;
    }

    /**
     * @brief 写入新的帧快照（移动语义，自动保证环内时间戳单调非递减不变式，乱序较小时间戳钳制为上一帧时间戳）
     * @param Timestamp 仿真物理时间戳（秒）
     * @param FrameSequence 递增帧序号
     * @param InSnapshot 目标快照数据（右值）
     */
    void RecordSnapshot(double Timestamp, uint64 FrameSequence, TSnapshot&& InSnapshot) {
        const uint32 Slot = PrepareNextSlot(Timestamp, FrameSequence);
        Entries[Slot].State = MoveTemp(InSnapshot);
    }

    /**
     * @brief 原位构造写入新的帧快照（避免临时对象的构造与移动开销）
     * @tparam Args 构造参数包
     * @param Timestamp 仿真物理时间戳（秒）
     * @param FrameSequence 递增帧序号
     * @param InArgs 快照构造参数
     * @return 槽位内原位构造的快照引用
     */
    template <typename... Args>
    TSnapshot& EmplaceSnapshot(double Timestamp, uint64 FrameSequence, Args&&... InArgs) {
        const uint32 Slot = PrepareNextSlot(Timestamp, FrameSequence);
        Entries[Slot].State = TSnapshot(Forward<Args>(InArgs)...);
        return Entries[Slot].State;
    }

    /**
     * @brief 按物理帧序号精确探测快照只读指针（连续帧序列下优先 O(1) 偏移直达，非连续帧序列回退环形扫描）
     * @param FrameSequence 期望查询的帧序号
     * @return 存在有效快照返回只读指针，否则返回 nullptr
     */
    [[nodiscard]] const TSnapshot* PeekSnapshotAtFrame(uint64 FrameSequence) const {
        if (ValidCount == 0) {
            return nullptr;
        }

        const FSnapshotEntry& Newest = Entries[HeadIndex];
        if (Newest.bValid && FrameSequence <= Newest.FrameSequence) {
            const uint64 SequenceDelta = Newest.FrameSequence - FrameSequence;
            if (SequenceDelta < static_cast<uint64>(ValidCount)) {
                const uint32 DirectSlot = (HeadIndex + Capacity - static_cast<uint32>(SequenceDelta)) & Mask;
                if (Entries[DirectSlot].bValid && Entries[DirectSlot].FrameSequence == FrameSequence) {
                    return &Entries[DirectSlot].State;
                }
            }
        }

        for (int32 Offset = 0; Offset < ValidCount; ++Offset) {
            const uint32 Slot = (HeadIndex + Capacity - static_cast<uint32>(Offset)) & Mask;
            if (Entries[Slot].bValid && Entries[Slot].FrameSequence == FrameSequence) {
                return &Entries[Slot].State;
            }
        }

        return nullptr;
    }

    /**
     * @brief 按物理帧序号精确获取快照（连续帧序列下优先 O(1) 偏移直达，非连续帧序列回退环形扫描）
     * @param FrameSequence 期望查询的帧序号
     * @param OutSnapshot 输出快照容器
     * @return 存在且有效返回 true
     */
    [[nodiscard]] bool GetSnapshotAtFrame(uint64 FrameSequence, TSnapshot& OutSnapshot) const {
        const TSnapshot* Found = PeekSnapshotAtFrame(FrameSequence);
        if (Found != nullptr) {
            OutSnapshot = *Found;
            return true;
        }
        return false;
    }

    /**
     * @brief 按物理帧序号精确获取快照（现代可选值包装）
     * @param FrameSequence 期望查询的帧序号
     * @return 包含快照的 Optional 实例；若不存在返回空 Optional
     */
    [[nodiscard]] TOptional<TSnapshot> GetSnapshotAtFrame(uint64 FrameSequence) const {
        if (const TSnapshot* Found = PeekSnapshotAtFrame(FrameSequence)) {
            return *Found;
        }
        return {};
    }

    /**
     * @brief 获取最新写入的快照只读指针（零拷贝）
     * @return 存在有效快照返回只读指针，否则返回 nullptr
     */
    [[nodiscard]] const TSnapshot* PeekLatestSnapshot() const {
        if (ValidCount == 0 || !Entries[HeadIndex].bValid) {
            return nullptr;
        }
        return &Entries[HeadIndex].State;
    }

    /**
     * @brief 获取最新写入的快照
     * @param OutSnapshot 输出快照容器
     * @return 存在有效快照返回 true
     */
    [[nodiscard]] bool GetLatestSnapshot(TSnapshot& OutSnapshot) const {
        const TSnapshot* Found = PeekLatestSnapshot();
        if (Found != nullptr) {
            OutSnapshot = *Found;
            return true;
        }
        return false;
    }

    /**
     * @brief 获取最新写入的快照（现代可选值包装）
     * @return 包含快照的 Optional 实例；若不存在返回空 Optional
     */
    [[nodiscard]] TOptional<TSnapshot> GetLatestSnapshot() const {
        if (const TSnapshot* Found = PeekLatestSnapshot()) {
            return *Found;
        }
        return {};
    }

    /**
     * @brief 获取最早尚未被覆盖的历史快照只读指针（零拷贝）
     * @return 存在有效快照返回只读指针，否则返回 nullptr
     */
    [[nodiscard]] const TSnapshot* PeekOldestSnapshot() const {
        if (ValidCount == 0) {
            return nullptr;
        }

        const uint32 TailIndex = (HeadIndex + Capacity - static_cast<uint32>(ValidCount - 1)) & Mask;
        if (!Entries[TailIndex].bValid) {
            return nullptr;
        }
        return &Entries[TailIndex].State;
    }

    /**
     * @brief 获取最早尚未被覆盖的历史快照
     * @param OutSnapshot 输出快照容器
     * @return 存在有效快照返回 true
     */
    [[nodiscard]] bool GetOldestSnapshot(TSnapshot& OutSnapshot) const {
        const TSnapshot* Found = PeekOldestSnapshot();
        if (Found != nullptr) {
            OutSnapshot = *Found;
            return true;
        }
        return false;
    }

    /**
     * @brief 获取最早尚未被覆盖的历史快照（现代可选值包装）
     * @return 包含快照的 Optional 实例；若不存在返回空 Optional
     */
    [[nodiscard]] TOptional<TSnapshot> GetOldestSnapshot() const {
        if (const TSnapshot* Found = PeekOldestSnapshot()) {
            return *Found;
        }
        return {};
    }

    /**
     * @brief 依据连续时间戳获取亚帧平滑插值快照（基于环内单调时间戳执行 O(log N) 二分查找定位区间）
     * @param TargetTimestamp 目标插值时间戳
     * @param OutSnapshot 输出快照容器
     * @param Interpolator 元素插值委托 (A, B, Alpha) -> TSnapshot
     * @return 成功计算插值返回 true；缓冲区为空或传入非有限时间戳 (NaN) 返回 false
     */
    [[nodiscard]] bool GetInterpolatedSnapshot(
        double TargetTimestamp,
        TSnapshot& OutSnapshot,
        FInterpolator Interpolator
    ) const {
        if (ValidCount == 0 || !FMath::IsFinite(TargetTimestamp)) {
            return false;
        }

        const FSnapshotEntry& Newest = Entries[HeadIndex];
        if (TargetTimestamp >= Newest.Timestamp || ValidCount == 1) {
            OutSnapshot = Newest.State;
            return true;
        }

        const uint32 TailIndex = (HeadIndex + Capacity - static_cast<uint32>(ValidCount - 1)) & Mask;
        const FSnapshotEntry& Oldest = Entries[TailIndex];
        if (TargetTimestamp <= Oldest.Timestamp) {
            OutSnapshot = Oldest.State;
            return true;
        }

        int32 Low = 0;
        int32 High = ValidCount - 1;
        while (Low + 1 < High) {
            const int32 Mid = Low + ((High - Low) >> 1);
            const uint32 MidSlot = (TailIndex + static_cast<uint32>(Mid)) & Mask;
            if (Entries[MidSlot].Timestamp <= TargetTimestamp) {
                Low = Mid;
            } else {
                High = Mid;
            }
        }

        const uint32 IndexA = (TailIndex + static_cast<uint32>(Low)) & Mask;
        const uint32 IndexB = (TailIndex + static_cast<uint32>(High)) & Mask;
        const FSnapshotEntry& EntryA = Entries[IndexA];
        const FSnapshotEntry& EntryB = Entries[IndexB];

        const double TimeDelta = EntryB.Timestamp - EntryA.Timestamp;
        const float Alpha = TimeDelta > 1e-7
            ? FMath::Clamp(static_cast<float>((TargetTimestamp - EntryA.Timestamp) / TimeDelta), 0.0f, 1.0f)
            : 0.0f;

        OutSnapshot = Interpolator(EntryA.State, EntryB.State, Alpha);
        return true;
    }

    /**
     * @brief 依据连续时间戳获取亚帧平滑插值快照（现代可选值包装）
     * @param TargetTimestamp 目标插值时间戳
     * @param Interpolator 元素插值委托 (A, B, Alpha) -> TSnapshot
     * @return 包含插值快照的 Optional 实例；若无法插值返回空 Optional
     */
    [[nodiscard]] TOptional<TSnapshot> GetInterpolatedSnapshot(
        double TargetTimestamp,
        FInterpolator Interpolator
    ) const {
        TSnapshot OutSnapshot{};
        if (GetInterpolatedSnapshot(TargetTimestamp, OutSnapshot, Interpolator)) {
            return OutSnapshot;
        }
        return {};
    }

    /**
     * @brief 获取当前有效的快照记录数
     * @return 有效槽位数
     */
    [[nodiscard]] int32 Num() const noexcept {
        return ValidCount;
    }

    /**
     * @brief 查询缓冲区是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return ValidCount == 0;
    }

    /**
     * @brief 获取缓冲区最大容量
     * @return 容量常量
     */
    [[nodiscard]] constexpr uint32 GetCapacity() const noexcept {
        return Capacity;
    }

    /**
     * @brief 清空快照缓冲区
     */
    void Clear() noexcept {
        for (uint32 Index = 0; Index < Capacity; ++Index) {
            Entries[Index].bValid = false;
        }
        HeadIndex = Capacity - 1;
        ValidCount = 0;
    }

private:
    uint32 PrepareNextSlot(double Timestamp, uint64 FrameSequence) noexcept {
        double SafeTimestamp = FMath::IsFinite(Timestamp) ? Timestamp : 0.0;
        if (ValidCount > 0 && Entries[HeadIndex].bValid && SafeTimestamp < Entries[HeadIndex].Timestamp) {
            SafeTimestamp = Entries[HeadIndex].Timestamp;
        }

        HeadIndex = (HeadIndex + 1) & Mask;

        Entries[HeadIndex].Timestamp = SafeTimestamp;
        Entries[HeadIndex].FrameSequence = FrameSequence;
        Entries[HeadIndex].bValid = true;

        if (ValidCount < static_cast<int32>(Capacity)) {
            ++ValidCount;
        }

        return HeadIndex;
    }

    static constexpr uint32 Mask = Capacity - 1;

    FSnapshotEntry Entries[Capacity];
    uint32 HeadIndex = Capacity - 1;
    int32 ValidCount = 0;
};
