/**
 * @file TLinearArenaTest.cpp
 * @brief FLinearArena 单元测试（对齐、指针稳定、非平凡析构链、作用域标记与并发独立性）
 */

#include "Memory/TLinearArena.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"

namespace {

struct alignas(64) FAligned64Type {
    uint8 Data[64];
};

struct FDestructorProbe {
    int32* CallCounter = nullptr;
    int32 Value = 0;

    FDestructorProbe(int32* InCounter, int32 InValue)
        : CallCounter(InCounter)
        , Value(InValue) {
    }

    ~FDestructorProbe() {
        if (CallCounter != nullptr) {
            ++(*CallCounter);
        }
    }
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLinearArenaBasicAndAlignmentTest,
    "Game.Core.Memory.LinearArena.BasicAndAlignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证线性分配器的原生分配、对齐保障、数组分配与容量状态查询
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLinearArenaBasicAndAlignmentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena Arena(4096);
    TestEqual(TEXT("初始分配字节数为 0"), Arena.GetTotalAllocatedBytes(), static_cast<size_t>(0));
    TestTrue(TEXT("初始容量大于等于 4096"), Arena.GetTotalCapacityBytes() >= 4096);

    TestNull(TEXT("申请 0 字节返回 nullptr"), Arena.Allocate(0));
    TestNull(TEXT("申请 0 元素数组返回 nullptr"), Arena.AllocateArray<int32>(0));

    void* Ptr1 = Arena.Allocate(17, 1);
    TestNotNull(TEXT("1 字节对齐分配有效"), Ptr1);

    void* Ptr4 = Arena.Allocate(12, 4);
    TestNotNull(TEXT("4 字节对齐分配有效"), Ptr4);
    TestEqual(TEXT("地址沿 4 字节对齐"), reinterpret_cast<uintptr_t>(Ptr4) % 4, static_cast<uintptr_t>(0));

    void* Ptr16 = Arena.Allocate(33, 16);
    TestNotNull(TEXT("16 字节对齐分配有效"), Ptr16);
    TestEqual(TEXT("地址沿 16 字节对齐"), reinterpret_cast<uintptr_t>(Ptr16) % 16, static_cast<uintptr_t>(0));

    FAligned64Type* AlignedObj = Arena.New<FAligned64Type>();
    TestNotNull(TEXT("64 字节强制对齐结构体构造有效"), AlignedObj);
    TestEqual(TEXT("地址沿 64 字节对齐"), reinterpret_cast<uintptr_t>(AlignedObj) % 64, static_cast<uintptr_t>(0));

    for (int32 Idx = 0; Idx < 10; ++Idx) {
        Arena.Allocate(1, 1);
        void* Ptr64 = Arena.Allocate(48, 64);
        TestNotNull(TEXT("64 字节对齐分配有效"), Ptr64);
        TestEqual(TEXT("奇数偏移后申请 64 字节对齐精确满足"), reinterpret_cast<uintptr_t>(Ptr64) % 64, static_cast<uintptr_t>(0));
    }

    int32* IntArray = Arena.AllocateArray<int32>(100);
    TestNotNull(TEXT("连续 100 个整型数组分配成功"), IntArray);
    for (int32 Index = 0; Index < 100; ++Index) {
        IntArray[Index] = Index * 2;
    }
    TestEqual(TEXT("首元素正确写入"), IntArray[0], 0);
    TestEqual(TEXT("尾元素正确写入"), IntArray[99], 198);

    TestTrue(TEXT("已分配大小随分配递增"), Arena.GetTotalAllocatedBytes() > 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLinearArenaScopeMarkerAndDestructorsTest,
    "Game.Core.Memory.LinearArena.ScopeMarkerAndDestructors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FScopeMarker RAII 游标回退与非平凡对象逆序析构链
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLinearArenaScopeMarkerAndDestructorsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena Arena(4096);
    int32 DestructCount = 0;

    FDestructorProbe* OuterObj = Arena.New<FDestructorProbe>(&DestructCount, 100);
    TestNotNull(TEXT("外层对象构造成功"), OuterObj);
    TestEqual(TEXT("外层对象未被析构"), DestructCount, 0);

    const size_t AllocatedBeforeScope = Arena.GetTotalAllocatedBytes();

    {
        FLinearArena::FScopeMarker ScopeMarker = Arena.CreateScopeMarker();

        FDestructorProbe* ScopedObj1 = Arena.New<FDestructorProbe>(&DestructCount, 201);
        FDestructorProbe* ScopedObj2 = Arena.New<FDestructorProbe>(&DestructCount, 202);
        TestNotNull(TEXT("作用域内对象 1 有效"), ScopedObj1);
        TestNotNull(TEXT("作用域内对象 2 有效"), ScopedObj2);
        TestEqual(TEXT("作用域内对象未被析构"), DestructCount, 0);
        TestTrue(TEXT("作用域内已分配大小增长"), Arena.GetTotalAllocatedBytes() > AllocatedBeforeScope);
    }

    TestEqual(TEXT("离开作用域后 2 个局部对象均已被逆序析构"), DestructCount, 2);
    TestEqual(TEXT("离开作用域后已分配字节数回退到标记点"), Arena.GetTotalAllocatedBytes(), AllocatedBeforeScope);

    Arena.Reset();
    TestEqual(TEXT("Reset 后所有对象（含外层）均已析构"), DestructCount, 3);
    TestEqual(TEXT("Reset 后已分配字节数归零"), Arena.GetTotalAllocatedBytes(), static_cast<size_t>(0));

    int32 ArrayDestructCount = 0;
    {
        FLinearArena::FScopeMarker ArrayScope = Arena.CreateScopeMarker();
        FDestructorProbe* ProbeArray = Arena.NewArray<FDestructorProbe>(4, &ArrayDestructCount, 7);
        TestNotNull(TEXT("NewArray 非平凡对象数组构造成功"), ProbeArray);
        TestEqual(TEXT("NewArray 首元素初始化正确"), ProbeArray[0].Value, 7);
        TestEqual(TEXT("NewArray 尾元素初始化正确"), ProbeArray[3].Value, 7);
    }
    TestEqual(TEXT("离开作用域后 NewArray 的 4 个元素全部逆序析构"), ArrayDestructCount, 4);

    int32 ResetWhileScopedDestructCount = 0;
    {
        Arena.Allocate(128, 8);
        FLinearArena::FScopeMarker StaleScope = Arena.CreateScopeMarker();
        Arena.New<FDestructorProbe>(&ResetWhileScopedDestructCount, 99);
        Arena.Reset();
        TestEqual(TEXT("Reset 立即析构存活对象并清空活跃标记栈"), ResetWhileScopedDestructCount, 1);
        TestEqual(TEXT("Reset 后活跃 ScopeMarker 数为 0"), Arena.GetActiveScopeCount(), 0);
    }
    TestEqual(TEXT("已失效的 StaleScope 离开作用域时不触发二次析构"), ResetWhileScopedDestructCount, 1);
    TestEqual(TEXT("已失效的 StaleScope 离开作用域时不将游标错误前推"), Arena.GetTotalAllocatedBytes(), static_cast<size_t>(0));

    FLinearArena MoveSource(2048);
    int32* NumPtr = MoveSource.AllocateArray<int32>(10);
    TestNotNull(TEXT("移动源分配数组有效"), NumPtr);
    NumPtr[0] = 12345;

    FLinearArena MoveTarget(MoveTemp(MoveSource));
    TestEqual(TEXT("移动构造后源分配字节归零"), MoveSource.GetTotalAllocatedBytes(), static_cast<size_t>(0));
    TestTrue(TEXT("移动构造后目标接管已分配字节"), MoveTarget.GetTotalAllocatedBytes() > 0);

    FLinearArena AssignTarget(1024);
    AssignTarget = MoveTemp(MoveTarget);
    TestEqual(TEXT("移动赋值后源分配字节归零"), MoveTarget.GetTotalAllocatedBytes(), static_cast<size_t>(0));
    TestTrue(TEXT("移动赋值后目标接管已分配字节"), AssignTarget.GetTotalAllocatedBytes() > 0);

    FLinearArena::FScopeMarker OuterMarker = AssignTarget.CreateScopeMarker();
    FLinearArena::FScopeMarker MovedMarker(MoveTemp(OuterMarker));
    TestEqual(TEXT("移动作用域标记后活跃深度保持为 1"), AssignTarget.GetActiveScopeCount(), 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLinearArenaMultiChunkAndConcurrencyTest,
    "Game.Core.Memory.LinearArena.MultiChunkAndConcurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证跨块扩容指针稳定化、Reset 复用与多线程独立实例并发安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FLinearArenaMultiChunkAndConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLinearArena Arena(1024);

    uint8* FirstAlloc = static_cast<uint8*>(Arena.Allocate(600, 8));
    TestNotNull(TEXT("首块内分配成功"), FirstAlloc);

    uint8* SecondAlloc = static_cast<uint8*>(Arena.Allocate(800, 8));
    TestNotNull(TEXT("超出首块触发新块扩展成功"), SecondAlloc);
    TestTrue(TEXT("容量已自动跨块扩展"), Arena.GetTotalCapacityBytes() >= 2048);

    FirstAlloc[0] = 0xAA;
    FirstAlloc[599] = 0xBB;
    SecondAlloc[0] = 0xCC;
    SecondAlloc[799] = 0xDD;

    TestEqual(TEXT("跨块扩展后首块内存内容完全稳定"), FirstAlloc[0], static_cast<uint8>(0xAA));
    TestEqual(TEXT("首块尾部内容稳定"), FirstAlloc[599], static_cast<uint8>(0xBB));
    TestEqual(TEXT("新块首部写入准确"), SecondAlloc[0], static_cast<uint8>(0xCC));
    TestEqual(TEXT("新块尾部写入准确"), SecondAlloc[799], static_cast<uint8>(0xDD));

    const size_t CapacityBeforeReset = Arena.GetTotalCapacityBytes();
    Arena.Reset();
    TestEqual(TEXT("Reset 游标归零"), Arena.GetTotalAllocatedBytes(), static_cast<size_t>(0));
    TestEqual(TEXT("Reset 保留已申请块总容量供零 Malloc 复用"), Arena.GetTotalCapacityBytes(), CapacityBeforeReset);

    uint8* ReusedAlloc = static_cast<uint8*>(Arena.Allocate(1200, 8));
    TestNotNull(TEXT("Reset 后大块连续分配无需额外 Malloc 顺利复用"), ReusedAlloc);

    constexpr int32 WorkerCount = 8;
    std::atomic<int32> ConcurrentSuccessCount{0};

    ParallelFor(WorkerCount, [&ConcurrentSuccessCount](int32 WorkerIdx) {
        FLinearArena LocalArena(2048);
        for (int32 Iteration = 0; Iteration < 100; ++Iteration) {
            int32* Numbers = LocalArena.AllocateArray<int32>(50);
            if (Numbers != nullptr) {
                Numbers[0] = WorkerIdx;
                Numbers[49] = Iteration;
            }
        }
        if (LocalArena.GetTotalAllocatedBytes() > 0) {
            ConcurrentSuccessCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    TestEqual(TEXT("8 个工作线程各持独立 Arena 高频并发分配全量成功"), ConcurrentSuccessCount.load(), WorkerCount);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
