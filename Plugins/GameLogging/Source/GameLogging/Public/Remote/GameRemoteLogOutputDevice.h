// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameRemoteLogOutputDevice.h
 * @brief 线程安全、防重入且零节点堆分配的远程日志捕获输出设备声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Array.h"
#include "Containers/TLockFreeRingBuffer.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Templates/UniquePtr.h"

#include <atomic>

/**
 * @brief 线程安全且防重入的远程日志捕获输出设备（接入 GLog 输出流）
 *
 * 内部复用 GameCore 的 TLockFreeRingBuffer（Vyukov MPMC 无锁环形缓冲区）消除逐条日志链表节点堆分配，
 * 并通过原子容量预留与内联读锁黑名单实现有界容量保护与零内存泄漏过滤。
 */
class GAMELOGGING_API FGameRemoteLogOutputDevice final : public FOutputDevice {
public:
    static constexpr int32 MAX_CAPACITY = 2048;

public:
    /**
     * @brief 默认构造函数，初始化为仅捕获 Warning 及以上日志并预置防自激黑名单
     */
    FGameRemoteLogOutputDevice();

    /**
     * @brief 析构函数，清空未消费日志队列
     */
    ~FGameRemoteLogOutputDevice() override;

    FGameRemoteLogOutputDevice(const FGameRemoteLogOutputDevice&) = delete;
    FGameRemoteLogOutputDevice& operator=(const FGameRemoteLogOutputDevice&) = delete;
    FGameRemoteLogOutputDevice(FGameRemoteLogOutputDevice&&) = delete;
    FGameRemoteLogOutputDevice& operator=(FGameRemoteLogOutputDevice&&) = delete;

    /**
     * @brief 声明该输出设备支持跨多工作线程并发调用
     * @return 恒定返回 true
     */
    [[nodiscard]] bool CanBeUsedOnMultipleThreads() const override;

    /**
     * @brief 日志序列化核心拦截入口（带时间戳重载）
     * @param V 日志正文原始字符串指针
     * @param Verbosity 日志详细级别
     * @param Category 日志分类名称
     * @param Time 发生时间戳（秒；小于 0 时在通过过滤后延迟读取平台时钟）
     */
    void Serialize(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category,
        const double Time
    ) override;

    /**
     * @brief 日志序列化拦截入口（不带时间戳重载，通过全部过滤后延迟补全平台时钟）
     * @param V 日志正文原始字符串指针
     * @param Verbosity 日志详细级别
     * @param Category 日志分类名称
     */
    void Serialize(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category
    ) override;

    /**
     * @brief 批量排出队列中累积的远程日志条目（并发安全）
     * @param OutBatch 接收批次条目的输出数组
     * @param MaxCount 单次最多排出的条目数量上限
     * @return 本次实际排出的条目数量
     */
    int32 DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount);

    /**
     * @brief 原地丢弃队列中至多 MaxCount 条日志（无监听者快速路径，零 TArray 分配）
     * @param MaxCount 单次最多丢弃的条目数量上限
     * @return 本次实际丢弃的条目数量
     */
    int32 DiscardBatch(int32 MaxCount);

    /**
     * @brief 动态调整远程收集的最低严重程度阈值
     * @param InMinVerbosity 最低严重程度（数值越小越严重，如 Warning 为 3；NoLogging 静默全部）
     */
    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;

    /**
     * @brief 查询当前生效的最低严重程度阈值
     * @return 当前最低详细级别
     */
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    /**
     * @brief 设定队列有效容量软上限（自动钳制至 [1, MAX_CAPACITY]，非正值忽略）
     * @param InCapacity 队列容量上限
     */
    void SetMaxCapacity(int32 InCapacity) noexcept;

    /**
     * @brief 查询当前生效的队列容量上限
     * @return 当前容量上限（范围 [1, MAX_CAPACITY]）
     */
    [[nodiscard]] int32 GetMaxCapacity() const noexcept;

    /**
     * @brief 快速判定底层无锁队列当前是否为空
     * @return 队列为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept;

    /**
     * @brief 查询当前队列中尚未排出的条目数量
     * @return 队列当前条目数
     */
    [[nodiscard]] int32 GetQueuedCount() const noexcept;

    /**
     * @brief 查询因队列满溢而被丢弃的条目累计总数
     * @return 丢弃累计数
     */
    [[nodiscard]] int64 GetDroppedCount() const noexcept;

    /**
     * @brief 添加需要忽略采集的特定日志分类
     * @param CategoryName 待忽略的分类名称（NAME_None 忽略）
     */
    void AddIgnoredCategory(FName CategoryName);

    /**
     * @brief 移除已忽略的特定日志分类
     * @param CategoryName 待恢复采集的分类名称（NAME_None 忽略）
     */
    void RemoveIgnoredCategory(FName CategoryName);

    /**
     * @brief 查询指定日志分类当前是否位于忽略黑名单中
     * @param CategoryName 待查询的分类名称
     * @return 位于忽略黑名单中返回 true
     */
    [[nodiscard]] bool IsCategoryIgnored(FName CategoryName) const;

    /**
     * @brief 并发安全清空当前队列并重置丢弃计数器
     */
    void Clear();

private:
    using FRingBuffer = TLockFreeRingBuffer<FGameRemoteLogEntry, static_cast<uint32>(MAX_CAPACITY)>;

    /**
     * @brief 内部核心入队处理（按 级别/控制符快速拦截 -> TLS 重入守卫 -> 内联黑名单 -> CAS 容量预留 -> 延迟时钟 -> 无锁环形写入 执行）
     * @param V 原始日志字符串
     * @param Verbosity 原始级别
     * @param Category 分类名
     * @param Time 时间戳秒数（小于 0 时延迟采集 FPlatformTime::Seconds）
     */
    void EnqueueLogInternal(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category,
        double Time
    );

    /**
     * @brief CAS 原子预留一个容量槽位，严格杜绝多线程突发写入超越 MaxCapacity
     * @return 预留成功返回 true，已达容量上限返回 false
     */
    [[nodiscard]] bool TryReserveCapacitySlot() noexcept;

private:
    TUniquePtr<FRingBuffer> RingBuffer;
    std::atomic<int32> QueuedCount{0};
    std::atomic<int32> MaxCapacity{MAX_CAPACITY};
    std::atomic<int64> DroppedCount{0};
    std::atomic<uint8> MinVerbosity{static_cast<uint8>(ELogVerbosity::Warning)};

    mutable FRWLock IgnoredCategoriesLock;
    TArray<FName, TInlineAllocator<8>> IgnoredCategories;
};
