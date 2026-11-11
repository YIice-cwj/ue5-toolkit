/**
 * @file FGameDistIdTest.cpp
 * @brief 64 位分布式单调递增雪花唯一 ID 与生成器单元测试
 */

#include "Types/FGameDistId.h"
#include "Types/FGameDistIdGenerator.h"

#if WITH_AUTOMATION_TESTS

#include "Async/ParallelFor.h"
#include "HAL/CriticalSection.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDistIdBitfieldsAndSerializationTest,
    "Game.Core.Id.DistId.BitfieldsAndSerialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDistId 位段合成、分解反解与 FArchive 原生序列化支持
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDistIdBitfieldsAndSerializationTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDistId EmptyId;
    TestFalse(TEXT("默认构造 ID 无效"), EmptyId.IsValid());
    TestEqual(TEXT("默认构造 ID 裸值为 0"), EmptyId.GetRawValue(), 0ULL);

    const int64 TimestampDelta = 123456789LL;
    const uint16 NodeId = 42;
    const uint16 Sequence = 1024;

    const FGameDistId SyntheticId = FGameDistId::FromParts(TimestampDelta, NodeId, Sequence);
    TestTrue(TEXT("合成 ID 有效"), SyntheticId.IsValid());
    TestEqual(TEXT("提取相对时间戳偏移精确匹配"), SyntheticId.GetTimestampDelta(), TimestampDelta);
    TestEqual(TEXT("提取节点编号精确匹配"), SyntheticId.GetNodeId(), NodeId);
    TestEqual(TEXT("提取自增序列精确匹配"), SyntheticId.GetSequence(), Sequence);

    const int64 CustomEpoch = 1700000000000LL;
    TestEqual(
        TEXT("绝对时间戳反解等于 Epoch + Delta"),
        SyntheticId.GetTimestampMilliseconds(CustomEpoch),
        CustomEpoch + TimestampDelta
    );

    const FGameDistId NegativeTimestampId = FGameDistId::FromParts(-500LL, 3, 9);
    TestFalse(TEXT("负时间戳偏移被安全拦截返回无效 ID"), NegativeTimestampId.IsValid());

    const int64 MaxTimestamp = static_cast<int64>(FGameDistId::MAX_TIMESTAMP_DELTA);
    const FGameDistId MaxTimestampId = FGameDistId::FromParts(MaxTimestamp, 3, 9);
    TestTrue(TEXT("41 位上限时间戳偏移合法有效"), MaxTimestampId.IsValid());
    TestEqual(TEXT("41 位上限时间戳偏移精确还原"), MaxTimestampId.GetTimestampDelta(), MaxTimestamp);

    const int64 OverflowTimestamp = MaxTimestamp + 12345LL;
    const FGameDistId OverflowTimestampId = FGameDistId::FromParts(OverflowTimestamp, 3, 9);
    TestFalse(TEXT("超 41 位时间戳偏移被安全拦截返回无效 ID"), OverflowTimestampId.IsValid());

    FBufferArchive SaveArchive;
    FGameDistId SaveTarget = SyntheticId;
    SaveArchive << SaveTarget;

    FMemoryReader LoadArchive(SaveArchive);
    FGameDistId LoadedId;
    LoadArchive << LoadedId;

    TestTrue(TEXT("反序列化后 ID 保持有效"), LoadedId.IsValid());
    TestEqual(TEXT("反序列化后裸数值精确一致"), LoadedId.GetRawValue(), SyntheticId.GetRawValue());
    TestTrue(TEXT("运算符 == 判定相等"), LoadedId == SyntheticId);

    TestEqual(TEXT("ToHex 格式化有效"), SyntheticId.ToHex(), FString::Printf(TEXT("0x%016llX"), SyntheticId.GetRawValue()));

    FGameDistId ParsedDistDec;
    TestTrue(TEXT("FromString 解析十进制成功"), FGameDistId::FromString(SyntheticId.ToString(), ParsedDistDec));
    TestEqual(TEXT("十进制还原精确"), ParsedDistDec, SyntheticId);

    FGameDistId ParsedDistHex;
    TestTrue(TEXT("FromString 解析 0x 十六进制成功"), FGameDistId::FromString(SyntheticId.ToHex(), ParsedDistHex));
    TestEqual(TEXT("十六进制还原精确"), ParsedDistHex, SyntheticId);

    const uint32 DistHashA = GetTypeHash(SyntheticId);
    const uint32 DistHashB = GetTypeHash(FGameDistId(SyntheticId.GetRawValue() + 1ULL));
    TestNotEqual(TEXT("邻近雪花 ID 散列哈希绝不碰撞"), DistHashA, DistHashB);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDistIdGeneratorMonotonicAndConcurrencyTest,
    "Game.Core.Id.DistId.MonotonicAndConcurrency",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FGameDistIdGenerator 单调递增性与高并发无碰撞 ID 生成
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameDistIdGeneratorMonotonicAndConcurrencyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameDistIdGenerator Generator(7);
    TestEqual(TEXT("正确初始化节点号"), Generator.GetNodeId(), static_cast<uint16>(7));

    FGameDistId PrevId = Generator.NextId();
    TestTrue(TEXT("生成首个 ID 有效"), PrevId.IsValid());
    TestEqual(TEXT("生成 ID 的节点号匹配"), PrevId.GetNodeId(), static_cast<uint16>(7));

    for (int32 Index = 0; Index < 100; ++Index) {
        const FGameDistId CurrId = Generator.NextId();
        TestTrue(TEXT("单调递增比较成立"), PrevId < CurrId);
        TestFalse(TEXT("相邻生成绝不相等"), PrevId == CurrId);
        PrevId = CurrId;
    }

    constexpr int32 TOTAL_IDS = 10000;
    TArray<uint64> GeneratedRawIds;
    GeneratedRawIds.SetNumUninitialized(TOTAL_IDS);

    ParallelFor(TOTAL_IDS, [&Generator, &GeneratedRawIds](int32 Idx) {
        GeneratedRawIds[Idx] = Generator.NextId().GetRawValue();
    });

    TSet<uint64> UniqueSet;
    UniqueSet.Reserve(TOTAL_IDS);
    for (int32 Idx = 0; Idx < TOTAL_IDS; ++Idx) {
        UniqueSet.Add(GeneratedRawIds[Idx]);
    }

    TestEqual(TEXT("高并发批量生成 10000 个 ID 零碰撞"), UniqueSet.Num(), TOTAL_IDS);

    int64 MockClockMs = FGameDistIdGenerator::DEFAULT_CUSTOM_EPOCH + 5000LL;
    FGameDistIdGenerator DeterministicGen(3);
    DeterministicGen.SetClockSource([&MockClockMs]() {
        return MockClockMs;
    });

    FGameDistId LastDeterministicId;
    for (int32 Idx = 0; Idx < 4097; ++Idx) {
        const FGameDistId Next = DeterministicGen.NextId();
        TestTrue(TEXT("停滞时钟下连续 4097 次生成（跨 4096 序列号溢出）严格单调递增且不死循环"), LastDeterministicId < Next);
        LastDeterministicId = Next;
    }
    TestEqual(TEXT("同毫秒 4096 溢出后逻辑时间戳自动推进 1ms"), LastDeterministicId.GetTimestampDelta(), 5001LL);

    MockClockMs = FGameDistIdGenerator::DEFAULT_CUSTOM_EPOCH + 4998LL;
    const FGameDistId SmallRollbackId = DeterministicGen.NextId();
    TestTrue(TEXT("小幅时钟回拨 (<=5ms) 有界恢复后保持严格单调递增"), LastDeterministicId < SmallRollbackId);

    MockClockMs = FGameDistIdGenerator::DEFAULT_CUSTOM_EPOCH + 1000LL;
    const FGameDistId LargeRollbackId = DeterministicGen.NextId();
    TestTrue(TEXT("大幅时钟回拨 (>5ms) 逻辑时钟兜底保持严格单调递增"), SmallRollbackId < LargeRollbackId);

    return true;
}

#endif
