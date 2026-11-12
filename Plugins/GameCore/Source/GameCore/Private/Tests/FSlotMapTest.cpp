/**
 * @file FSlotMapTest.cpp
 * @brief TGameHandle 与 TSlotMap 紧凑代际槽位容器单元测试
 */

#include "Containers/TSlotMap.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace {

struct FTestEntityTag {};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameHandlePackingAndHashTest,
    "Game.Core.Memory.SlotMap.HandlePackingAndHash",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TGameHandle 有效态判定、64 位打包解包与哈希字典键兼容性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameHandlePackingAndHashTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using FEntityHandle = TGameHandle<FTestEntityTag>;

    FEntityHandle EmptyHandle;
    TestFalse(TEXT("默认构造句柄无效"), EmptyHandle.IsValid());
    TestFalse(TEXT("默认构造句柄布尔转换为假"), static_cast<bool>(EmptyHandle));

    const FEntityHandle ZeroGenHandle(0u, 0u);
    TestFalse(TEXT("零代际号句柄无效"), ZeroGenHandle.IsValid());

    FEntityHandle ValidHandle(17u, 5u);
    TestTrue(TEXT("非零代际与有效索引句柄有效"), ValidHandle.IsValid());
    TestTrue(TEXT("有效句柄布尔转换为真"), static_cast<bool>(ValidHandle));
    TestEqual(TEXT("索引读取一致"), ValidHandle.GetIndex(), 17u);
    TestEqual(TEXT("代际读取一致"), ValidHandle.GetGeneration(), 5u);

    const uint64 Packed = ValidHandle.PackUInt64();
    const FEntityHandle Restored = FEntityHandle::FromPackedUInt64(Packed);
    TestTrue(TEXT("打包与解包往返相等"), Restored == ValidHandle);

    TMap<FEntityHandle, FString> HandleMap;
    HandleMap.Add(ValidHandle, TEXT("HeroUnit"));
    const FString* FoundName = HandleMap.Find(Restored);
    TestNotNull(TEXT("句柄可作为 TMap 哈希键精确命中"), FoundName);
    if (FoundName != nullptr) {
        TestEqual(TEXT("TMap 映射值一致"), *FoundName, FString(TEXT("HeroUnit")));
    }

    const FEntityHandle HandleA(1u, 1u);
    const FEntityHandle HandleB(2u, 1u);
    TestNotEqual(TEXT("连续自增槽位句柄雪崩哈希绝不相同"), GetTypeHash(HandleA), GetTypeHash(HandleB));

    ValidHandle.Reset();
    TestFalse(TEXT("Reset 后句柄恢复无效态"), ValidHandle.IsValid());

    TArray<uint8> Buffer;
    FMemoryWriter Writer(Buffer);
    FEntityHandle SaveHandle(42u, 7u);
    Writer << SaveHandle;
    TestTrue(TEXT("归档写入成功且包含数据"), Buffer.Num() > 0);

    FMemoryReader Reader(Buffer);
    FEntityHandle LoadedHandle;
    Reader << LoadedHandle;
    TestTrue(TEXT("归档读取还原句柄与原始句柄相等"), LoadedHandle == SaveHandle);
    TestEqual(TEXT("还原句柄索引一致"), LoadedHandle.GetIndex(), 42u);
    TestEqual(TEXT("还原句柄代际一致"), LoadedHandle.GetGeneration(), 7u);

    const FEntityHandle SmallHandle(10u, 2u);
    const FEntityHandle LargeIndexHandle(11u, 2u);
    const FEntityHandle LargeGenHandle(10u, 3u);
    TestTrue(TEXT("句柄 operator< 索引递增比较正确"), SmallHandle < LargeIndexHandle);
    TestTrue(TEXT("句柄 operator< 索引相同时代际递增比较正确"), SmallHandle < LargeGenHandle);
    TestFalse(TEXT("句柄自身不小于自身"), SmallHandle < SmallHandle);

    TArray<FEntityHandle> SortList = { LargeGenHandle, SmallHandle, LargeIndexHandle };
    SortList.Sort();
    TestEqual(TEXT("Sort 排序后首元素为 SmallHandle"), SortList[0], SmallHandle);
    TestEqual(TEXT("Sort 排序后中间元素为 LargeIndexHandle"), SortList[1], LargeIndexHandle);
    TestEqual(TEXT("Sort 排序后末元素为 LargeGenHandle"), SortList[2], LargeGenHandle);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSlotMapLifecycleAndGenerationSafetyTest,
    "Game.Core.Memory.SlotMap.LifecycleAndGenerationSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TSlotMap 增删改查、尾部交换连续性、槽位代际复用隔离及 Reset 全量失效语义
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSlotMapLifecycleAndGenerationSafetyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using FEntitySlotMap = TSlotMap<FString, FTestEntityTag>;
    using FEntityHandle = FEntitySlotMap::HandleType;

    FEntitySlotMap Map;
    Map.Reserve(0);
    Map.Reserve(8);
    TestTrue(TEXT("初始为空"), Map.IsEmpty());
    TestEqual(TEXT("初始数量为 0"), Map.Num(), 0);

    const FEntityHandle H0 = Map.Insert(TEXT("Alpha"));
    const FEntityHandle H1 = Map.Insert(TEXT("Bravo"));
    const FEntityHandle H2 = Map.Emplace(TEXT("Charlie"));
    TestEqual(TEXT("插入 3 项后数量为 3"), Map.Num(), 3);
    TestFalse(TEXT("非空判定正确"), Map.IsEmpty());

    const FEntitySlotMap& ConstMap = Map;
    TestNotNull(TEXT("只读 Find 命中 H0"), ConstMap.Find(H0));
    TestEqual(TEXT("H0 内容为 Alpha"), *ConstMap.Find(H0), FString(TEXT("Alpha")));
    TestEqual(TEXT("H1 内容为 Bravo"), *ConstMap.Find(H1), FString(TEXT("Bravo")));
    TestEqual(TEXT("H2 内容为 Charlie"), *ConstMap.Find(H2), FString(TEXT("Charlie")));

    TestTrue(TEXT("移除首元素触发尾部交换弹出成功"), Map.Remove(H0));
    TestEqual(TEXT("移除后数量减为 2"), Map.Num(), 2);
    TestFalse(TEXT("已移除句柄 H0 变为无效"), Map.IsValidHandle(H0));
    TestNull(TEXT("已移除句柄 H0 查找返回空"), Map.Find(H0));
    TestNull(TEXT("只读查找已移除句柄 H0 返回空"), ConstMap.Find(H0));
    TestFalse(TEXT("重复移除过期句柄 H0 返回 false"), Map.Remove(H0));

    TestNotNull(TEXT("被交换移动的尾部句柄 H2 依然有效"), Map.Find(H2));
    TestEqual(TEXT("尾部交换后 H2 数据无损"), *Map.Find(H2), FString(TEXT("Charlie")));
    TestEqual(TEXT("H1 数据无损"), *Map.Find(H1), FString(TEXT("Bravo")));

    const FEntityHandle H3 = Map.Insert(TEXT("Delta"));
    TestEqual(TEXT("复用空闲槽位索引 0"), H3.GetIndex(), H0.GetIndex());
    TestTrue(TEXT("复用槽位代际号已递增"), H3.GetGeneration() > H0.GetGeneration());
    TestNull(TEXT("旧句柄 H0 在槽位复用后仍被严格拦截"), Map.Find(H0));
    TestEqual(TEXT("新句柄 H3 指向新数据 Delta"), *Map.Find(H3), FString(TEXT("Delta")));

    if (FString* MutableDelta = Map.Find(H3)) {
        *MutableDelta = TEXT("DeltaUpdated");
    }
    TestEqual(TEXT("可变 Find 修改生效"), *ConstMap.Find(H3), FString(TEXT("DeltaUpdated")));

    for (FString& Val : Map.GetMutableDenseValues()) {
        Val += TEXT("!");
    }
    TestEqual(TEXT("连续密集视图遍历修改生效"), Map.GetDenseValues().Num(), 3);
    TestEqual(TEXT("H1 追加感叹号成功"), *ConstMap.Find(H1), FString(TEXT("Bravo!")));

    TestEqual(TEXT("operator[] 句柄下标访问正确"), Map[H1], FString(TEXT("Bravo!")));
    Map[H1] = TEXT("BravoUpdated");
    TestEqual(TEXT("operator[] 原地修改生效"), Map[H1], FString(TEXT("BravoUpdated")));
    TestEqual(TEXT("const operator[] 读取生效"), ConstMap[H1], FString(TEXT("BravoUpdated")));

    int32 RangeForCount = 0;
    for (const FString& Item : Map) {
        ++RangeForCount;
    }
    TestEqual(TEXT("Range-based for 遍历总数正确"), RangeForCount, 3);

    const FEntityHandle OutOfRangeHandle(999u, 1u);
    TestFalse(TEXT("越界索引句柄无效"), Map.IsValidHandle(OutOfRangeHandle));
    TestFalse(TEXT("越界索引句柄移除失败"), Map.Remove(OutOfRangeHandle));
    TestFalse(TEXT("空句柄移除失败"), Map.Remove(FEntityHandle{}));

    TestTrue(TEXT("移除当前密集数组末尾元素成功"), Map.Remove(H3));
    TestEqual(TEXT("末尾移除后数量为 2"), Map.Num(), 2);

    Map.Reset();
    TestTrue(TEXT("Reset 后为空"), Map.IsEmpty());
    TestFalse(TEXT("Reset 前句柄 H1 彻底失效"), Map.IsValidHandle(H1));
    TestFalse(TEXT("Reset 前句柄 H2 彻底失效"), Map.IsValidHandle(H2));

    const FEntityHandle PostResetHandle = Map.Insert(TEXT("Omega"));
    TestFalse(TEXT("Reset 后重新插入也不会令旧句柄 H1 复活"), Map.IsValidHandle(H1));
    TestFalse(TEXT("Reset 后重新插入也不会令旧句柄 H2 复活"), Map.IsValidHandle(H2));
    TestEqual(TEXT("Reset 后新句柄正常读取"), *Map.Find(PostResetHandle), FString(TEXT("Omega")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSlotMapIterationAndHandleLookupTest,
    "Game.Core.Memory.SlotMap.IterationAndHandleLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TSlotMap 的密集索引反查 Handle 与 ForEach 遍历闭包
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSlotMapIterationAndHandleLookupTest::RunTest([[maybe_unused]] const FString& Parameters) {
    using FEntitySlotMap = TSlotMap<int32, FTestEntityTag>;
    using FEntityHandle = FEntitySlotMap::HandleType;

    FEntitySlotMap Map;
    const FEntityHandle H0 = Map.Insert(10);
    const FEntityHandle H1 = Map.Insert(20);
    const FEntityHandle H2 = Map.Insert(30);

    TestEqual(TEXT("反查 0 号密集项句柄一致"), Map.GetHandleForDenseIndex(0), H0);
    TestEqual(TEXT("反查 1 号密集项句柄一致"), Map.GetHandleForDenseIndex(1), H1);
    TestEqual(TEXT("反查 2 号密集项句柄一致"), Map.GetHandleForDenseIndex(2), H2);
    TestFalse(TEXT("反查越界密集项返回无效句柄"), Map.GetHandleForDenseIndex(99).IsValid());
    TestFalse(TEXT("反查负数密集项返回无效句柄"), Map.GetHandleForDenseIndex(-1).IsValid());

    int32 Sum = 0;
    int32 IterCount = 0;
    Map.ForEach([this, &Sum, &IterCount](FEntityHandle Handle, int32& Val) {
        TestTrue(TEXT("遍历中的句柄有效"), Handle.IsValid());
        Sum += Val;
        Val += 1;
        ++IterCount;
    });

    TestEqual(TEXT("ForEach 遍历项数正确"), IterCount, 3);
    TestEqual(TEXT("初始求和为 60"), Sum, 60);
    TestEqual(TEXT("ForEach 原地修改生效：H0 变为 11"), *Map.Find(H0), 11);

    int32 EarlyStopCount = 0;
    const FEntitySlotMap& ConstMap = Map;
    ConstMap.ForEach([&EarlyStopCount](FEntityHandle, const int32&) -> bool {
        ++EarlyStopCount;
        return false;
    });
    TestEqual(TEXT("返回 false 成功提前中止遍历"), EarlyStopCount, 1);

    TSlotMap<TUniquePtr<int32>, FTestEntityTag> MoveOnlyMap;
    const auto MoveHandle = MoveOnlyMap.Insert(MakeUnique<int32>(999));
    TestTrue(TEXT("仅移动类型插入后句柄有效"), MoveHandle.IsValid());
    TUniquePtr<int32>* FoundMoveOnly = MoveOnlyMap.Find(MoveHandle);
    TestNotNull(TEXT("查找仅移动类型元素非空"), FoundMoveOnly);
    if (FoundMoveOnly != nullptr) {
        TestEqual(TEXT("仅移动类型数值匹配"), **FoundMoveOnly, 999);
    }
    TestTrue(TEXT("仅移动类型移除成功"), MoveOnlyMap.Remove(MoveHandle));
    TestFalse(TEXT("仅移动类型移除后句柄失效"), MoveOnlyMap.IsValidHandle(MoveHandle));

    return true;
}

#endif
