/**
 * @file FGameDistIdGenerator.cpp
 * @brief 64 位分布式单调递增雪花唯一 ID 生成器实现
 */

#include "Types/FGameDistIdGenerator.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/ScopeLock.h"

/**
 * @brief 构造函数：初始化节点与自定义纪元
 * @param InNodeId 节点编号
 * @param InCustomEpoch 自定义基准纪元 Unix 毫秒时间戳
 */
FGameDistIdGenerator::FGameDistIdGenerator(uint16 InNodeId, int64 InCustomEpoch) noexcept
    : CustomEpoch(InCustomEpoch)
    , NodeId(InNodeId & FGameDistId::MAX_NODE_ID) {}

/**
 * @brief 初始化或重新配置生成器节点与基准纪元
 * @param InNodeId 节点编号
 * @param InCustomEpoch 自定义基准纪元 Unix 毫秒时间戳
 */
void FGameDistIdGenerator::Initialize(uint16 InNodeId, int64 InCustomEpoch) noexcept {
    FScopeLock Lock(&GeneratorLock);
    CustomEpoch = InCustomEpoch;
    NodeId = InNodeId & FGameDistId::MAX_NODE_ID;
    Sequence = 0;
    LastTimestampDelta = -1;
}

/**
 * @brief 注入自定义毫秒时钟源（供确定性测试覆盖序号溢出与时钟回拨分支，传空闭包恢复系统 UTC 时钟）
 * @param InClockSource 返回绝对 Unix 毫秒时间戳的回调
 */
void FGameDistIdGenerator::SetClockSource(FClockSource InClockSource) {
    FScopeLock Lock(&GeneratorLock);
    CustomClockSource = MoveTemp(InClockSource);
}

/**
 * @brief 线程安全生成下一个全局单调递增唯一 ID（时钟停滞/大幅回拨且单毫秒序号溢出时自动推进逻辑毫秒，绝不死锁）
 * @return 分布式唯一 ID（若时间戳为负或超出 41 位上限返回无效 ID）
 */
FGameDistId FGameDistIdGenerator::NextId() {
    FScopeLock Lock(&GeneratorLock);

    int64 CurrentDelta = GetCurrentTimeMs() - CustomEpoch;

    if (CurrentDelta < LastTimestampDelta) {
        const int64 Drift = LastTimestampDelta - CurrentDelta;
        if (Drift <= 5) {
            for (int32 Spin = 0; Spin < 100 && CurrentDelta < LastTimestampDelta; ++Spin) {
                FPlatformProcess::Yield();
                CurrentDelta = GetCurrentTimeMs() - CustomEpoch;
            }
        }
        if (CurrentDelta < LastTimestampDelta) {
            CurrentDelta = LastTimestampDelta;
        }
    }

    if (CurrentDelta < 0 || static_cast<uint64>(CurrentDelta) > FGameDistId::MAX_TIMESTAMP_DELTA) {
        return FGameDistId(0ULL);
    }

    if (CurrentDelta == LastTimestampDelta) {
        Sequence = (Sequence + 1U) & FGameDistId::MAX_SEQUENCE;
        if (Sequence == 0) {
            CurrentDelta = WaitUntilNextMillis(LastTimestampDelta);
            if (CurrentDelta < 0 || static_cast<uint64>(CurrentDelta) > FGameDistId::MAX_TIMESTAMP_DELTA) {
                return FGameDistId(0ULL);
            }
        }
    } else {
        Sequence = 0;
    }

    LastTimestampDelta = CurrentDelta;
    return FGameDistId::FromParts(CurrentDelta, NodeId, Sequence);
}

/**
 * @brief 查询当前分配的节点编号
 * @return 节点编号
 */
uint16 FGameDistIdGenerator::GetNodeId() const noexcept {
    FScopeLock Lock(&GeneratorLock);
    return NodeId;
}

/**
 * @brief 查询当前采用的基准纪元
 * @return 基准纪元毫秒数
 */
int64 FGameDistIdGenerator::GetCustomEpoch() const noexcept {
    FScopeLock Lock(&GeneratorLock);
    return CustomEpoch;
}

/**
 * @brief 获取全局默认单例生成器引用
 * @return 全局单例引用
 */
FGameDistIdGenerator& FGameDistIdGenerator::Get() {
    static FGameDistIdGenerator Instance;
    return Instance;
}

/**
 * @brief 获取当前系统或注入时钟源的 Unix 毫秒时间戳
 * @return 绝对 Unix 毫秒时间戳
 */
int64 FGameDistIdGenerator::GetCurrentTimeMs() const {
    if (CustomClockSource) {
        return CustomClockSource();
    }
    const FDateTime Now = FDateTime::UtcNow();
    return Now.ToUnixTimestamp() * 1000LL + Now.GetMillisecond();
}

/**
 * @brief 有界自旋等待至下一毫秒以解决序号溢出（若物理时钟停滞或处于大幅回拨期则单调推进逻辑毫秒）
 * @param InLastTimestamp 上次发生溢出的时间戳相对值
 * @return 推进后的新时间戳相对值
 */
int64 FGameDistIdGenerator::WaitUntilNextMillis(int64 InLastTimestamp) const {
    int64 Timestamp = GetCurrentTimeMs() - CustomEpoch;
    for (int32 Spin = 0; Spin < MAX_WAIT_SPINS && Timestamp <= InLastTimestamp; ++Spin) {
        FPlatformProcess::Yield();
        Timestamp = GetCurrentTimeMs() - CustomEpoch;
    }
    if (Timestamp <= InLastTimestamp) {
        Timestamp = InLastTimestamp + 1LL;
    }
    return Timestamp;
}
