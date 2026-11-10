/**
 * @file TLockFreeRingBufferTest.cpp
 * @brief TLockFreeRingBuffer 无锁环形缓冲区自动化单元测试
 */

#include "Containers/TLockFreeRingBuffer.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

namespace {

struct FDestructorTracker {
    static inline std::atomic<int32> InstanceCount{0};

    int32 Value = 0;

    FDestructorTracker() : Value(0) {
        InstanceCount.fetch_add(1, std::memory_order_relaxed);
    }

    explicit FDestructorTracker(int32 InValue) : Value(InValue) {
        InstanceCount.fetch_add(1, std::memory_order_relaxed);
    }

    FDestructorTracker(const FDestructorTracker& Other) : Value(Other.Value) {
        InstanceCount.fetch_add(1, std::memory_order_relaxed);
    }

    FDestructorTracker& operator=(const FDestructorTracker& Other) {
        Value = Other.Value;
        return *this;
    }

    FDestructorTracker(FDestructorTracker&& Other) noexcept : Value(Other.Value) {
        Other.Value = 0;
        InstanceCount.fetch_add(1, std::memory_order_relaxed);
    }

    FDestructorTracker& operator=(FDestructorTracker&& Other) noexcept {
        Value = Other.Value;
        Other.Value = 0;
        return *this;
    }

    ~FDestructorTracker() {
        InstanceCount.fetch_sub(1, std::memory_order_relaxed);
    }
};

struct FNonDefaultConstructible {
    FNonDefaultConstructible() = delete;
    explicit FNonDefaultConstructible(int32 InId) : Id(InId) {}
    int32 Id = 0;
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferBasicTest,
    "Game.Core.Containers.LockFreeRingBuffer.BasicFIFO",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无锁环形缓冲区的 FIFO 先入先出顺序、满载拦截与析构生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLockFreeRingBufferBasicTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TLockFreeRingBuffer<int32, 4> Buffer;
    TestEqual(TEXT("初始容量常量为 4"), Buffer.GetCapacity(), 4U);
    TestTrue(TEXT("初始队列为空"), Buffer.IsEmpty());
    TestEqual(TEXT("初始计数为 0"), Buffer.Count(), 0U);

    int32 OutVal = 0;
    TestFalse(TEXT("空队列 Dequeue 返回 false"), Buffer.Dequeue(OutVal));

    TestTrue(TEXT("入队 10 成功"), Buffer.Enqueue(10));
    TestTrue(TEXT("入队 20 成功"), Buffer.Enqueue(20));
    TestTrue(TEXT("入队 30 成功"), Buffer.Enqueue(30));
    TestTrue(TEXT("入队 40 成功"), Buffer.Enqueue(40));

    TestEqual(TEXT("满载时 Count 为 4"), Buffer.Count(), 4U);
    TestFalse(TEXT("队列满载入队被拒绝"), Buffer.Enqueue(50));

    TestTrue(TEXT("出队首个元素成功"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("出队值严格符合 FIFO 为 10"), OutVal, 10);

    TestTrue(TEXT("出队后腾出空间，入队 50 成功"), Buffer.Enqueue(50));

    TestTrue(TEXT("出队 20 成功"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("出队为 20"), OutVal, 20);
    TestTrue(TEXT("出队 30 成功"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("出队为 30"), OutVal, 30);
    TestTrue(TEXT("出队 40 成功"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("出队为 40"), OutVal, 40);
    TestTrue(TEXT("出队 50 成功"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("出队为 50"), OutVal, 50);

    TestTrue(TEXT("全部取空后 IsEmpty 为 true"), Buffer.IsEmpty());

    Buffer.Enqueue(100);
    Buffer.Enqueue(200);
    TestTrue(TEXT("Pop 成功丢弃队头 100"), Buffer.Pop());
    TestTrue(TEXT("Dequeue 取出剩余元素 200"), Buffer.Dequeue(OutVal));
    TestEqual(TEXT("值为 200"), OutVal, 200);
    TestFalse(TEXT("空队列 Pop 返回 false"), Buffer.Pop());

    TLockFreeRingBuffer<FNonDefaultConstructible, 4> NonDefaultBuffer;
    NonDefaultBuffer.Emplace(1001);
    NonDefaultBuffer.Emplace(1002);
    TestEqual(TEXT("非默认构造类型容器容量正常"), NonDefaultBuffer.Count(), 2U);
    NonDefaultBuffer.Clear();
    TestTrue(TEXT("非默认构造类型容器 Clear 成功"), NonDefaultBuffer.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferLifecycleTest,
    "Game.Core.Containers.LockFreeRingBuffer.ObjectDestruction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无锁环形缓冲区对具有非平凡构造与析构的对象的生命周期管理与零泄漏
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLockFreeRingBufferLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FDestructorTracker::InstanceCount.store(0);

    {
        TLockFreeRingBuffer<FDestructorTracker, 8> TrackerBuffer;
        TrackerBuffer.Emplace(100);
        TrackerBuffer.Emplace(200);
        TrackerBuffer.Emplace(300);

        TestEqual(TEXT("缓冲区内暂存 3 个对象实例"), FDestructorTracker::InstanceCount.load(), 3);

        FDestructorTracker Popped;
        TestTrue(TEXT("成功弹出一个对象"), TrackerBuffer.Dequeue(Popped));
        TestEqual(TEXT("弹出后依然有效存在 3 个（含本地临时变量）"), FDestructorTracker::InstanceCount.load(), 3);
    }

    TestEqual(TEXT("离开作用域后所有对象正确调用析构，实例计数严格归零"), FDestructorTracker::InstanceCount.load(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferConcurrentMPMCTest,
    "Game.Core.Containers.LockFreeRingBuffer.ConcurrentMPMC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 真实多线程并发：多生产者与多消费者无锁环形缓冲区高压力对撞与零数据丢失
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLockFreeRingBufferConcurrentMPMCTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 NumProducers = 4;
    constexpr int32 NumConsumers = 4;
    constexpr int32 ItemsPerProducer = 5000;
    constexpr int32 TotalExpectedItems = NumProducers * ItemsPerProducer;
    constexpr uint32 BufferCapacity = 1024;

    TLockFreeRingBuffer<int32, BufferCapacity> Buffer;
    std::atomic<bool> bProducersDone{false};
    std::atomic<int32> ActiveProducers{NumProducers};

    TArray<TArray<int32>> ConsumerResults;
    ConsumerResults.SetNum(NumConsumers);

    TArray<TFuture<void>> ConsumerFutures;
    ConsumerFutures.Reserve(NumConsumers);

    for (int32 ConsumerIdx = 0; ConsumerIdx < NumConsumers; ++ConsumerIdx) {
        ConsumerFutures.Add(Async(EAsyncExecution::Thread, [&Buffer, &bProducersDone, &ConsumerResults, ConsumerIdx]() {
            TArray<int32>& LocalList = ConsumerResults[ConsumerIdx];
            LocalList.Reserve(TotalExpectedItems / NumConsumers + 500);

            int32 Value = 0;
            while (!bProducersDone.load(std::memory_order_acquire) || !Buffer.IsEmpty()) {
                if (Buffer.Dequeue(Value)) {
                    LocalList.Add(Value);
                } else {
                    FPlatformProcess::Yield();
                }
            }
            while (Buffer.Dequeue(Value)) {
                LocalList.Add(Value);
            }
        }));
    }

    ParallelFor(NumProducers, [&Buffer, &ActiveProducers, &bProducersDone, ItemsPerProducer](int32 ProducerIdx) {
        const int32 BaseVal = ProducerIdx * ItemsPerProducer;
        for (int32 Step = 1; Step <= ItemsPerProducer; ++Step) {
            const int32 Val = BaseVal + Step;
            while (!Buffer.Enqueue(Val)) {
                FPlatformProcess::Yield();
            }
        }
        if (ActiveProducers.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            bProducersDone.store(true, std::memory_order_release);
        }
    });

    const uint32 ConcurrentSampleCount = Buffer.Count();
    TestTrue(TEXT("并发期间弱一致 Count 采样始终受限于 [0, Capacity]"), ConcurrentSampleCount <= BufferCapacity);

    for (TFuture<void>& Future : ConsumerFutures) {
        Future.Wait();
    }

    int32 TotalDequeued = 0;
    int64 SumDequeued = 0;
    TSet<int32> UniqueValues;
    UniqueValues.Reserve(TotalExpectedItems);

    for (int32 ConsumerIdx = 0; ConsumerIdx < NumConsumers; ++ConsumerIdx) {
        for (const int32 Val : ConsumerResults[ConsumerIdx]) {
            ++TotalDequeued;
            SumDequeued += Val;
            UniqueValues.Add(Val);
        }
    }

    int64 ExpectedSum = 0;
    for (int32 ProducerIdx = 0; ProducerIdx < NumProducers; ++ProducerIdx) {
        const int32 BaseVal = ProducerIdx * ItemsPerProducer;
        for (int32 Step = 1; Step <= ItemsPerProducer; ++Step) {
            ExpectedSum += BaseVal + Step;
        }
    }

    TestEqual(TEXT("多线程并发 MPMC 出队总数精确匹配"), TotalDequeued, TotalExpectedItems);
    TestEqual(TEXT("全部入队值零重复零丢失"), UniqueValues.Num(), TotalExpectedItems);
    TestEqual(TEXT("出队数值总和精确匹配无数据损坏"), SumDequeued, ExpectedSum);
    TestTrue(TEXT("最终缓冲区为空"), Buffer.IsEmpty());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferCountAndWrapTest,
    "Game.Core.Containers.LockFreeRingBuffer.CountAndWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证无锁环形缓冲区的 Count、IsEmpty 与连续入队出队准确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLockFreeRingBufferCountAndWrapTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TLockFreeRingBuffer<int32, 8> Buffer;
    TestTrue(TEXT("初始状态为空"), Buffer.IsEmpty());
    TestEqual(TEXT("初始计数为 0"), Buffer.Count(), 0U);

    for (int32 Cycle = 0; Cycle < 1000; ++Cycle) {
        for (int32 i = 0; i < 4; ++i) {
            TestTrue(TEXT("入队成功"), Buffer.Enqueue(i));
        }
        TestEqual(TEXT("入队 4 个元素后计数为 4"), Buffer.Count(), 4U);
        TestFalse(TEXT("队列非空"), Buffer.IsEmpty());

        for (int32 i = 0; i < 4; ++i) {
            int32 OutVal = 0;
            TestTrue(TEXT("出队成功"), Buffer.Dequeue(OutVal));
            TestEqual(TEXT("出队值正确"), OutVal, i);
        }
        TestEqual(TEXT("出队 4 个元素后计数为 0"), Buffer.Count(), 0U);
        TestTrue(TEXT("队列为空"), Buffer.IsEmpty());
    }

    return true;
}

#endif
