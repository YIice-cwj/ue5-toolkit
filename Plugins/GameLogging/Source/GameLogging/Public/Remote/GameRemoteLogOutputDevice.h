// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "Containers/Set.h"
#include "HAL/CriticalSection.h"
#include "Misc/OutputDevice.h"
#include "Remote/GameRemoteLogTypes.h"

#include <atomic>

/**
 * @brief 线程安全且防重入的远程日志捕获输出设备（接入 GLog 输出流）
 */
class GAMELOGGING_API FGameRemoteLogOutputDevice final : public FOutputDevice {
public:
    static constexpr int32 DEFAULT_MAX_CAPACITY = 2048;

public:
    /**
     * @brief 默认构造函数，初始化为仅捕获 Warning 及以上日志
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
     * @param Time 发生时间戳（秒）
     */
    void Serialize(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category,
        const double Time
    ) override;

    /**
     * @brief 日志序列化拦截入口（不带时间戳重载）
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
     * @brief 批量排出队列中累积的远程日志条目（仅限游戏主线程调用）
     * @param OutBatch 接收批次条目的输出数组
     * @param MaxCount 单次最多排出的条目数量上限
     * @return 本次实际排出的条目数量
     */
    int32 DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount);

    /**
     * @brief 动态调整远程收集的最低严重程度阈值
     * @param InMinVerbosity 最低严重程度（数值越小越严重，如 Warning 为 3）
     */
    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;

    /**
     * @brief 查询当前生效的最低严重程度阈值
     * @return 当前最低详细级别
     */
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    /**
     * @brief 设定队列最大安全容量上限
     * @param InCapacity 队列容量上限（必须大于 0）
     */
    void SetMaxCapacity(int32 InCapacity) noexcept;

    /**
     * @brief 查询当前队列中尚未排出的条目估算数量
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
     * @param CategoryName 待忽略的分类名称
     */
    void AddIgnoredCategory(FName CategoryName);

    /**
     * @brief 移除已忽略的特定日志分类
     * @param CategoryName 待恢复采集的分类名称
     */
    void RemoveIgnoredCategory(FName CategoryName);

    /**
     * @brief 清空当前队列并重置丢弃计数器
     */
    void Clear();

private:
    /**
     * @brief 内部核心入队处理（校验阈值、忽略名单、防重入并压入无锁队列）
     * @param V 原始日志字符串
     * @param Verbosity 原始级别
     * @param Category 分类名
     * @param Time 时间戳秒数
     */
    void EnqueueLogInternal(
        const TCHAR* V,
        ELogVerbosity::Type Verbosity,
        const class FName& Category,
        double Time
    );

private:
    TQueue<FGameRemoteLogEntry, EQueueMode::Mpsc> LogQueue;
    std::atomic<int32> QueuedCount{0};
    std::atomic<int32> MaxCapacity{DEFAULT_MAX_CAPACITY};
    std::atomic<int64> DroppedCount{0};
    std::atomic<uint8> MinVerbosity{static_cast<uint8>(ELogVerbosity::Warning)};

    mutable FRWLock IgnoredCategoriesLock;
    TSet<FName> IgnoredCategories;
};

