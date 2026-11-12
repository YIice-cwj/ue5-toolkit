// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemoryPool/StructPool.h"

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
 * @brief 验证纯 C++ 原生结构体池的纳秒级分配、原地构造、析构回收与 RAII 租约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameStructPoolLifecycleTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FStructPoolTestItem::ConstructionCounter = 0;
    FStructPoolTestItem::DestructionCounter = 0;

    TStructPool<FStructPoolTestItem, 16> StructPool;
    TestEqual(TEXT("初始已分配数量为 0"), StructPool.GetAllocatedCount(), size_t(0));

    FStructPoolTestItem* ItemA = StructPool.Acquire(100, TEXT("ItemA"));
    TestNotNull(TEXT("借出结构体 A 成功"), ItemA);
    if (!ItemA) {
        return false;
    }
    TestEqual(TEXT("结构体 A 原地构造参数值正确"), ItemA->Value, 100);
    TestEqual(TEXT("结构体 A 字符串正确"), ItemA->Label, FString(TEXT("ItemA")));
    TestEqual(TEXT("构造计数为 1"), FStructPoolTestItem::ConstructionCounter, 1);
    TestEqual(TEXT("已分配数量为 1"), StructPool.GetAllocatedCount(), size_t(1));

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
    TestEqual(TEXT("已分配数量恢复为 2"), StructPool.GetAllocatedCount(), size_t(2));

    {
        TScopedStructRental<FStructPoolTestItem, 16> ScopedItem = StructPool.AcquireScoped(400, TEXT("Scoped"));
        TestTrue(TEXT("作用域结构体租约有效"), ScopedItem.IsValid());
        TestEqual(TEXT("作用域内已分配数量为 3"), StructPool.GetAllocatedCount(), size_t(3));
        TestEqual(TEXT("解引用访问值正确"), ScopedItem->Value, 400);
        TestEqual(TEXT("操作符访问正确"), (*ScopedItem).Value, 400);
    }
    TestEqual(TEXT("离开作用域后自动归还已分配数变为 2"), StructPool.GetAllocatedCount(), size_t(2));

    StructPool.Release(ItemB);
    StructPool.Release(ItemC);
    TestEqual(TEXT("全部归还后已分配数为 0"), StructPool.GetAllocatedCount(), size_t(0));

    StructPool.Clear();
    TestEqual(TEXT("清空后已开辟 Chunk 为 0"), StructPool.GetChunkCount(), size_t(0));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
