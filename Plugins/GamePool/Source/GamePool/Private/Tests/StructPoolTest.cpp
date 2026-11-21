/**
 * @file StructPoolTest.cpp
 * @brief 原生 C++ 结构体内存池 TStructPool 与 RAII 租约 TScopedStructRental 全契约自动化测试
 */

#include "Memory/TStructPool.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

struct FStructPoolTestItem {
    int32 Value = 0;
    FString Label;
    static int32 ConstructionCounter;
    static int32 DestructionCounter;

    FStructPoolTestItem(int32 InValue, FString InLabel)
        : Value(InValue)
        , Label(MoveTemp(InLabel)) {
        ++ConstructionCounter;
    }

    ~FStructPoolTestItem() {
        ++DestructionCounter;
    }
};

int32 FStructPoolTestItem::ConstructionCounter = 0;
int32 FStructPoolTestItem::DestructionCounter = 0;

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStructPoolLifecycleTest,
    "Game.Pool.StructPoolLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TStructPool 原地构造、内存复用、预热 ReserveChunks、多 Chunk 扩容及 TScopedStructRental 移动/提前归还/脱钩
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameStructPoolLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStructPoolTestItem::ConstructionCounter = 0;
    FStructPoolTestItem::DestructionCounter = 0;

    TStructPool<FStructPoolTestItem, 4> StructPool;
    TestTrue(TEXT("初始 IsEmpty 为 true"), StructPool.IsEmpty());
    TestEqual(TEXT("初始已分配数量为 0"), StructPool.GetAllocatedCount(), size_t(0));
    TestEqual(TEXT("初始空闲块数为 0"), StructPool.GetFreeCount(), size_t(0));

    StructPool.ReserveChunks(2);
    TestEqual(TEXT("预热后 Chunk 数量为 2"), StructPool.GetChunkCount(), size_t(2));
    TestEqual(TEXT("预热后总容量为 8"), StructPool.GetTotalCapacity(), size_t(8));
    TestEqual(TEXT("预热后空闲块数为 8"), StructPool.GetFreeCount(), size_t(8));
    TestTrue(TEXT("预热后未借出仍满足 IsEmpty"), StructPool.IsEmpty());

    FStructPoolTestItem* ItemA = StructPool.Acquire(100, TEXT("ItemA"));
    TestNotNull(TEXT("借出结构体 A 成功"), ItemA);
    if (!ItemA) {
        return false;
    }
    TestFalse(TEXT("借出后 IsEmpty 为 false"), StructPool.IsEmpty());
    TestEqual(TEXT("结构体 A 原地构造参数值正确"), ItemA->Value, 100);
    TestEqual(TEXT("结构体 A 字符串正确"), ItemA->Label, FString(TEXT("ItemA")));
    TestEqual(TEXT("构造计数为 1"), FStructPoolTestItem::ConstructionCounter, 1);
    TestEqual(TEXT("已分配数量为 1"), StructPool.GetAllocatedCount(), size_t(1));
    TestEqual(TEXT("空闲块数降为 7"), StructPool.GetFreeCount(), size_t(7));

    StructPool.Release(nullptr);
    TestEqual(TEXT("Release(nullptr) 安全无副作用"), StructPool.GetAllocatedCount(), size_t(1));

    FStructPoolTestItem* ItemB = StructPool.Acquire(200, TEXT("ItemB"));
    TestNotNull(TEXT("借出结构体 B 成功"), ItemB);
    TestEqual(TEXT("构造计数为 2"), FStructPoolTestItem::ConstructionCounter, 2);
    TestEqual(TEXT("已分配数量为 2"), StructPool.GetAllocatedCount(), size_t(2));

    StructPool.Release(ItemA);
    TestEqual(TEXT("归还 A 触发析构函数"), FStructPoolTestItem::DestructionCounter, 1);
    TestEqual(TEXT("归还 A 后已分配数量减为 1"), StructPool.GetAllocatedCount(), size_t(1));

    FStructPoolTestItem* ItemC = StructPool.Acquire(300, TEXT("ItemC"));
    TestNotNull(TEXT("借出结构体 C 复用内存成功"), ItemC);
    TestEqual(TEXT("复用内存地址一致"), static_cast<void*>(ItemC), static_cast<void*>(ItemA));
    TestEqual(TEXT("结构体 C 构造值正确"), ItemC->Value, 300);

    {
        TScopedStructRental<FStructPoolTestItem, 4> Rental1 = StructPool.AcquireScoped(400, TEXT("R1"));
        TScopedStructRental<FStructPoolTestItem, 4> Rental2 = StructPool.AcquireScoped(500, TEXT("R2"));
        TestTrue(TEXT("作用域租约 1 有效"), static_cast<bool>(Rental1));
        TestEqual(TEXT("解引用访问值正确"), Rental1->Value, 400);
        TestEqual(TEXT("操作符访问正确"), (*Rental2).Value, 500);
        TestEqual(TEXT("双租约下活跃数为 4"), StructPool.GetAllocatedCount(), size_t(4));

        Rental1 = MoveTemp(Rental2);
        TestFalse(TEXT("移动赋值后源租约 2 失活"), Rental2.IsValid());
        TestEqual(TEXT("移动赋值后租约 1 指向原 500"), Rental1->Value, 500);
        TestEqual(TEXT("移动赋值自动归还被覆盖旧租约，活跃数降为 3"), StructPool.GetAllocatedCount(), size_t(3));

        TScopedStructRental<FStructPoolTestItem, 4> MovedRental = MoveTemp(Rental1);
        TestFalse(TEXT("移动构造后源租约失活"), Rental1.IsValid());
        MovedRental.EarlyRelease();
        TestFalse(TEXT("EarlyRelease 后租约失活"), MovedRental.IsValid());
        TestEqual(TEXT("EarlyRelease 后活跃数降为 2"), StructPool.GetAllocatedCount(), size_t(2));

        TScopedStructRental<FStructPoolTestItem, 4> DetachRental = StructPool.AcquireScoped(600, TEXT("Detach"));
        FStructPoolTestItem* DetachedPtr = DetachRental.ReleaseAndDetach();
        TestFalse(TEXT("ReleaseAndDetach 后租约失活"), DetachRental.IsValid());
        TestNotNull(TEXT("脱钩返回有效裸指针"), DetachedPtr);
        TestEqual(TEXT("脱钩后对象仍处于活跃借出态"), StructPool.GetAllocatedCount(), size_t(3));
        StructPool.Release(DetachedPtr);
    }

    TArray<FStructPoolTestItem*> OverflowItems;
    for (int32 Index = 0; Index < 8; ++Index) {
        OverflowItems.Add(StructPool.Acquire(Index, TEXT("Grow")));
    }
    TestTrue(TEXT("超容量自动触发第 3 个 Chunk 扩容"), StructPool.GetChunkCount() >= size_t(3));
    for (FStructPoolTestItem* Item : OverflowItems) {
        StructPool.Release(Item);
    }

    StructPool.Release(ItemB);
    StructPool.Release(ItemC);
    TestTrue(TEXT("全部归还后 IsEmpty 恢复为 true"), StructPool.IsEmpty());
    TestEqual(TEXT("构造与析构次数严格平衡"), FStructPoolTestItem::ConstructionCounter, FStructPoolTestItem::DestructionCounter);

    TStructPool<FStructPoolTestItem, 4> MovedPool = MoveTemp(StructPool);
    TestEqual(TEXT("移动构造接管全部 Chunk 容量"), MovedPool.GetTotalCapacity(), size_t(12));
    TestEqual(TEXT("源池移走后 Chunk 归零"), StructPool.GetChunkCount(), size_t(0));

    MovedPool.Clear();
    TestEqual(TEXT("清空后已开辟 Chunk 为 0"), MovedPool.GetChunkCount(), size_t(0));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
