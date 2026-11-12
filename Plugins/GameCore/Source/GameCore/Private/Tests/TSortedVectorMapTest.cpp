/**
 * @file TSortedVectorMapTest.cpp
 * @brief 连续内存扁平排序字典单元测试
 */

#include "Containers/TSortedVectorMap.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSortedVectorMapBasicOpsTest,
    "Game.Core.Containers.SortedVectorMap.BasicOps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TSortedVectorMap 基础增删查改、默认插入与元素计数
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSortedVectorMapBasicOpsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSortedVectorMap<int32, FString, 4> Map;

    TestTrue(TEXT("初始为空"), Map.IsEmpty());
    TestEqual(TEXT("初始条目为 0"), Map.Num(), 0);
    TestNull(TEXT("查找不存在键返回空"), Map.Find(10));
    TestFalse(TEXT("Contains 判假"), Map.Contains(10));

    TestTrue(TEXT("新插入键 20 返回 true"), Map.Add(20, TEXT("Twenty")));
    TestTrue(TEXT("新插入键 10 返回 true"), Map.Add(10, TEXT("Ten")));
    TestTrue(TEXT("新插入键 30 返回 true"), Map.Add(30, TEXT("Thirty")));
    TestEqual(TEXT("已插入 3 项"), Map.Num(), 3);

    TestFalse(TEXT("更新已有键 20 返回 false"), Map.Add(20, TEXT("Twenty-Updated")));
    TestEqual(TEXT("更新不增加条目数"), Map.Num(), 3);

    const FString* Val20 = Map.Find(20);
    TestNotNull(TEXT("查找键 20 命中"), Val20);
    TestEqual(TEXT("键 20 数值已更新"), *Val20, TEXT("Twenty-Updated"));

    FString& Val40 = Map.FindOrAdd(40);
    TestEqual(TEXT("默认插入值为空字符串"), Val40, TEXT(""));
    Val40 = TEXT("Forty");
    TestEqual(TEXT("原地修改生效"), *Map.Find(40), TEXT("Forty"));

    TestTrue(TEXT("移除已有键 10 成功"), Map.Remove(10));
    TestFalse(TEXT("键 10 已不存在"), Map.Contains(10));
    TestFalse(TEXT("重复移除失败"), Map.Remove(10));
    TestEqual(TEXT("剩余条目为 3"), Map.Num(), 3);

    Map.Reset();
    TestTrue(TEXT("Reset 后字典为空"), Map.IsEmpty());
    TestEqual(TEXT("Reset 后条目为 0"), Map.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSortedVectorMapOrderedIterationTest,
    "Game.Core.Containers.SortedVectorMap.OrderedIteration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证乱序插入后的严格升序排序与双向迭代器行为
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSortedVectorMapOrderedIterationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSortedVectorMap<int32, int32, 16> Map;

    const TArray<int32> UnorderedKeys = { 50, 10, 80, 20, 90, 30, 70, 40, 60 };
    for (int32 K : UnorderedKeys) {
        Map.Add(K, K * 10);
    }

    TestEqual(TEXT("全部键插入完成"), Map.Num(), 9);

    int32 PreviousKey = -1;
    for (const auto& Entry : Map) {
        TestTrue(TEXT("严格单调升序遍历"), Entry.Key > PreviousKey);
        TestEqual(TEXT("对应 Value 正确"), Entry.Value, Entry.Key * 10);
        PreviousKey = Entry.Key;
    }

    int32 VisitedCount = 0;
    Map.ForEach([&VisitedCount](const int32& Key, const int32& Value) {
        ++VisitedCount;
        return Key < 30;
    });

    TestEqual(TEXT("ForEach 返回 false 提前截断遍历"), VisitedCount, 3);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSortedVectorMapSpillTest,
    "Game.Core.Containers.SortedVectorMap.Spill",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证超出内联栈容量后的动态扩容与数据完整性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSortedVectorMapSpillTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSortedVectorMap<int32, int32, 2> TinyMap;

    constexpr int32 InsertCount = 50;
    for (int32 Index = InsertCount - 1; Index >= 0; --Index) {
        TinyMap.Add(Index, Index * 2);
    }

    TestEqual(TEXT("50 项全部插入成功"), TinyMap.Num(), InsertCount);

    for (int32 Index = 0; Index < InsertCount; ++Index) {
        const int32* Found = TinyMap.Find(Index);
        TestNotNull(TEXT("查找成功"), Found);
        TestEqual(TEXT("数值准确"), *Found, Index * 2);
    }

    TinyMap.Empty();
    TestEqual(TEXT("Empty 后容量清空"), TinyMap.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSortedVectorMapRangeAndBatchTest,
    "Game.Core.Containers.SortedVectorMap.RangeAndBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证下标运算符、二分区间查询与批量合并追加
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSortedVectorMapRangeAndBatchTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSortedVectorMap<int32, FString, 8> Map;

    Map[10] = TEXT("Ten");
    Map[30] = TEXT("Thirty");
    Map[20] = TEXT("Twenty");

    TestEqual(TEXT("下标运算符自动插入 3 项"), Map.Num(), 3);
    TestEqual(TEXT("下标访问返回正确引用"), Map[20], TEXT("Twenty"));

    const auto& ConstMap = Map;
    TestEqual(TEXT("只读下标访问正确"), ConstMap[10], TEXT("Ten"));

    TestEqual(TEXT("LowerBoundIndex 查找 15 返回索引 1 (对应键 20)"), Map.LowerBoundIndex(15), 1);
    TestEqual(TEXT("LowerBoundIndex 查找 20 返回索引 1 (对应键 20)"), Map.LowerBoundIndex(20), 1);
    TestEqual(TEXT("UpperBoundIndex 查找 20 返回索引 2 (对应键 30)"), Map.UpperBoundIndex(20), 2);
    TestEqual(TEXT("UpperBoundIndex 查找 35 返回 3 (尾部越界位置)"), Map.UpperBoundIndex(35), 3);

    TArray<int32> RangeKeys;
    Map.ForRange(15, 30, [&RangeKeys](const int32& Key, const FString& Value) {
        RangeKeys.Add(Key);
    });

    TestEqual(TEXT("闭区间 [15, 30] 捕获 2 项"), RangeKeys.Num(), 2);
    TestEqual(TEXT("区间第 1 项为 20"), RangeKeys[0], 20);
    TestEqual(TEXT("区间第 2 项为 30"), RangeKeys[1], 30);

    using FEntry = TSortedVectorMap<int32, FString, 8>::FEntry;
    const FEntry BatchItems[] = {
        FEntry(20, TEXT("Twenty-Overwritten")),
        FEntry(40, TEXT("Forty")),
        FEntry(5, TEXT("Five"))
    };

    Map.AppendOrAssign(MakeArrayView(BatchItems, 3));

    TestEqual(TEXT("批量合并后总项数为 5"), Map.Num(), 5);
    TestEqual(TEXT("首元素为 5"), Map.GetEntries()[0].Key, 5);
    TestEqual(TEXT("键 20 的数值被批量项覆盖"), Map[20], TEXT("Twenty-Overwritten"));
    TestEqual(TEXT("尾元素为 40"), Map.GetEntries()[4].Key, 40);

    Map.ForRange(20, 40, [](const int32& Key, FString& Value) {
        Value += TEXT("!");
    });
    TestEqual(TEXT("非常量 ForRange 原地修改生效：20"), Map[20], TEXT("Twenty-Overwritten!"));
    TestEqual(TEXT("非常量 ForRange 原地修改生效：40"), Map[40], TEXT("Forty!"));

    const int32 NumBeforeSelfAppend = Map.Num();
    Map.AppendOrAssign(Map.GetEntries());
    TestEqual(TEXT("自别名 AppendOrAssign 幂等保持原项数"), Map.Num(), NumBeforeSelfAppend);
    TestEqual(TEXT("自别名 AppendOrAssign 不损坏内部数据"), Map[20], TEXT("Twenty-Overwritten!"));

    const FEntry SingleItem(99, TEXT("NinetyNine"));
    Map.AppendOrAssign(MakeArrayView(&SingleItem, 1));
    TestEqual(TEXT("单条目 AppendOrAssign 成功插入"), Map[99], TEXT("NinetyNine"));

    const FEntry EntryA(1, TEXT("Alpha"));
    const FEntry EntryB(1, TEXT("Alpha"));
    const FEntry EntryC(1, TEXT("Beta"));
    TestTrue(TEXT("键与值全等 Entry 相等"), EntryA == EntryB);
    TestFalse(TEXT("键同值异 Entry 不相等"), EntryA == EntryC);

    TSortedVectorMap<int32, FString, 8> MapClone = Map;
    TestTrue(TEXT("克隆字典相等性成立"), Map == MapClone);
    TestFalse(TEXT("克隆字典不相等性为假"), Map != MapClone);
    MapClone[99] = TEXT("Modified");
    TestFalse(TEXT("值修改后克隆字典不再相等"), Map == MapClone);
    TestTrue(TEXT("值修改后克隆字典不相等为真"), Map != MapClone);

    return true;
}

#endif
