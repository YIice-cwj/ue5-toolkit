// Copyright Epic Games, Inc. All Rights Reserved.

#include "Remote/GameRemoteLogOutputDevice.h"

#include "HAL/PlatformTime.h"

namespace {
thread_local bool bThreadIsHandlingRemoteLog = false;
} // namespace

/**
 * @brief 构造守卫并记录进入当前线程重入状态
 */
FRemoteLogReentrancyScope::FRemoteLogReentrancyScope()
    : bPreviousState(bThreadIsHandlingRemoteLog) {
    bThreadIsHandlingRemoteLog = true;
}

/**
 * @brief 析构守卫并恢复前序重入状态
 */
FRemoteLogReentrancyScope::~FRemoteLogReentrancyScope() {
    bThreadIsHandlingRemoteLog = bPreviousState;
}

/**
 * @brief 查询当前调用线程是否处于日志拦截或派发重入上下文中
 * @return 处于重入上下文中返回 true
 */
bool FRemoteLogReentrancyScope::IsHandling() noexcept {
    return bThreadIsHandlingRemoteLog;
}

/**
 * @brief 构造函数实现：设置默认级别阈值与默认忽略的反射循环分类
 */
FGameRemoteLogOutputDevice::FGameRemoteLogOutputDevice() {
    FIgnoredCategoriesSnapshot* Initial = new FIgnoredCategoriesSnapshot();
    Initial->Categories.Add(FName(TEXT("LogGameplayMessage")));
    Initial->Categories.Add(FName(TEXT("LogGameLogging")));
    Initial->Categories.Add(FName(TEXT("LogGameNetService")));
    IgnoredSnapshot.store(Initial, std::memory_order_release);
}

/**
 * @brief 析构函数实现：清空队列中待发条目并释放快照内存
 */
FGameRemoteLogOutputDevice::~FGameRemoteLogOutputDevice() {
    Clear();
    const FIgnoredCategoriesSnapshot* Current = IgnoredSnapshot.exchange(nullptr, std::memory_order_acq_rel);
    delete Current;
    RetiredSnapshots.Empty();
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
 * @brief 核心入队逻辑实现（涵盖级别过滤、防重入、COW 无锁忽略名单排查与队列上限保护）
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
    if (FRemoteLogReentrancyScope::IsHandling()) {
        return;
    }
    const FRemoteLogReentrancyScope ReentrancyGuard;

    const uint8 PureVerbosity = static_cast<uint8>(Verbosity & ELogVerbosity::VerbosityMask);
    if (PureVerbosity > MinVerbosity.load(std::memory_order_relaxed)) {
        return;
    }

    if (const FIgnoredCategoriesSnapshot* Snapshot = IgnoredSnapshot.load(std::memory_order_acquire)) {
        if (Snapshot->Categories.Contains(Category)) {
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
 * @brief 批量排出日志队列条目实现（合并单次原子扣减）
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
        ++Drained;
    }

    if (Drained > 0) {
        QueuedCount.fetch_sub(Drained, std::memory_order_relaxed);
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
 * @brief 添加忽略日志分类实现（COW 无锁快照替换）
 * @param CategoryName 分类名称
 */
void FGameRemoteLogOutputDevice::AddIgnoredCategory(FName CategoryName) {
    if (CategoryName.IsNone()) {
        return;
    }
    FScopeLock Lock(&IgnoredCategoriesMutex);
    const FIgnoredCategoriesSnapshot* Old = IgnoredSnapshot.load(std::memory_order_relaxed);
    if (Old != nullptr && Old->Categories.Contains(CategoryName)) {
        return;
    }

    FIgnoredCategoriesSnapshot* NewSnapshot = new FIgnoredCategoriesSnapshot();
    if (Old != nullptr) {
        NewSnapshot->Categories = Old->Categories;
    }
    NewSnapshot->Categories.Add(CategoryName);
    IgnoredSnapshot.store(NewSnapshot, std::memory_order_release);
    if (Old != nullptr) {
        RetiredSnapshots.Add(TUniquePtr<const FIgnoredCategoriesSnapshot>(Old));
    }
}

/**
 * @brief 移除忽略日志分类实现（COW 无锁快照替换）
 * @param CategoryName 分类名称
 */
void FGameRemoteLogOutputDevice::RemoveIgnoredCategory(FName CategoryName) {
    if (CategoryName.IsNone()) {
        return;
    }
    FScopeLock Lock(&IgnoredCategoriesMutex);
    const FIgnoredCategoriesSnapshot* Old = IgnoredSnapshot.load(std::memory_order_relaxed);
    if (Old == nullptr || !Old->Categories.Contains(CategoryName)) {
        return;
    }

    FIgnoredCategoriesSnapshot* NewSnapshot = new FIgnoredCategoriesSnapshot();
    NewSnapshot->Categories = Old->Categories;
    NewSnapshot->Categories.Remove(CategoryName);
    IgnoredSnapshot.store(NewSnapshot, std::memory_order_release);
    RetiredSnapshots.Add(TUniquePtr<const FIgnoredCategoriesSnapshot>(Old));
}

/**
 * @brief 清空队列并重置状态实现（支持并发入队条目计数平滑扣减）
 */
void FGameRemoteLogOutputDevice::Clear() {
    FGameRemoteLogEntry Dump;
    int32 DrainedInClear = 0;
    while (LogQueue.Dequeue(Dump)) {
        ++DrainedInClear;
    }
    if (DrainedInClear > 0) {
        QueuedCount.fetch_sub(DrainedInClear, std::memory_order_relaxed);
    }
    DroppedCount.store(0, std::memory_order_relaxed);
}
