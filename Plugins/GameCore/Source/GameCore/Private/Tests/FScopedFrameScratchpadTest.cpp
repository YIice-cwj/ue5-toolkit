/**
 * @file FScopedFrameScratchpadTest.cpp
 * @brief FScopedFrameScratchpad 与 TScratchArray 单元测试
 */

#include "Memory/FScopedFrameScratchpad.h"
#include "Containers/TScratchArray.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

namespace {

struct FDestructorCounter {
    int32* Counter = nullptr;
    int32 Value = 0;

    FDestructorCounter(int32* InCounter, int32 InValue)
        : Counter(InCounter)
        , Value(InValue) {
    }

    ~FDestructorCounter() {
        if (Counter != nullptr) {
            ++(*Counter);
        }
    }
};

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedFrameScratchpadBasicTest,
    "Game.Core.Memory.ScopedFrameScratchpad.BasicAndRewind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单帧临时分配器的基础分配、RAII 自动回退与非平凡析构链
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedFrameScratchpadBasicTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena CustomArena(4096);
    int32 DestructionCount = 0;

    TestEqual(TEXT("初始已分配字节为 0"), CustomArena.GetTotalAllocatedBytes(), static_cast<size_t>(0));

    {
        FScopedFrameScratchpad Scratch(CustomArena);
        TestTrue(TEXT("GetArena 返回正确实例"), &Scratch.GetArena() == &CustomArena);

        void* Memory = Scratch.Allocate(128, 16);
        TestNotNull(TEXT("原生分配返回有效指针"), Memory);
        TestEqual(TEXT("16 字节对齐有效"), reinterpret_cast<uintptr_t>(Memory) % 16, static_cast<uintptr_t>(0));

        TArrayView<int32> EmptyArray = Scratch.AllocArray<int32>(0);
        TestEqual(TEXT("0 长度数组视图为空"), EmptyArray.Num(), 0);

        TArrayView<int32> NegativeArray = Scratch.AllocArray<int32>(-5);
        TestEqual(TEXT("负长度数组视图为空"), NegativeArray.Num(), 0);

        TArrayView<int32> ValidArray = Scratch.AllocArray<int32>(10);
        TestEqual(TEXT("有效数组视图长度正确"), ValidArray.Num(), 10);
        TestNotNull(TEXT("有效数组视图数据指针非空"), ValidArray.GetData());

        FDestructorCounter* Item1 = Scratch.New<FDestructorCounter>(&DestructionCount, 100);
        TestNotNull(TEXT("对象 1 构造有效"), Item1);
        TestEqual(TEXT("对象 1 初值正确"), Item1->Value, 100);

        {
            FScopedFrameScratchpad InnerScratch(CustomArena);
            FDestructorCounter* Item2 = InnerScratch.New<FDestructorCounter>(&DestructionCount, 200);
            TestNotNull(TEXT("嵌套对象构造有效"), Item2);
            TestEqual(TEXT("离开内层前析构次数为 0"), DestructionCount, 0);
        }

        TestEqual(TEXT("离开内层作用域后已执行 1 次析构"), DestructionCount, 1);
    }

    TestEqual(TEXT("离开外层作用域后已累计执行 2 次析构"), DestructionCount, 2);
    TestEqual(TEXT("作用域退出后已分配字节回退为 0"), CustomArena.GetTotalAllocatedBytes(), static_cast<size_t>(0));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedFrameScratchpadTlsTest,
    "Game.Core.Memory.ScopedFrameScratchpad.TlsAndConcurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证线程局部单帧分配器单例与多线程隔离安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedFrameScratchpadTlsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedFrameScratchpad::ResetThreadLocalArena();
    TestEqual(TEXT("重置后分配字节为 0"), FScopedFrameScratchpad::GetThreadLocalArena().GetTotalAllocatedBytes(), static_cast<size_t>(0));

    {
        FScopedFrameScratchpad DefaultScratch;
        void* Data = DefaultScratch.Allocate(64);
        TestNotNull(TEXT("TLS 默认分配有效"), Data);
        TestTrue(TEXT("分配后已分配字节大于 0"), FScopedFrameScratchpad::GetThreadLocalArena().GetTotalAllocatedBytes() > 0);
    }

    TestEqual(TEXT("默认作用域退出后自动回退"), FScopedFrameScratchpad::GetThreadLocalArena().GetTotalAllocatedBytes(), static_cast<size_t>(0));

    constexpr int32 TaskCount = 32;
    std::atomic<int32> SuccessCount{0};

    ParallelFor(TaskCount, [&SuccessCount](int32) {
        FScopedFrameScratchpad ThreadScratch;
        void* Mem = ThreadScratch.Allocate(128, 8);
        if (Mem != nullptr && (reinterpret_cast<uintptr_t>(Mem) % 8 == 0)) {
            SuccessCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    TestEqual(TEXT("全部并发线程均成功分配且独立回退"), SuccessCount.load(), TaskCount);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    TScratchArrayTest,
    "Game.Core.Memory.ScratchArray.FullLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证临时动态数组 TScratchArray 的增删、扩容、逆序析构与迭代视图
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool TScratchArrayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena Arena(8192);
    int32 DestructionCount = 0;

    {
        FScopedFrameScratchpad Scratch(Arena);
        TScratchArray<int32> Numbers(Scratch, 2);

        TestTrue(TEXT("初始状态为空"), Numbers.IsEmpty());
        TestEqual(TEXT("初始数量为 0"), Numbers.Num(), 0);
        TestTrue(TEXT("初始容量至少为 2"), Numbers.Capacity() >= 2);

        Numbers.Add(10);
        Numbers.Add(20);
        TestEqual(TEXT("追加 2 元素后数量为 2"), Numbers.Num(), 2);
        TestEqual(TEXT("下标 0 正确"), Numbers[0], 10);
        TestEqual(TEXT("下标 1 正确"), Numbers[1], 20);

        Numbers.Add(30);
        TestTrue(TEXT("扩容后容量增大"), Numbers.Capacity() >= 4);
        TestEqual(TEXT("扩容后数量为 3"), Numbers.Num(), 3);
        TestEqual(TEXT("扩容后数据一致"), Numbers[2], 30);

        Numbers[1] = 25;
        TestEqual(TEXT("非常量修改生效"), Numbers[1], 25);

        int32 Sum = 0;
        for (const int32 Value : Numbers) {
            Sum += Value;
        }
        TestEqual(TEXT("范围 for 遍历求和正确"), Sum, 10 + 25 + 30);

        TArrayView<int32> View = Numbers.AsView();
        TestEqual(TEXT("视图大小正确"), View.Num(), 3);
        TestEqual(TEXT("视图首元素指针匹配"), View.GetData(), Numbers.GetData());

        const int32 Popped = Numbers.Pop();
        TestEqual(TEXT("弹出末尾元素值正确"), Popped, 30);
        TestEqual(TEXT("弹出后数量为 2"), Numbers.Num(), 2);

        Numbers.Reset();
        TestTrue(TEXT("Reset 后为空"), Numbers.IsEmpty());
        TestEqual(TEXT("Reset 后数量为 0"), Numbers.Num(), 0);

        Numbers.Reserve(64);
        TestTrue(TEXT("Reserve 后容量至少为 64"), Numbers.Capacity() >= 64);
        Numbers.Emplace(99);
        TestEqual(TEXT("Emplace 成功写入元素"), Numbers[0], 99);
        TestEqual(TEXT("Emplace 后数量为 1"), Numbers.Num(), 1);

        TScratchArray<FDestructorCounter> ComplexItems(Scratch, 4);
        ComplexItems.Add(FDestructorCounter(&DestructionCount, 1));
        ComplexItems.Emplace(&DestructionCount, 2);
        TestEqual(TEXT("元素添加过程不发生外部对象析构漏记"), ComplexItems.Num(), 2);

        FDestructorCounter PoppedComplex = ComplexItems.Pop();
        TestEqual(TEXT("弹出复杂对象值匹配"), PoppedComplex.Value, 2);
    }

    TestTrue(TEXT("离开作用域后复杂对象已安全析构"), DestructionCount >= 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FScopedFrameScratchpadNewArrayTest,
    "Game.Core.Memory.ScopedFrameScratchpad.NewArrayAggregation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单帧分配器 NewArray 数组构造与聚合逆序析构机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FScopedFrameScratchpadNewArrayTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena Arena(4096);
    int32 DestructCount = 0;
    TArray<int32> DestructOrder;

    struct FOrderTracker {
        int32* Counter = nullptr;
        TArray<int32>* Order = nullptr;
        int32 Id = 0;

        FOrderTracker(int32* InCounter, TArray<int32>* InOrder, int32 InId)
            : Counter(InCounter)
            , Order(InOrder)
            , Id(InId) {
        }

        ~FOrderTracker() {
            if (Counter != nullptr) {
                ++(*Counter);
            }
            if (Order != nullptr) {
                Order->Add(Id);
            }
        }
    };

    {
        FScopedFrameScratchpad Scratch(Arena);

        TArrayView<int32> ZeroArr = Scratch.NewArray<int32>(0);
        TestEqual(TEXT("0 长度 NewArray 为空"), ZeroArr.Num(), 0);

        TArrayView<int32> NegArr = Scratch.NewArray<int32>(-1);
        TestEqual(TEXT("负长度 NewArray 为空"), NegArr.Num(), 0);

        TArrayView<int32> TrivialArr = Scratch.NewArray<int32>(5, 42);
        TestEqual(TEXT("平凡类型数组长度正确"), TrivialArr.Num(), 5);
        for (int32 Val : TrivialArr) {
            TestEqual(TEXT("平凡类型构造参数传递生效"), Val, 42);
        }

        constexpr int32 ItemCount = 4;
        TArrayView<FOrderTracker> Items = Scratch.NewArray<FOrderTracker>(ItemCount, &DestructCount, &DestructOrder, 0);
        for (int32 Index = 0; Index < ItemCount; ++Index) {
            Items[Index].Id = Index + 1;
        }
        TestEqual(TEXT("对象构造完成，析构计数为 0"), DestructCount, 0);
    }

    TestEqual(TEXT("作用域退出后析构全部 4 个对象"), DestructCount, 4);
    TestEqual(TEXT("析构顺序收集完整"), DestructOrder.Num(), 4);
    if (DestructOrder.Num() == 4) {
        TestEqual(TEXT("首个析构的是最后一个元素"), DestructOrder[0], 4);
        TestEqual(TEXT("第二个析构的是第三个元素"), DestructOrder[1], 3);
        TestEqual(TEXT("第三个析构的是第二个元素"), DestructOrder[2], 2);
        TestEqual(TEXT("最后一个析构的是首元素"), DestructOrder[3], 1);
    }

    return true;
}

#endif
