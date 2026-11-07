/**
 * @file FSlotMapTest.cpp
 * @brief TGameHandle 与 TSlotMap 紧凑代际槽位容器单元测试
 */

#include "Types/TSlotMap.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

struct FTestEntityTag {};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameHandlePackingAndHashTest,
    "Game.Core.Types.SlotMap.HandlePackingAndHash",
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

    ValidHandle.Reset();
    TestFalse(TEXT("Reset 后句柄恢复无效态"), ValidHandle.IsValid());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSlotMapLifecycleAndGenerationSafetyTest,
    "Game.Core.Types.SlotMap.LifecycleAndGenerationSafety",
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

#endif
