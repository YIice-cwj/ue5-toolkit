/**
 * @file TFixedBlockAllocatorTest.cpp
 * @brief TFixedBlockAllocator 与 TFixedSizePool 单元测试（侵入式空闲链表、跨 Chunk 扩容与对象生命周期）
 */

#include "Memory/TFixedBlockAllocator.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>
#include "Misc/AutomationTest.h"

namespace {

class FPoolTestObject {
public:
    static inline std::atomic<int32> ConstructorCalls{0};
    static inline std::atomic<int32> DestructorCalls{0};

    explicit FPoolTestObject(int32 InValue = 0)
        : Value(InValue) {
        ++ConstructorCalls;
    }

    ~FPoolTestObject() {
        ++DestructorCalls;
    }

    int32 GetValue() const {
        return Value;
    }

private:
    int32 Value = 0;
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFixedBlockAllocatorBasicAndReuseTest,
    "Game.Core.Memory.FixedBlockAllocator.BasicAndReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证固定块分配器的基本分配、空闲链表纳秒级 O(1) 复用与对齐
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFixedBlockAllocatorBasicAndReuseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TFixedBlockAllocator<32, 4, 16> Allocator;
    TestEqual(TEXT("初始分配计数为 0"), Allocator.GetAllocatedCount(), static_cast<size_t>(0));
    TestEqual(TEXT("初始 Chunk 计数为 0"), Allocator.GetChunkCount(), static_cast<size_t>(0));
    TestEqual(TEXT("初始容量为 0"), Allocator.GetTotalCapacity(), static_cast<size_t>(0));
    TestEqual(TEXT("对齐后尺寸正确"), Allocator.GetAlignedBlockSize(), static_cast<size_t>(32));

    void* Ptr1 = Allocator.Allocate();
    TestNotNull(TEXT("首次分配成功"), Ptr1);
    TestEqual(TEXT("首分配后分配数为 1"), Allocator.GetAllocatedCount(), static_cast<size_t>(1));
    TestEqual(TEXT("首分配开辟 1 个 Chunk"), Allocator.GetChunkCount(), static_cast<size_t>(1));
    TestEqual(TEXT("总容量等于 BlocksPerChunk"), Allocator.GetTotalCapacity(), static_cast<size_t>(4));
    TestEqual(TEXT("指针满足 16 字节对齐"), reinterpret_cast<uintptr_t>(Ptr1) % 16, static_cast<uintptr_t>(0));

    void* Ptr2 = Allocator.Allocate();
    TestNotNull(TEXT("二次分配成功"), Ptr2);
    TestTrue(TEXT("连续分配不同地址"), Ptr1 != Ptr2);
    TestEqual(TEXT("当前分配数为 2"), Allocator.GetAllocatedCount(), static_cast<size_t>(2));

    Allocator.Deallocate(Ptr1);
    TestEqual(TEXT("归还后分配数为 1"), Allocator.GetAllocatedCount(), static_cast<size_t>(1));

    void* PtrReused = Allocator.Allocate();
    TestEqual(TEXT("优先复用最近归还的块地址"), PtrReused, Ptr1);
    TestEqual(TEXT("复用后分配数恢复为 2"), Allocator.GetAllocatedCount(), static_cast<size_t>(2));

    Allocator.Deallocate(nullptr);
    TestEqual(TEXT("归还 nullptr 无副作用"), Allocator.GetAllocatedCount(), static_cast<size_t>(2));

    Allocator.Deallocate(Ptr2);
    Allocator.Deallocate(PtrReused);
    TestEqual(TEXT("全部归还后分配数为 0"), Allocator.GetAllocatedCount(), static_cast<size_t>(0));

    Allocator.Reset();
    TestEqual(TEXT("Reset 后 Chunk 计数归零"), Allocator.GetChunkCount(), static_cast<size_t>(0));
    TestEqual(TEXT("Reset 后总容量归零"), Allocator.GetTotalCapacity(), static_cast<size_t>(0));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFixedBlockAllocatorChunkExpansionAndMoveTest,
    "Game.Core.Memory.FixedBlockAllocator.ChunkExpansionAndMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多 Chunk 跨块扩容与移动构造/赋值的所有权转移
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFixedBlockAllocatorChunkExpansionAndMoveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr size_t BlocksPerChunk = 8;
    TFixedBlockAllocator<16, BlocksPerChunk> Allocator;

    TArray<void*> AllocatedBlocks;
    AllocatedBlocks.Reserve(20);

    for (int32 Index = 0; Index < 20; ++Index) {
        void* Block = Allocator.Allocate();
        TestNotNull(TEXT("连续分配有效"), Block);
        AllocatedBlocks.Add(Block);
    }

    TestEqual(TEXT("已分配 20 个块"), Allocator.GetAllocatedCount(), static_cast<size_t>(20));
    TestEqual(TEXT("分配 20 块需要 3 个 Chunk (8*3=24)"), Allocator.GetChunkCount(), static_cast<size_t>(3));
    TestEqual(TEXT("总容量为 24"), Allocator.GetTotalCapacity(), static_cast<size_t>(24));

    TFixedBlockAllocator<16, BlocksPerChunk> MovedAllocator(MoveTemp(Allocator));
    TestEqual(TEXT("移动构造后源分配数归零"), Allocator.GetAllocatedCount(), static_cast<size_t>(0));
    TestEqual(TEXT("移动构造后源 Chunk 归零"), Allocator.GetChunkCount(), static_cast<size_t>(0));
    TestEqual(TEXT("移动后目标接管 20 个分配"), MovedAllocator.GetAllocatedCount(), static_cast<size_t>(20));
    TestEqual(TEXT("移动后目标接管 3 个 Chunk"), MovedAllocator.GetChunkCount(), static_cast<size_t>(3));

    for (void* Block : AllocatedBlocks) {
        MovedAllocator.Deallocate(Block);
    }
    TestEqual(TEXT("通过移动目标归还所有块"), MovedAllocator.GetAllocatedCount(), static_cast<size_t>(0));

    TFixedBlockAllocator<16, BlocksPerChunk> AssignedAllocator;
    AssignedAllocator = MoveTemp(MovedAllocator);
    TestEqual(TEXT("移动赋值接管 Chunk"), AssignedAllocator.GetChunkCount(), static_cast<size_t>(3));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFixedSizePoolObjectLifecycleTest,
    "Game.Core.Memory.FixedBlockAllocator.PoolObjectLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TFixedSizePool 类型化对象池的原地构造、实参转发、析构计数与安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFixedSizePoolObjectLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FPoolTestObject::ConstructorCalls = 0;
    FPoolTestObject::DestructorCalls = 0;

    {
        TFixedSizePool<FPoolTestObject, 8> ObjectPool;

        FPoolTestObject* Obj1 = ObjectPool.New(101);
        TestNotNull(TEXT("对象池 New 成功"), Obj1);
        TestEqual(TEXT("构造函数调用 1 次"), FPoolTestObject::ConstructorCalls.load(), 1);
        TestEqual(TEXT("构造值正确写入"), Obj1->GetValue(), 101);
        TestEqual(TEXT("分配计数为 1"), ObjectPool.GetAllocatedCount(), static_cast<size_t>(1));

        FPoolTestObject* Obj2 = ObjectPool.New(202);
        TestNotNull(TEXT("对象 2 New 成功"), Obj2);
        TestEqual(TEXT("构造函数累计调用 2 次"), FPoolTestObject::ConstructorCalls.load(), 2);
        TestEqual(TEXT("对象 2 值正确"), Obj2->GetValue(), 202);

        ObjectPool.Free(Obj1);
        TestEqual(TEXT("Free 后析构函数执行 1 次"), FPoolTestObject::DestructorCalls.load(), 1);
        TestEqual(TEXT("Free 后活跃计数减 1"), ObjectPool.GetAllocatedCount(), static_cast<size_t>(1));

        FPoolTestObject* ObjReused = ObjectPool.New(303);
        TestEqual(TEXT("复用内存指针地址一致"), ObjReused, Obj1);
        TestEqual(TEXT("新构造值正确覆盖"), ObjReused->GetValue(), 303);
        TestEqual(TEXT("构造函数累计调用 3 次"), FPoolTestObject::ConstructorCalls.load(), 3);

        ObjectPool.Free(nullptr);
        TestEqual(TEXT("Free nullptr 无析构"), FPoolTestObject::DestructorCalls.load(), 1);

        ObjectPool.Free(Obj2);
        ObjectPool.Free(ObjReused);
        TestEqual(TEXT("全部手动 Free 后析构累计 3 次"), FPoolTestObject::DestructorCalls.load(), 3);
        TestEqual(TEXT("全部归还后活跃计数归零"), ObjectPool.GetAllocatedCount(), static_cast<size_t>(0));
    }

    TestEqual(TEXT("构造与析构次数严格平衡"), FPoolTestObject::ConstructorCalls.load(), FPoolTestObject::DestructorCalls.load());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
