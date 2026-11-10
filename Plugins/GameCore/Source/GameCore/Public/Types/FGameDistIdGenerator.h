/**
 * @file FGameDistIdGenerator.h
 * @brief 线程安全的 64 位分布式单调递增雪花唯一 ID 生成器
 */

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "Types/FGameDistId.h"

/**
 * @brief 线程安全的 64 位分布式单调递增雪花唯一 ID 生成器
 */
class GAMECORE_API FGameDistIdGenerator {
public:
    using FClockSource = TFunction<int64()>;

    static constexpr int64 DEFAULT_CUSTOM_EPOCH = 1735689600000LL;
    static constexpr int32 MAX_WAIT_SPINS = 1000;

public:
    FGameDistIdGenerator() = default;

    /**
     * @brief 构造并初始化生成器节点与自定义纪元
     * @param InNodeId 节点编号 [0, 1023]
     * @param InCustomEpoch 自定义基准纪元 Unix 毫秒时间戳
     */
    explicit FGameDistIdGenerator(uint16 InNodeId, int64 InCustomEpoch = DEFAULT_CUSTOM_EPOCH) noexcept;

    /**
     * @brief 初始化或重新配置生成器节点与基准纪元
     * @param InNodeId 节点编号 [0, 1023]
     * @param InCustomEpoch 自定义基准纪元 Unix 毫秒时间戳
     */
    void Initialize(uint16 InNodeId, int64 InCustomEpoch = DEFAULT_CUSTOM_EPOCH) noexcept;

    /**
     * @brief 注入自定义毫秒时钟源（供确定性测试覆盖序号溢出与时钟回拨分支，传空闭包恢复系统 UTC 时钟）
     * @param InClockSource 返回绝对 Unix 毫秒时间戳的回调
     */
    void SetClockSource(FClockSource InClockSource);

    /**
     * @brief 线程安全生成下一个全局单调递增唯一 ID（时钟停滞/大幅回拨且单毫秒序号溢出时自动推进逻辑毫秒，绝不死锁）
     * @return 分布式唯一 ID（若时间戳为负或超出 41 位上限返回无效 ID）
     */
    [[nodiscard]] FGameDistId NextId();

    /**
     * @brief 查询当前分配的节点编号
     * @return 节点编号
     */
    [[nodiscard]] uint16 GetNodeId() const noexcept;

    /**
     * @brief 查询当前采用的基准纪元
     * @return 基准纪元毫秒数
     */
    [[nodiscard]] int64 GetCustomEpoch() const noexcept;

    /**
     * @brief 获取全局默认单例生成器引用
     * @return 全局单例引用
     */
    [[nodiscard]] static FGameDistIdGenerator& Get();

private:
    /**
     * @brief 获取当前绝对 Unix 毫秒时间戳（优先调用注入的自定义时钟源）
     * @return 当前毫秒时间戳
     */
    [[nodiscard]] int64 GetCurrentTimeMs() const;

    /**
     * @brief 自旋等待直至平台毫秒时钟推进到下一毫秒（超过最大自旋次数时自动推进逻辑毫秒防死锁）
     * @param InLastTimestamp 上一次已分配的相对毫秒时间戳
     * @return 推进后的相对毫秒时间戳
     */
    [[nodiscard]] int64 WaitUntilNextMillis(int64 InLastTimestamp) const;

private:
    mutable FCriticalSection GeneratorLock;
    FClockSource CustomClockSource;
    int64 CustomEpoch = DEFAULT_CUSTOM_EPOCH;
    int64 LastTimestampDelta = -1;
    uint16 NodeId = 0;
    uint16 Sequence = 0;
};
