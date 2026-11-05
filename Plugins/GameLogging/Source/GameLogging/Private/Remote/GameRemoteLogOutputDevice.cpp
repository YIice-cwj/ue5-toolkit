// Copyright Epic Games, Inc. All Rights Reserved.

#include "Remote/GameRemoteLogOutputDevice.h"

#include "HAL/PlatformTime.h"

namespace {

struct FRemoteLogReentrancyScope {
    FRemoteLogReentrancyScope() {
        bHandling = true;
    }

    ~FRemoteLogReentrancyScope() {
        bHandling = false;
    }

    static thread_local bool bHandling;
};

thread_local bool FRemoteLogReentrancyScope::bHandling = false;

} // namespace

/**
 * @brief 构造函数实现：设置默认级别阈值与默认忽略的反射循环分类
 */
FGameRemoteLogOutputDevice::FGameRemoteLogOutputDevice() {
    IgnoredCategories.Add(FName(TEXT("LogGameplayMessage")));
    IgnoredCategories.Add(FName(TEXT("LogGameLogging")));
    IgnoredCategories.Add(FName(TEXT("LogGameNetService")));
}

/**
 * @brief 析构函数实现：清空队列中待发条目
 */
FGameRemoteLogOutputDevice::~FGameRemoteLogOutputDevice() {
    Clear();
}

/**
 * @brief 声明支持多线程调用实现
 * @return 恒为 true
 */
bool FGameRemoteLogOutputDevice::CanBeUsedOnMultipleThreads() const {
    return true;
}

/**
 * @brief 拦截带显式时间戳日志
 * @param V 日志文本指针
 * @param Verbosity 详细级别
 * @param Category 分类名称
 * @param Time 时间戳
 */
void FGameRemoteLogOutputDevice::Serialize(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category,
    const double Time
) {
    EnqueueLogInternal(V, Verbosity, Category, Time);
}

/**
 * @brief 拦截未指定时间戳日志（由平台时钟补充）
 * @param V 日志文本指针
 * @param Verbosity 详细级别
 * @param Category 分类名称
 */
void FGameRemoteLogOutputDevice::Serialize(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category
) {
    EnqueueLogInternal(V, Verbosity, Category, FPlatformTime::Seconds());
}

/**
 * @brief 核心入队逻辑实现（涵盖级别过滤、防重入、忽略名单排查与队列上限保护）
 * @param V 日志正文
 * @param Verbosity 日志级别
 * @param Category 分类
 * @param Time 时间戳
 */
void FGameRemoteLogOutputDevice::EnqueueLogInternal(
    const TCHAR* V,
    ELogVerbosity::Type Verbosity,
    const class FName& Category,
    double Time
) {
    if (FRemoteLogReentrancyScope::bHandling) {
        return;
    }
    const FRemoteLogReentrancyScope ReentrancyGuard;

    const uint8 PureVerbosity = static_cast<uint8>(Verbosity & ELogVerbosity::VerbosityMask);
    if (PureVerbosity > MinVerbosity.load(std::memory_order_relaxed)) {
        return;
    }

    {
        FRWScopeLock ReadLock(IgnoredCategoriesLock, SLT_ReadOnly);
        if (IgnoredCategories.Contains(Category)) {
            return;
        }
    }

    const int32 CurrentCapacity = MaxCapacity.load(std::memory_order_relaxed);
    if (QueuedCount.load(std::memory_order_relaxed) >= CurrentCapacity) {
        DroppedCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const int64 Frame = static_cast<int64>(GFrameCounter);
    FString Content(V != nullptr ? V : TEXT(""));
    LogQueue.Enqueue(FGameRemoteLogEntry(Category, PureVerbosity, MoveTemp(Content), Time, Frame));
    QueuedCount.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 批量排出日志队列条目实现
 * @param OutBatch 目标数组
 * @param MaxCount 最大排出数量
 * @return 实际排出数量
 */
int32 FGameRemoteLogOutputDevice::DrainBatch(TArray<FGameRemoteLogEntry>& OutBatch, int32 MaxCount) {
    if (MaxCount <= 0) {
        return 0;
    }

    const int32 CurrentQueued = QueuedCount.load(std::memory_order_relaxed);
    const int32 ExpectedCount = FMath::Min(MaxCount, FMath::Max(0, CurrentQueued));
    if (ExpectedCount > 0) {
        OutBatch.Reserve(OutBatch.Num() + ExpectedCount);
    }

    int32 Drained = 0;
    while (Drained < MaxCount) {
        FGameRemoteLogEntry Item;
        if (!LogQueue.Dequeue(Item)) {
            break;
        }
        OutBatch.Add(MoveTemp(Item));
        QueuedCount.fetch_sub(1, std::memory_order_relaxed);
        ++Drained;
    }

    return Drained;
}

/**
 * @brief 设置最低采集级别阈值实现
 * @param InMinVerbosity 阈值枚举
 */
void FGameRemoteLogOutputDevice::SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept {
    MinVerbosity.store(
        static_cast<uint8>(InMinVerbosity & ELogVerbosity::VerbosityMask),
        std::memory_order_relaxed
    );
}

/**
 * @brief 查询当前最低采集级别阈值实现
 * @return 阈值枚举
 */
ELogVerbosity::Type FGameRemoteLogOutputDevice::GetMinVerbosity() const noexcept {
    return static_cast<ELogVerbosity::Type>(MinVerbosity.load(std::memory_order_relaxed));
}

/**
 * @brief 设定队列安全容量上限实现
 * @param InCapacity 容量数值
 */
void FGameRemoteLogOutputDevice::SetMaxCapacity(int32 InCapacity) noexcept {
    if (InCapacity > 0) {
        MaxCapacity.store(InCapacity, std::memory_order_relaxed);
    }
}

/**
 * @brief 查询队列条目估算数量实现
 * @return 条目数
 */
int32 FGameRemoteLogOutputDevice::GetQueuedCount() const noexcept {
    return FMath::Max(0, QueuedCount.load(std::memory_order_relaxed));
}

/**
 * @brief 查询丢弃累计数实现
 * @return 丢弃计数
 */
int64 FGameRemoteLogOutputDevice::GetDroppedCount() const noexcept {
    return DroppedCount.load(std::memory_order_relaxed);
}

/**
 * @brief 添加忽略日志分类实现
 * @param CategoryName 分类名称
 */
void FGameRemoteLogOutputDevice::AddIgnoredCategory(FName CategoryName) {
    if (!CategoryName.IsNone()) {
        FRWScopeLock WriteLock(IgnoredCategoriesLock, SLT_Write);
        IgnoredCategories.Add(CategoryName);
    }
}

/**
 * @brief 移除忽略日志分类实现
 * @param CategoryName 分类名称
 */
void FGameRemoteLogOutputDevice::RemoveIgnoredCategory(FName CategoryName) {
    if (!CategoryName.IsNone()) {
        FRWScopeLock WriteLock(IgnoredCategoriesLock, SLT_Write);
        IgnoredCategories.Remove(CategoryName);
    }
}

/**
 * @brief 清空队列并重置状态实现
 */
void FGameRemoteLogOutputDevice::Clear() {
    FGameRemoteLogEntry Dump;
    while (LogQueue.Dequeue(Dump)) {
    }
    QueuedCount.store(0, std::memory_order_relaxed);
    DroppedCount.store(0, std::memory_order_relaxed);
}
