/**
 * @file FFastTagBitmaskTest.cpp
 * @brief FFastTagBitmask 与 FFastTagDictionary 单元测试
 */

#include "Containers/FFastTagBitmask.h"

#if WITH_AUTOMATION_TESTS

#include <atomic>

#include "Async/ParallelFor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFastTagBitmaskBasicOpsTest,
    "Game.Core.Containers.FastTagBitmask.BasicOps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 64 位极速状态位掩码的基础增删查改、互斥与子集运算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFastTagBitmaskBasicOpsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FFastTagBitmask Mask;
    TestTrue(TEXT("初始掩码为空"), Mask.IsEmpty());
    TestEqual(TEXT("初始激活 bit 为 0"), Mask.CountBits(), 0);

    Mask.AddTag(0);
    Mask.AddTag(63);
    TestFalse(TEXT("添加后非空"), Mask.IsEmpty());
    TestEqual(TEXT("激活 2 个 bit"), Mask.CountBits(), 2);
    TestTrue(TEXT("包含 bit 0"), Mask.HasTag(0));
    TestTrue(TEXT("包含 bit 63"), Mask.HasTag(63));
    TestFalse(TEXT("不包含 bit 1"), Mask.HasTag(1));

    Mask.AddTag(-1);
    Mask.AddTag(64);
    Mask.RemoveTag(-1);
    Mask.RemoveTag(64);
    Mask.ToggleTag(-1);
    Mask.ToggleTag(64);
    TestEqual(TEXT("越界索引不造成状态损坏"), Mask.CountBits(), 2);
    TestFalse(TEXT("负索引 HasTag 返回 false"), Mask.HasTag(-1));
    TestFalse(TEXT("超界索引 HasTag 返回 false"), Mask.HasTag(64));

    Mask.ToggleTag(0);
    TestFalse(TEXT("翻转 bit 0 后关闭"), Mask.HasTag(0));
    Mask.ToggleTag(0);
    TestTrue(TEXT("再次翻转 bit 0 后激活"), Mask.HasTag(0));

    Mask.RemoveTag(0);
    TestFalse(TEXT("移除 bit 0 后关闭"), Mask.HasTag(0));
    TestEqual(TEXT("仅剩 1 个 bit"), Mask.CountBits(), 1);

    FFastTagBitmask MaskA(0b0101ULL);
    FFastTagBitmask MaskB(0b0110ULL);
    FFastTagBitmask MaskC(0b1000ULL);

    TestTrue(TEXT("A 与 B 存在交集"), MaskA.HasAny(MaskB));
    TestFalse(TEXT("A 与 C 无交集"), MaskA.HasAny(MaskC));
    TestTrue(TEXT("A 与 C 完全互斥"), MaskA.HasNone(MaskC));
    TestFalse(TEXT("A 与 B 不互斥"), MaskA.HasNone(MaskB));

    FFastTagBitmask Subset(0b0001ULL);
    TestTrue(TEXT("A 包含 Subset 全部 bit"), MaskA.HasAll(Subset));
    TestFalse(TEXT("B 不包含 Subset 全部 bit"), MaskB.HasAll(Subset));

    FFastTagBitmask Combined = MaskA | MaskB;
    TestEqual(TEXT("位或合并结果正确"), Combined.Value, 0b0111ULL);

    FFastTagBitmask Intersected = MaskA & MaskB;
    TestEqual(TEXT("位与交集结果正确"), Intersected.Value, 0b0100ULL);

    FFastTagBitmask AssignOrMask(0b0101ULL);
    AssignOrMask |= FFastTagBitmask(0b0010ULL);
    TestEqual(TEXT("复合位或赋值生效"), AssignOrMask.Value, 0b0111ULL);

    AssignOrMask &= FFastTagBitmask(0b0011ULL);
    TestEqual(TEXT("复合位与赋值生效"), AssignOrMask.Value, 0b0011ULL);

    FFastTagBitmask Inverted = ~FFastTagBitmask(0ULL);
    TestEqual(TEXT("全位翻转计数值为 64"), Inverted.CountBits(), 64);

    MaskA |= MaskB;
    TestEqual(TEXT("掩码或运算生效"), MaskA.Value, 0b0111ULL);

    MaskA -= MaskB;
    TestEqual(TEXT("掩码复合差集赋值生效"), MaskA.Value, 0b0001ULL);

    const FFastTagBitmask Diff = FFastTagBitmask(0b0111ULL) - MaskB;
    TestEqual(TEXT("掩码差集运算符生效"), Diff.Value, 0b0001ULL);

    const FFastTagBitmask XorMask = FFastTagBitmask(0b0101ULL) ^ MaskB;
    TestEqual(TEXT("掩码异或运算符生效"), XorMask.Value, 0b0011ULL);

    FFastTagBitmask AssignXorMask(0b0101ULL);
    AssignXorMask ^= MaskB;
    TestEqual(TEXT("掩码复合异或赋值生效"), AssignXorMask.Value, 0b0011ULL);

    constexpr FFastTagBitmask ConstexprMask = []() consteval {
        FFastTagBitmask M(0b0001ULL);
        M.AddTag(1);
        M.RemoveTag(0);
        M.ToggleTag(3);
        return M;
    }();
    static_assert(ConstexprMask.Value == 0b1010ULL);
    static_assert(ConstexprMask.HasTag(1));
    static_assert(!ConstexprMask.HasTag(0));
    static_assert(!ConstexprMask.IsEmpty());
    static_assert(ConstexprMask.HasAny(FFastTagBitmask(0b0010ULL)));
    static_assert(ConstexprMask.HasAll(FFastTagBitmask(0b1010ULL)));
    static_assert(ConstexprMask.HasNone(FFastTagBitmask(0b0101ULL)));

    MaskA.Clear();
    TestTrue(TEXT("Clear 后恢复为空"), MaskA.IsEmpty());

    TMap<FFastTagBitmask, FString> BitmaskMap;
    const FFastTagBitmask KeyMaskA(0b10101ULL);
    const FFastTagBitmask KeyMaskB(0b01010ULL);
    BitmaskMap.Add(KeyMaskA, TEXT("StateA"));
    BitmaskMap.Add(KeyMaskB, TEXT("StateB"));
    const FString* FoundState = BitmaskMap.Find(KeyMaskA);
    TestNotNull(TEXT("FFastTagBitmask 作为 TMap 键可精确命中"), FoundState);
    if (FoundState != nullptr) {
        TestEqual(TEXT("命中值正确"), *FoundState, FString(TEXT("StateA")));
    }

    TArray<uint8> ArchiveBuffer;
    FMemoryWriter Writer(ArchiveBuffer);
    FFastTagBitmask SaveMask(0xDEADBEEFCAFEULL);
    Writer << SaveMask;
    TestTrue(TEXT("归档写入数据非空"), ArchiveBuffer.Num() > 0);

    FMemoryReader Reader(ArchiveBuffer);
    FFastTagBitmask LoadedMask;
    Reader << LoadedMask;
    TestEqual(TEXT("归档序列化往返无损一致"), LoadedMask.Value, 0xDEADBEEFCAFEULL);
    TestTrue(TEXT("往返还原掩码与源掩码相等"), LoadedMask == SaveMask);

    const FFastTagBitmask SmallBitmask(10ULL);
    const FFastTagBitmask LargeBitmask(20ULL);
    TestTrue(TEXT("operator< 比较正确"), SmallBitmask < LargeBitmask);
    TestFalse(TEXT("自身不小于自身"), SmallBitmask < SmallBitmask);

    TArray<FFastTagBitmask> SortBitmasks = { LargeBitmask, SmallBitmask };
    SortBitmasks.Sort();
    TestEqual(TEXT("Sort 排序后首项为 SmallBitmask"), SortBitmasks[0], SmallBitmask);
    TestEqual(TEXT("Sort 排序后末项为 LargeBitmask"), SortBitmasks[1], LargeBitmask);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFastTagDictionaryTest,
    "Game.Core.Containers.FastTagDictionary.MappingAndOverflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FName 与 64 位 Tag 双向映射字典、容量上限保护与批量掩码生成
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFastTagDictionaryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FFastTagDictionary& Dict = FFastTagDictionary::Get();
    Dict.Reset();
    TestEqual(TEXT("重置后字典为空"), Dict.Num(), 0);

    TestEqual(TEXT("None 标签注册返回 INDEX_NONE"), Dict.RegisterOrFindTag(NAME_None), INDEX_NONE);
    TestEqual(TEXT("None 标签查找返回 INDEX_NONE"), Dict.FindTagIndex(NAME_None), INDEX_NONE);
    TestEqual(TEXT("负索引查询名称返回 NAME_None"), Dict.FindTagName(-1), NAME_None);
    TestEqual(TEXT("越界索引查询名称返回 NAME_None"), Dict.FindTagName(100), NAME_None);

    const int32 StunIndex = Dict.RegisterOrFindTag(TEXT("State.Combat.Stun"));
    TestEqual(TEXT("首个注册标签索引为 0"), StunIndex, 0);

    const int32 RequeriedStun = Dict.RegisterOrFindTag(TEXT("State.Combat.Stun"));
    TestEqual(TEXT("重复注册返回相同索引"), RequeriedStun, 0);
    TestEqual(TEXT("通过名称查询索引匹配"), Dict.FindTagIndex(TEXT("State.Combat.Stun")), 0);
    TestEqual(TEXT("通过索引查询名称匹配"), Dict.FindTagName(0), FName(TEXT("State.Combat.Stun")));

    const int32 ArmorIndex = Dict.RegisterOrFindTag(TEXT("State.Combat.SuperArmor"));
    TestEqual(TEXT("第二个注册标签索引为 1"), ArmorIndex, 1);
    TestEqual(TEXT("当前注册总数为 2"), Dict.Num(), 2);

    for (int32 Index = 2; Index < FFastTagDictionary::MaxTagCount; ++Index) {
        const FName AutoName = *FString::Printf(TEXT("State.Auto.%d"), Index);
        const int32 RegisteredIndex = Dict.RegisterOrFindTag(AutoName);
        TestEqual(TEXT("顺序注册索引连续"), RegisteredIndex, Index);
    }
    TestEqual(TEXT("已成功注册 64 个标签"), Dict.Num(), FFastTagDictionary::MaxTagCount);

    const int32 OverflowIndex = Dict.RegisterOrFindTag(TEXT("State.Overflow.Extra"));
    TestEqual(TEXT("超出 64 个上限返回 INDEX_NONE"), OverflowIndex, INDEX_NONE);
    TestEqual(TEXT("注册数量未超标"), Dict.Num(), FFastTagDictionary::MaxTagCount);

    TArray<FName> BatchNames = {
        TEXT("State.Combat.Stun"),
        TEXT("State.Combat.SuperArmor"),
        TEXT("NonExistentTag")
    };
    FFastTagBitmask BatchMask = Dict.MakeMask(BatchNames);
    TestTrue(TEXT("批量掩码包含 Stun"), BatchMask.HasTag(StunIndex));
    TestTrue(TEXT("批量掩码包含 SuperArmor"), BatchMask.HasTag(ArmorIndex));
    TestEqual(TEXT("有效激活数量为 2"), BatchMask.CountBits(), 2);

    bool bSuccessAll = false;
    TArray<FName> ValidBatch = {
        TEXT("State.Combat.Stun"),
        TEXT("State.Combat.SuperArmor")
    };
    FFastTagBitmask ValidMask = Dict.MakeMask(ValidBatch, &bSuccessAll);
    TestTrue(TEXT("全部合法标签转换 MakeMask 返回 true"), bSuccessAll);
    TestEqual(TEXT("全部合法标签掩码 bit 数正确"), ValidMask.CountBits(), 2);

    bool bSuccessPartial = true;
    TArray<FName> InvalidBatch = {
        TEXT("State.Combat.Stun"),
        NAME_None
    };
    FFastTagBitmask PartialMask = Dict.MakeMask(InvalidBatch, &bSuccessPartial);
    TestFalse(TEXT("包含非法标签时 MakeMask 置 OutAllSucceeded 为 false"), bSuccessPartial);
    TestTrue(TEXT("合法标签仍成功置入掩码"), PartialMask.HasTag(StunIndex));

    Dict.Reset();
    TestEqual(TEXT("测试完成重置后数量为 0"), Dict.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FFastTagDictionaryConcurrencyTest,
    "Game.Core.Containers.FastTagDictionary.Concurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程并发注册与查询 FFastTagDictionary 时的线程安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FFastTagDictionaryConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FFastTagDictionary& Dict = FFastTagDictionary::Get();
    Dict.Reset();

    constexpr int32 ThreadCount = 20;
    std::atomic<int32> ValidIndexCount{0};

    ParallelFor(ThreadCount, [&Dict, &ValidIndexCount](int32 Index) {
        const FName ThreadTagName = *FString::Printf(TEXT("Tag.Thread.%d"), Index % 10);
        const int32 AllocatedIndex = Dict.RegisterOrFindTag(ThreadTagName);
        if (AllocatedIndex >= 0 && AllocatedIndex < FFastTagDictionary::MaxTagCount) {
            ValidIndexCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    TestEqual(TEXT("所有并发注册线程均返回合法有效索引"), ValidIndexCount.load(), ThreadCount);
    TestTrue(TEXT("注册的唯一标签数量不大于 10"), Dict.Num() <= 10);

    Dict.Reset();
    return true;
}

#endif
