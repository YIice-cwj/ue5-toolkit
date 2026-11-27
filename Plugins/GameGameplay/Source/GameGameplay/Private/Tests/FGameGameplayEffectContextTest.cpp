/**
 * @file FGameGameplayEffectContextTest.cpp
 * @brief 打击上下文脚本结构、5-bit 紧凑网络序列化与损坏流容错自动化测试套件
 */

#include "Damage/FGameGameplayEffectContext.h"

#if WITH_AUTOMATION_TESTS

#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "UObject/CoreNet.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayEffectContextTest,
    "Game.Gameplay.EffectContext",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证打击上下文数值读写、标签维护、Duplicate 独立拷贝、紧凑位打包及网络序列化往返
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameGameplayEffectContextTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameGameplayEffectContext Source;
    Source.SetActualDamage(37.5f);

    const FGameplayTag CriticalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Critical")));
    const FGameplayTag FatalTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.Fatal")));
    const FGameplayTag HitStopTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Feedback.HitStop")));
    const FGameplayTag ChannelPhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Channel.Physical")));

    Source.AddFeedbackTag(CriticalTag);
    Source.SetDynamicValue(HitStopTag, 0.12f);
    Source.SetActualChannelDamage(ChannelPhysTag, 37.5f);
    Source.SetFeedbackEventId(321);

    TestTrue(TEXT("上下文包含反馈标签"), Source.HasFeedbackTag(CriticalTag));
    TestFalse(TEXT("上下文未添加标签不存在"), Source.HasFeedbackTag(FatalTag));
    TestTrue(TEXT("上下文持有动态数值存在判定"), Source.HasDynamicValue(HitStopTag));
    TestFalse(TEXT("上下文未持有动态数值存在判定"), Source.HasDynamicValue(FatalTag));
    TestEqual(TEXT("上下文获取动态数值正确"), Source.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("上下文获取未设置动态数值回落默认值"), Source.GetDynamicValue(FatalTag, -1.0f), -1.0f);
    TestEqual(TEXT("上下文获取通道实际伤害正确"), Source.GetActualChannelDamage(ChannelPhysTag), 37.5f);

    TUniquePtr<FGameplayEffectContext> Duplicated(Source.Duplicate());
    const FGameGameplayEffectContext* Copy = static_cast<const FGameGameplayEffectContext*>(Duplicated.Get());
    TestNotNull(TEXT("Duplicate 返回有效上下文"), Copy);
    if (Copy == nullptr) {
        return false;
    }
    TestTrue(TEXT("Duplicate 保持本工程脚本结构"), Copy->GetScriptStruct() == FGameGameplayEffectContext::StaticStruct());
    TestNull(TEXT("Duplicate 无 HitResult 时结果为空"), Copy->GetHitResult());
    TestEqual(TEXT("Duplicate 复制实际伤害"), Copy->GetActualDamage(), 37.5f);
    TestTrue(TEXT("Duplicate 复制反馈标签"), Copy->HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("Duplicate 复制动态数值"), Copy->GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("Duplicate 复制通道实际伤害"), Copy->GetActualChannelDamage(ChannelPhysTag), 37.5f);
    TestEqual(TEXT("Duplicate 复制反馈事件 ID"), Copy->GetFeedbackEventId(), static_cast<uint16>(321));

    FGameGameplayEffectContext ContextWithHit;
    FHitResult SourceHit;
    SourceHit.ImpactPoint = FVector(10.0f, 20.0f, 30.0f);
    ContextWithHit.AddHitResult(SourceHit, true);
    TUniquePtr<FGameplayEffectContext> DupWithHit(ContextWithHit.Duplicate());
    TestNotNull(TEXT("Duplicate 复制 HitResult 有效"), DupWithHit->GetHitResult());
    if (DupWithHit->GetHitResult() != nullptr) {
        TestEqual(TEXT("Duplicate 复制 HitResult 命中点一致"), DupWithHit->GetHitResult()->ImpactPoint, FVector(10.0f, 20.0f, 30.0f));
    }

    Source.RemoveFeedbackTag(CriticalTag);
    TestFalse(TEXT("移除反馈标签后不存在"), Source.HasFeedbackTag(CriticalTag));
    Source.AddFeedbackTag(CriticalTag);

    FGameplayEffectContextHandle EmptyContextHandle;
    TestNull(TEXT("空句柄提取上下文返回 nullptr"), FGameGameplayEffectContext::ExtractEffectContext(EmptyContextHandle));

    FGameplayEffectContextHandle NativeContextHandle(new FGameplayEffectContext());
    TestNull(TEXT("原生 GE 上下文句柄提取返回 nullptr"), FGameGameplayEffectContext::ExtractEffectContext(NativeContextHandle));

    FGameplayEffectContextHandle ValidContextHandle(Source.Duplicate());
    TestNotNull(TEXT("有效 GE 上下文句柄提取成功"), FGameGameplayEffectContext::ExtractEffectContext(ValidContextHandle));

    FBufferArchive Writer;
    bool bWriteSuccess = false;
    TestTrue(TEXT("写出 NetSerialize 成功"), Source.NetSerialize(Writer, nullptr, bWriteSuccess));
    TestTrue(TEXT("写出成功标记置位"), bWriteSuccess);

    FMemoryReader Reader(Writer);
    FGameGameplayEffectContext Loaded;
    bool bReadSuccess = false;
    TestTrue(TEXT("读入 NetSerialize 成功"), Loaded.NetSerialize(Reader, nullptr, bReadSuccess));
    TestTrue(TEXT("读入成功标记置位"), bReadSuccess);
    TestEqual(TEXT("NetSerialize 往返保持实际伤害"), Loaded.GetActualDamage(), 37.5f);
    TestTrue(TEXT("NetSerialize 往返保持反馈标签"), Loaded.HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("NetSerialize 往返保持动态数值"), Loaded.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("NetSerialize 往返保持通道伤害"), Loaded.GetActualChannelDamage(ChannelPhysTag), 37.5f);
    TestEqual(TEXT("NetSerialize 往返保持反馈事件 ID"), Loaded.GetFeedbackEventId(), static_cast<uint16>(321));

    const FGameDamageResult& Result = Source.ToDamageResult();
    TestEqual(TEXT("ToDamageResult 常引用总伤害对齐"), Result.TotalActualDamage, 37.5f);
    TestTrue(TEXT("ToDamageResult 反馈标签对齐"), Result.HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("ToDamageResult 动态数值对齐"), Result.GetDynamicValue(HitStopTag), 0.12f);
    TestEqual(TEXT("ToDamageResult 通道实际伤害对齐"), Result.GetActualChannelDamageExact(ChannelPhysTag), 37.5f);

    const float NearZeroDamage = UE_SMALL_NUMBER * 0.5f;
    Source.SetActualDamage(NearZeroDamage);
    Source.SetActualChannelDamage(ChannelPhysTag, NearZeroDamage);

    FBufferArchive NearZeroWriter;
    bool bNearZeroWriteSuccess = false;
    TestTrue(TEXT("近零非零伤害写出成功"), Source.NetSerialize(NearZeroWriter, nullptr, bNearZeroWriteSuccess));
    TestTrue(TEXT("近零非零伤害写出标记置位"), bNearZeroWriteSuccess);

    FMemoryReader NearZeroReader(NearZeroWriter);
    FGameGameplayEffectContext NearZeroLoaded;
    bool bNearZeroReadSuccess = false;
    TestTrue(TEXT("近零非零伤害读入成功"), NearZeroLoaded.NetSerialize(NearZeroReader, nullptr, bNearZeroReadSuccess));
    TestTrue(TEXT("近零非零伤害读入标记置位"), bNearZeroReadSuccess);
    TestEqual(TEXT("NetSerialize 保留近零非零总伤害"), NearZeroLoaded.GetActualDamage(), NearZeroDamage, 0.0f);
    TestEqual(TEXT("NetSerialize 保留近零非零通道伤害"), NearZeroLoaded.GetActualChannelDamage(ChannelPhysTag), NearZeroDamage, 0.0f);

    FGameGameplayEffectContext CompactZeroId;
    FBufferArchive CompactZeroWriter;
    bool bCompactZeroOk = false;
    TestTrue(TEXT("零事件 ID 空上下文写出成功"), CompactZeroId.NetSerialize(CompactZeroWriter, nullptr, bCompactZeroOk));

    FGameGameplayEffectContext CompactNonZeroId;
    CompactNonZeroId.SetFeedbackEventId(77);
    FBufferArchive CompactNonZeroWriter;
    bool bCompactNonZeroOk = false;
    TestTrue(TEXT("非零事件 ID 空上下文写出成功"), CompactNonZeroId.NetSerialize(CompactNonZeroWriter, nullptr, bCompactNonZeroOk));
    TestEqual(
        TEXT("5-bit 位掩码紧凑打包省略零事件 ID 节省 2 字节"),
        CompactNonZeroWriter.Num() - CompactZeroWriter.Num(),
        2
    );

    FNetBitWriter BitWriterZeroId(4096);
    bool bBitZeroWriteOk = false;
    TestTrue(TEXT("FNetBitWriter 零事件 ID 写出成功"), CompactZeroId.NetSerialize(BitWriterZeroId, nullptr, bBitZeroWriteOk));
    TestTrue(TEXT("FNetBitWriter 零事件 ID 写出标记置位"), bBitZeroWriteOk);

    FNetBitWriter BitWriterNonZeroId(4096);
    bool bBitNonZeroWriteOk = false;
    TestTrue(TEXT("FNetBitWriter 非零事件 ID 写出成功"), CompactNonZeroId.NetSerialize(BitWriterNonZeroId, nullptr, bBitNonZeroWriteOk));
    TestTrue(TEXT("FNetBitWriter 非零事件 ID 写出标记置位"), bBitNonZeroWriteOk);
    TestEqual(
        TEXT("真实 UE 网络位流中 5-bit 打包省略零事件 ID 精确节省 16 bits"),
        BitWriterNonZeroId.GetNumBits() - BitWriterZeroId.GetNumBits(),
        static_cast<int64>(16)
    );

    FNetBitWriter FullBitWriter(8192);
    bool bFullBitWriteOk = false;
    TestTrue(TEXT("FNetBitWriter 全载荷写出成功"), Source.NetSerialize(FullBitWriter, nullptr, bFullBitWriteOk));
    TestTrue(TEXT("FNetBitWriter 全载荷写出标记置位"), bFullBitWriteOk);

    FNetBitReader FullBitReader(nullptr, FullBitWriter.GetData(), FullBitWriter.GetNumBits());
    FGameGameplayEffectContext BitLoaded;
    bool bFullBitReadOk = false;
    TestTrue(TEXT("FNetBitReader 全载荷读入成功"), BitLoaded.NetSerialize(FullBitReader, nullptr, bFullBitReadOk));
    TestTrue(TEXT("FNetBitReader 全载荷读入标记置位"), bFullBitReadOk);
    TestEqual(TEXT("位流往返保持事件 ID"), BitLoaded.GetFeedbackEventId(), Source.GetFeedbackEventId());
    TestEqual(TEXT("位流往返保持实际总伤害"), BitLoaded.GetActualDamage(), Source.GetActualDamage(), 0.0f);
    TestTrue(TEXT("位流往返保持反馈标签"), BitLoaded.HasFeedbackTag(CriticalTag));
    TestEqual(TEXT("位流往返保持通道伤害"), BitLoaded.GetActualChannelDamage(ChannelPhysTag), Source.GetActualChannelDamage(ChannelPhysTag), 0.0f);
    TestEqual(TEXT("位流往返保持动态数值"), BitLoaded.GetDynamicValue(HitStopTag), 0.12f);

    FNetBitReader ErrorBitReader(nullptr, FullBitWriter.GetData(), FullBitWriter.GetNumBits());
    ErrorBitReader.SetError();
    FGameGameplayEffectContext ErrorBitTarget;
    bool bErrorBitOk = true;
    TestFalse(TEXT("带错误态的位流反序列化必须立即返回 false"), ErrorBitTarget.NetSerialize(ErrorBitReader, nullptr, bErrorBitOk));
    TestFalse(TEXT("带错误态的位流反序列化 bOutSuccess 必须为 false"), bErrorBitOk);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameEffectContextNetSerializeLimitTest,
    "Game.Gameplay.EffectContextLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证通道与动态数值超限截断、全空反序列化复位（含事件 ID 归零）及截断畸形流失败传播
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameEffectContextNetSerializeLimitTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameGameplayEffectContext Source;
    const int32 OverflowCount = FGameGameplayEffectContext::MaxNetSerializedEntries + 8;

    for (int32 Index = 0; Index < OverflowCount; ++Index) {
        const FGameplayTag EntryTag = UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*FString::Printf(TEXT("Damage.Channel.LimitTest.%d"), Index))
        );
        Source.SetActualChannelDamage(EntryTag, static_cast<float>(Index));
        Source.SetDynamicValue(EntryTag, static_cast<float>(Index) * 0.5f);
    }

    TestEqual(TEXT("源上下文通道数超过上限"), Source.GetActualChannels().Num(), OverflowCount);

    FBufferArchive Writer;
    bool bWriteSuccess = false;
    TestTrue(TEXT("超限上下文写出成功"), Source.NetSerialize(Writer, nullptr, bWriteSuccess));
    TestTrue(TEXT("写出成功标记置位"), bWriteSuccess);

    FMemoryReader Reader(Writer);
    FGameGameplayEffectContext Loaded;
    bool bReadSuccess = false;
    TestTrue(TEXT("超限上下文读入成功"), Loaded.NetSerialize(Reader, nullptr, bReadSuccess));
    TestTrue(TEXT("读入成功标记置位"), bReadSuccess);
    TestEqual(TEXT("通道明细截断至网络上限"), Loaded.GetActualChannels().Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);
    TestEqual(TEXT("动态数值截断至网络上限"), Loaded.GetDynamicValues().Num(), FGameGameplayEffectContext::MaxNetSerializedEntries);

    FGameGameplayEffectContext EmptyContext;
    FBufferArchive EmptyWriter;
    bool bEmptyWriteSuccess = false;
    TestTrue(TEXT("空上下文写出成功"), EmptyContext.NetSerialize(EmptyWriter, nullptr, bEmptyWriteSuccess));
    TestTrue(TEXT("空写出成功标记置位"), bEmptyWriteSuccess);

    FMemoryReader EmptyReader(EmptyWriter);
    FGameGameplayEffectContext EmptyLoaded;
    EmptyLoaded.SetFeedbackEventId(999);
    EmptyLoaded.SetActualDamage(50.0f);
    const FGameplayTag DummyTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.EmptyContextTag")));
    EmptyLoaded.AddFeedbackTag(DummyTag);
    EmptyLoaded.SetActualChannelDamage(DummyTag, 10.0f);
    EmptyLoaded.SetDynamicValue(DummyTag, 5.0f);

    bool bEmptyReadSuccess = false;
    TestTrue(TEXT("空上下文读入成功"), EmptyLoaded.NetSerialize(EmptyReader, nullptr, bEmptyReadSuccess));
    TestTrue(TEXT("空读入成功标记置位"), bEmptyReadSuccess);
    TestEqual(TEXT("空读入重置反馈事件 ID 为 0"), EmptyLoaded.GetFeedbackEventId(), static_cast<uint16>(0));
    TestEqual(TEXT("空读入重置实际伤害为 0"), EmptyLoaded.GetActualDamage(), 0.0f);
    TestFalse(TEXT("空读入重置反馈标签"), EmptyLoaded.HasFeedbackTag(DummyTag));
    TestEqual(TEXT("空读入重置通道明细"), EmptyLoaded.GetActualChannels().Num(), 0);
    TestEqual(TEXT("空读入重置动态数值"), EmptyLoaded.GetDynamicValues().Num(), 0);

    TArray<uint8> TruncatedPayload(Writer.GetData(), FMath::Max(1, Writer.Num() / 3));
    FMemoryReader TruncatedReader(TruncatedPayload);
    FGameGameplayEffectContext CorruptedTarget;
    bool bCorruptedSuccess = true;
    const bool bCorruptedResult = CorruptedTarget.NetSerialize(TruncatedReader, nullptr, bCorruptedSuccess);
    TestFalse(TEXT("截断损坏流 NetSerialize 必须返回 false"), bCorruptedResult);
    TestFalse(TEXT("截断损坏流 bOutSuccess 必须为 false 且不被后续状态覆写"), bCorruptedSuccess);
    TestEqual(TEXT("截断损坏流加载失败后不残留半初始化通道条目"), CorruptedTarget.GetActualChannels().Num(), 0);

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
