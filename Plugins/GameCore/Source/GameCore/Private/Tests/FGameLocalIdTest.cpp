/**
 * @file FGameLocalIdTest.cpp
 * @brief 极速原子无锁本地唯一 ID 生成器单元测试
 */

#include "Types/FGameLocalId.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "HAL/CriticalSection.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLocalIdBasicOpsTest,
    "Game.Core.Id.LocalId.BasicOps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameLocalId 基础操作、比较逻辑、哈希与序列化
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLocalIdBasicOpsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameLocalId EmptyId;
    TestFalse(TEXT("默认构造 ID 无效"), EmptyId.IsValid());
    TestFalse(TEXT("布尔转换操作符判假"), static_cast<bool>(EmptyId));
    TestEqual(TEXT("默认构造裸值为 0"), EmptyId.GetValue(), 0ULL);

    const FGameLocalId IdA(100ULL);
    const FGameLocalId IdB(200ULL);
    const FGameLocalId IdC(100ULL);

    TestTrue(TEXT("IdA 有效"), IdA.IsValid());
    TestTrue(TEXT("IdA 布尔转换判真"), static_cast<bool>(IdA));
    TestEqual(TEXT("IdA 相等 IdC"), IdA, IdC);
    TestTrue(TEXT("IdA 小于 IdB"), IdA < IdB);
    TestTrue(TEXT("IdA 小于等于 IdC"), IdA <= IdC);
    TestTrue(TEXT("IdB 大于 IdA"), IdB > IdA);
    TestTrue(TEXT("IdC 大于等于 IdA"), IdC >= IdA);
    TestNotEqual(TEXT("IdA 不等于 IdB"), IdA, IdB);

    TestEqual(TEXT("相等 ID 具有相同哈希"), GetTypeHash(IdA), GetTypeHash(IdC));

    const uint32 Hash1 = GetTypeHash(FGameLocalId(1ULL));
    const uint32 Hash2 = GetTypeHash(FGameLocalId(2ULL));
    TestNotEqual(TEXT("相邻连续自增 ID 的雪崩哈希值绝不相同"), Hash1, Hash2);

    TestEqual(TEXT("ToString 输出十进制"), IdA.ToString(), TEXT("100"));
    TestEqual(TEXT("ToHex 输出标准十六进制"), IdA.ToHex(), TEXT("0x0000000000000064"));

    FGameLocalId ParsedDec;
    TestTrue(TEXT("FromString 解析十进制成功"), FGameLocalId::FromString(TEXT("123456789"), ParsedDec));
    TestEqual(TEXT("十进制解析数值精确"), ParsedDec.GetValue(), 123456789ULL);

    FGameLocalId ParsedHex;
    TestTrue(TEXT("FromString 解析 0x 十六进制成功"), FGameLocalId::FromString(TEXT("0x1A2B3C"), ParsedHex));
    TestEqual(TEXT("十六进制解析数值精确"), ParsedHex.GetValue(), 0x1A2B3CULL);

    FGameLocalId InvalidId;
    TestFalse(TEXT("非法字符解析失败"), FGameLocalId::FromString(TEXT("123abcXYZ"), InvalidId));

    const FGameDistId DistId = IdA.ToDistId();
    TestEqual(TEXT("ToDistId 裸值匹配"), DistId.GetRawValue(), IdA.GetValue());
    const FGameLocalId ConvertedLocal = FGameLocalId::FromDistId(DistId);
    TestEqual(TEXT("FromDistId 还原一致"), ConvertedLocal, IdA);

    FBufferArchive Ar;
    FGameLocalId SerializedId = IdB;
    Ar << SerializedId;

    FMemoryReader Reader(Ar);
    FGameLocalId DeserializedId;
    Reader << DeserializedId;

    TestEqual(TEXT("反序列化后 ID 裸值与原值一致"), DeserializedId.GetValue(), 200ULL);
    TestEqual(TEXT("反序列化后 ID 对象相等"), DeserializedId, IdB);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLocalIdConcurrencyAndResetTest,
    "Game.Core.Id.LocalId.ConcurrencyAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证多线程高并发生成无冲突性与重置机制
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLocalIdConcurrencyAndResetTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameLocalIdGenerator::ResetForTesting(1000ULL);
    TestEqual(TEXT("探测当前序号为 1000"), FGameLocalIdGenerator::PeekCurrent(), 1000ULL);

    const FGameLocalId FirstId = FGameLocalIdGenerator::NextId();
    TestEqual(TEXT("重置后首个 ID 值为 1000"), FirstId.GetValue(), 1000ULL);

    constexpr int32 TotalCount = 10000;
    TArray<FGameLocalId> GeneratedIds;
    GeneratedIds.SetNum(TotalCount);

    ParallelFor(TotalCount, [&GeneratedIds](int32 Index) {
        GeneratedIds[Index] = FGameLocalIdGenerator::NextId();
    });

    TSet<uint64> UniqueValues;
    UniqueValues.Reserve(TotalCount);
    for (const FGameLocalId& Id : GeneratedIds) {
        TestTrue(TEXT("生成 ID 有效"), Id.IsValid());
        UniqueValues.Add(Id.GetValue());
    }

    TestEqual(TEXT("多线程并发生成 10000 个 ID 零重复零碰撞"), UniqueValues.Num(), TotalCount);

    return true;
}

#endif
