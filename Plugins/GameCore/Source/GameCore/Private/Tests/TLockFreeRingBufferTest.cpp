/**
 * @file TLockFreeRingBufferTest.cpp
 * @brief TLockFreeRingBuffer 无锁环形缓冲区自动化单元测试
 */

#include "Types/TLockFreeRingBuffer.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/ParallelFor.h"
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

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferBasicTest,
    "Game.Core.Types.LockFreeRingBuffer.BasicFIFO",
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

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLockFreeRingBufferLifecycleTest,
    "Game.Core.Types.LockFreeRingBuffer.ObjectDestruction",
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

#endif
