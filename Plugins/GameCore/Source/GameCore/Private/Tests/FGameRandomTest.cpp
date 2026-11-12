/**
 * @file FGameRandomTest.cpp
 * @brief FGameRandomStream、FPseudoRandomDistribution、TShuffleBag、TWeightedPicker 与 URandomSubsystem 单元测试
 */

#include "Random/FGameRandomStream.h"
#include "Random/FPseudoRandomDistribution.h"
#include "Random/TShuffleBag.h"
#include "Random/TWeightedPicker.h"
#include "Random/URandomSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace {

UWorld* FindTestWorldForRandom() {
    if (GEngine == nullptr) {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World() != nullptr) {
            return Context.World();
        }
    }
    return nullptr;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameRandomStreamDeterminismAndSamplingTest,
    "Game.Core.Random.StreamDeterminismAndSampling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证隔离随机流确定性复现、状态序列化、区间边界交换、全跨度无溢出与空间几何采样契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameRandomStreamDeterminismAndSamplingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRandomStream StreamA(123456789ULL);
    FGameRandomStream StreamB(123456789ULL);
    FGameRandomStream StreamDiff(123456790ULL);
    FGameRandomStream DefaultStreamA;
    FGameRandomStream DefaultStreamB;

    TestTrue(TEXT("相同初始状态两实例等号成立"), StreamA == StreamB);
    TestTrue(TEXT("不同初始状态两实例不等号成立"), StreamA != StreamDiff);
    TestEqual(TEXT("默认构造两实例初始状态一致"), DefaultStreamA.NextUInt32(), DefaultStreamB.NextUInt32());

    bool bAnyDiffBetweenSeeds = false;
    for (int32 Index = 0; Index < 64; ++Index) {
        const uint32 ValA = StreamA.NextUInt32();
        const uint32 ValB = StreamB.NextUInt32();
        TestEqual(TEXT("相同种子序列逐位一致"), ValA, ValB);
        if (ValA != StreamDiff.NextUInt32()) {
            bAnyDiffBetweenSeeds = true;
        }
    }
    TestTrue(TEXT("相邻种子经 SplitMix64 混合后产出不同序列"), bAnyDiffBetweenSeeds);

    TArray<uint8> StateBytes;
    {
        FMemoryWriter Writer(StateBytes);
        StreamA.SerializeState(Writer);
    }
    FGameRandomStream RestoredStream(999ULL);
    {
        FMemoryReader Reader(StateBytes);
        RestoredStream.SerializeState(Reader);
    }
    for (int32 Index = 0; Index < 16; ++Index) {
        TestEqual(TEXT("SerializeState 恢复后后续输出与原流完全一致"), RestoredStream.NextUInt32(), StreamA.NextUInt32());
    }

    TArray<uint8> StreamOpBytes;
    {
        FMemoryWriter OpWriter(StreamOpBytes);
        OpWriter << StreamA;
    }
    FGameRandomStream RestoredStreamViaOp2(888ULL);
    {
        FMemoryReader OpReader(StreamOpBytes);
        OpReader << RestoredStreamViaOp2;
    }
    TestEqual(TEXT("operator<< 流式反序列化状态一致"), RestoredStreamViaOp2.GetState(), StreamA.GetState());

    TestEqual(TEXT("NextBoundedUInt32(0) 返回 0"), StreamA.NextBoundedUInt32(0u), 0u);
    TestEqual(TEXT("NextBoundedUInt32(1) 返回 0"), StreamA.NextBoundedUInt32(1u), 0u);
    TestEqual(TEXT("单点闭区间 RandRange(7, 7) 返回 7"), StreamA.RandRange(7, 7), 7);

    const int32 FullSpanSample = StreamA.RandRange(MIN_int32, MAX_int32);
    TestTrue(TEXT("全 int32 跨度不发生有符号减法溢出"), FullSpanSample >= MIN_int32 && FullSpanSample <= MAX_int32);

    bool bHitLower = false;
    bool bHitUpper = false;
    for (int32 Index = 0; Index < 512; ++Index) {
        const int32 Sample = StreamA.RandRange(5, 2);
        TestTrue(TEXT("反向参数 RandRange(5, 2) 自动交换并落在 [2, 5]"), Sample >= 2 && Sample <= 5);
        bHitLower = bHitLower || (Sample == 2);
        bHitUpper = bHitUpper || (Sample == 5);

        const float UnitFloat = StreamA.FRand();
        TestTrue(TEXT("FRand 严格处于 [0.0, 1.0) 半开区间"), UnitFloat >= 0.0f && UnitFloat < 1.0f);

        const float RangeFloat = StreamA.FRandRange(10.0f, -2.0f);
        TestTrue(TEXT("反向参数 FRandRange(10, -2) 落在 [-2, 10]"), RangeFloat >= -2.0f && RangeFloat <= 10.0f);
    }
    TestTrue(TEXT("闭区间上下端点均可被采样命中"), bHitLower && bHitUpper);

    TestFalse(TEXT("概率 <= 0 恒不命中"), StreamA.RollChance(0.0f));
    TestFalse(TEXT("负概率恒不命中"), StreamA.RollChance(-0.5f));
    TestTrue(TEXT("概率 >= 1 恒命中"), StreamA.RollChance(1.0f));
    TestTrue(TEXT("超 1 概率恒命中"), StreamA.RollChance(1.5f));

    int32 HitCount = 0;
    constexpr int32 TotalRolls = 20000;
    for (int32 Index = 0; Index < TotalRolls; ++Index) {
        if (StreamA.RollChance(0.35f)) {
            ++HitCount;
        }
    }
    const float EmpiricalChance = static_cast<float>(HitCount) / static_cast<float>(TotalRolls);
    TestTrue(TEXT("RollChance(0.35) 样本频率收敛于 [0.33, 0.37]"), FMath::IsWithinInclusive(EmpiricalChance, 0.33f, 0.37f));

    for (int32 Index = 0; Index < 128; ++Index) {
        const FVector UnitDir = StreamA.RandomUnitVector();
        TestTrue(TEXT("RandomUnitVector 模长归一化"), UnitDir.IsNormalized());
    }

    TestTrue(TEXT("零轴向圆锥采样返回零向量"), StreamA.RandomVectorInConeRad(FVector::ZeroVector, 0.5f).IsZero());
    const FVector ForwardAxis(1.0, 0.0, 0.0);
    TestTrue(TEXT("零半张角圆锥采样精确返回轴向"), StreamA.RandomVectorInConeRad(ForwardAxis, 0.0f).Equals(ForwardAxis, 1e-4f));

    const float HalfAngle = UE_PI / 6.0f;
    const float MinCos = FMath::Cos(HalfAngle) - 1e-3f;
    for (int32 Index = 0; Index < 256; ++Index) {
        const FVector ConeVec = StreamA.RandomVectorInConeRad(FVector(2.0, 3.0, 6.0), HalfAngle);
        TestTrue(TEXT("圆锥采样向量为单位向量"), ConeVec.IsNormalized());
        const float DotVal = static_cast<float>(FVector::DotProduct(ConeVec, FVector(2.0, 3.0, 6.0).GetSafeNormal()));
        TestTrue(TEXT("圆锥采样向量严格落在半张角立体角内"), DotVal >= MinCos);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FPseudoRandomDistributionTest,
    "Game.Core.Random.PseudoRandomDistribution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 PRD 伪随机平滑分布器极值边界、最大连黑上限保底以及大样本宏观期望无偏性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FPseudoRandomDistributionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRandomStream Stream(987654321ULL);

    FPseudoRandomDistribution ZeroPrd(0.0f);
    TestEqual(TEXT("0 概率常数 C 为 0"), ZeroPrd.GetIncrementConstant(), 0.0f);
    TestEqual(TEXT("0 概率下一击概率为 0"), ZeroPrd.GetNextRollProbability(), 0.0f);
    TestFalse(TEXT("0 概率 Roll 恒为 false"), ZeroPrd.Roll(Stream));

    FPseudoRandomDistribution FullPrd(1.0f);
    TestEqual(TEXT("满概率常数 C 为 1"), FullPrd.GetIncrementConstant(), 1.0f);
    TestEqual(TEXT("满概率下一击概率为 1"), FullPrd.GetNextRollProbability(), 1.0f);
    TestTrue(TEXT("满概率 Roll 恒为 true"), FullPrd.Roll(Stream));

    FPseudoRandomDistribution CritPrd(0.25f);
    TestEqual(TEXT("标称概率为 0.25"), CritPrd.GetNominalProbability(), 0.25f);
    TestTrue(TEXT("0.25 标称概率对应 C 接近 0.084744"), FMath::IsNearlyEqual(CritPrd.GetIncrementConstant(), 0.084744f, 0.001f));

    const int32 TheoreticalMaxFailures = FMath::CeilToInt(1.0f / CritPrd.GetIncrementConstant()) - 1;
    int32 ObservedMaxFailureStreak = 0;
    int32 TotalHits = 0;
    constexpr int32 TotalTrials = 50000;

    for (int32 Index = 0; Index < TotalTrials; ++Index) {
        if (CritPrd.Roll(Stream)) {
            ++TotalHits;
            TestEqual(TEXT("命中后连续失败次数立即归零"), CritPrd.GetFailureStreak(), 0);
        } else {
            ObservedMaxFailureStreak = FMath::Max(ObservedMaxFailureStreak, CritPrd.GetFailureStreak());
        }
    }

    TestTrue(TEXT("连续未命中次数严格不超过理论硬保底上限"), ObservedMaxFailureStreak <= TheoreticalMaxFailures);
    const float ObservedRate = static_cast<float>(TotalHits) / static_cast<float>(TotalTrials);
    TestTrue(TEXT("50000 次 PRD 总体触发率精确收敛于 0.25 ± 0.01"), FMath::IsWithinInclusive(ObservedRate, 0.24f, 0.26f));

    CritPrd.Reset();
    TestEqual(TEXT("Reset 清零连续失败计数"), CritPrd.GetFailureStreak(), 0);

    FPseudoRandomDistribution RawPrd;
    if (FProperty* Prop = FPseudoRandomDistribution::StaticStruct()->FindPropertyByName(TEXT("NominalProbability"))) {
        const float Val = 0.20f;
        Prop->SetValue_InContainer(&RawPrd, &Val);
    }
    TestTrue(TEXT("未初始化常数的 PRD 具备自动求解保底能力"), RawPrd.GetNextRollProbability() > 0.0f);
    TestTrue(TEXT("求解后常数写回缓存"), RawPrd.GetIncrementConstant() > 0.0f);
    const float CachedC = RawPrd.GetIncrementConstant();
    TestEqual(TEXT("后续调用保持缓存一致"), RawPrd.GetIncrementConstant(), CachedC);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShuffleBagAndWeightedPickerTest,
    "Game.Core.Random.ShuffleBagAndWeightedPicker",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 TShuffleBag 周期内精确无放回配额与 TWeightedPicker O(1) 别名表概率分布一致性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FShuffleBagAndWeightedPickerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameRandomStream Stream(20261106ULL);

    TShuffleBag<FName> Bag;
    Bag.Reserve(0);
    Bag.Reserve(6);
    TestTrue(TEXT("初始洗牌袋为空"), Bag.IsEmpty());
    TestNull(TEXT("空袋 TryDraw 返回 nullptr"), Bag.TryDraw(Stream));

    Bag.Add(TEXT("Ignored"), 0);
    TestTrue(TEXT("添加 0 份不改变空状态"), Bag.IsEmpty());

    Bag.Add(TEXT("Common"), 3);
    Bag.Add(TEXT("Rare"), 2);
    Bag.Emplace(TEXT("Epic"));
    TestEqual(TEXT("袋总容量为 6"), Bag.Num(), 6);
    TestEqual(TEXT("初始剩余可抽数为 6"), Bag.Remaining(), 6);
    TestEqual(TEXT("GetItems 获取数组大小为 6"), Bag.GetItems().Num(), 6);

    for (int32 Cycle = 0; Cycle < 5; ++Cycle) {
        TMap<FName, int32> CycleCounts;
        for (int32 Step = 0; Step < 6; ++Step) {
            const FName Picked = Bag.Draw(Stream);
            ++CycleCounts.FindOrAdd(Picked);
        }
        TestEqual(TEXT("每轮周期结束时剩余数为 0"), Bag.Remaining(), 0);
        TestEqual(TEXT("每轮周期 Common 精确出现 3 次"), CycleCounts.FindRef(TEXT("Common")), 3);
        TestEqual(TEXT("每轮周期 Rare 精确出现 2 次"), CycleCounts.FindRef(TEXT("Rare")), 2);
        TestEqual(TEXT("每轮周期 Epic 精确出现 1 次"), CycleCounts.FindRef(TEXT("Epic")), 1);
    }

    [[maybe_unused]] const FName DrawnOnce = Bag.DrawValue(Stream);
    TestFalse(TEXT("DrawValue 抽取得到有效项"), DrawnOnce.IsNone());
    TestEqual(TEXT("自动重洗抽取 1 次后剩余 5"), Bag.Remaining(), 5);
    Bag.Reshuffle();
    TestEqual(TEXT("手动 Reshuffle 恢复剩余 6"), Bag.Remaining(), 6);
    Bag.Clear();
    TestTrue(TEXT("Clear 后袋为空"), Bag.IsEmpty());

    TWeightedPicker<int32> Picker;
    Picker.Reserve(0);
    Picker.Reserve(4);
    Picker.Build();
    TestTrue(TEXT("空权重抽取器为空"), Picker.IsEmpty());
    TestNull(TEXT("空抽取器 TryPick 返回 nullptr"), Picker.TryPick(Stream));
    TestFalse(TEXT("零权重条目被拒绝"), Picker.AddEntry(1, 0.0));
    TestFalse(TEXT("负权重条目被拒绝"), Picker.AddEntry(1, -5.0));

    TestTrue(TEXT("添加权重 10 成功"), Picker.AddEntry(100, 10.0));
    TestEqual(TEXT("单条目抽取必定返回该条目"), Picker.Pick(Stream), 100);

    TestTrue(TEXT("添加权重 30 成功"), Picker.AddEntry(200, 30.0));
    TestTrue(TEXT("添加权重 60 成功"), Picker.AddEntry(300, 60.0));
    TestEqual(TEXT("候选条目数为 3"), Picker.Num(), 3);
    TestEqual(TEXT("总权重等于 100"), Picker.GetTotalWeight(), 100.0);
    TestEqual(TEXT("条目 0 权重为 10.0"), Picker.GetWeight(0), 10.0);
    TestEqual(TEXT("条目 1 权重为 30.0"), Picker.GetWeight(1), 30.0);
    TestEqual(TEXT("条目 2 权重为 60.0"), Picker.GetWeight(2), 60.0);
    TestEqual(TEXT("非法索引返回 0.0"), Picker.GetWeight(99), 0.0);

    TMap<int32, int32> PickCounts;
    constexpr int32 TotalSamples = 30000;
    for (int32 Index = 0; Index < TotalSamples; ++Index) {
        ++PickCounts.FindOrAdd(Picker.Pick(Stream));
    }

    const double Ratio100 = static_cast<double>(PickCounts.FindRef(100)) / TotalSamples;
    const double Ratio200 = static_cast<double>(PickCounts.FindRef(200)) / TotalSamples;
    const double Ratio300 = static_cast<double>(PickCounts.FindRef(300)) / TotalSamples;
    TestTrue(TEXT("权重 10% 抽中率处于 [0.085, 0.115]"), FMath::IsWithinInclusive(Ratio100, 0.085, 0.115));
    TestTrue(TEXT("权重 30% 抽中率处于 [0.285, 0.315]"), FMath::IsWithinInclusive(Ratio200, 0.285, 0.315));
    TestTrue(TEXT("权重 60% 抽中率处于 [0.585, 0.615]"), FMath::IsWithinInclusive(Ratio300, 0.585, 0.615));

    const int32 PickedVal = Picker.PickValue(Stream);
    TestTrue(TEXT("PickValue 安全值拷贝有效"), PickedVal == 100 || PickedVal == 200 || PickedVal == 300);

    Picker.Clear();
    TestTrue(TEXT("Clear 后抽取器为空"), Picker.IsEmpty());
    TestEqual(TEXT("Clear 后总权重归零"), Picker.GetTotalWeight(), 0.0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FRandomSubsystemIsolationAndArchiveTest,
    "Game.Core.Random.SubsystemIsolationAndArchive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 URandomSubsystem 多通道零串扰隔离、归档快照导出导入与损坏归档原子回滚保护
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FRandomSubsystemIsolationAndArchiveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestNull(TEXT("空上下文 GetPtr 返回 nullptr"), URandomSubsystem::GetPtr(nullptr));

    UWorld* World = FindTestWorldForRandom();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        return false;
    }

    URandomSubsystem& Subsystem = URandomSubsystem::Get(World);
    Subsystem.ClearAllStreams();
    TestEqual(TEXT("清空后通道数为 0"), Subsystem.GetStreamCount(), 0);

    FGameRandomStream& CombatRef = Subsystem.InitializeStream(TEXT("Combat"), 77777ULL);
    const uint32 FirstCombatVal = CombatRef.NextUInt32();

    Subsystem.InitializeStream(TEXT("Combat"), 77777ULL);
    FGameRandomStream& CosmeticStream = Subsystem.GetOrCreateStream(TEXT("Cosmetic"));
    for (int32 Index = 0; Index < 500; ++Index) {
        [[maybe_unused]] const uint32 CosmeticDiscard = CosmeticStream.NextUInt32();
    }
    TestEqual(
        TEXT("高频消耗 Cosmetic 通道绝不污染 Combat 通道序列"),
        Subsystem.GetOrCreateStream(TEXT("Combat")).NextUInt32(),
        FirstCombatVal
    );

    FGameRandomStream& DefaultByNone = Subsystem.GetOrCreateStream(NAME_None);
    FGameRandomStream& DefaultByName = Subsystem.GetOrCreateStream(TEXT("Default"));
    TestTrue(TEXT("NAME_None 自动归一化映射到 Default 通道"), &DefaultByNone == &DefaultByName);
    TestTrue(TEXT("HasStream(Combat) 为真"), Subsystem.HasStream(TEXT("Combat")));

    TArray<FName> ActiveChannelNames;
    Subsystem.GetChannelNames(ActiveChannelNames);
    TestTrue(TEXT("GetChannelNames 包含 Combat"), ActiveChannelNames.Contains(TEXT("Combat")));
    TestTrue(TEXT("GetChannelNames 包含 Cosmetic"), ActiveChannelNames.Contains(TEXT("Cosmetic")));
    TestTrue(TEXT("GetChannelNames 包含 Default"), ActiveChannelNames.Contains(TEXT("Default")));

    TArray<uint8> SnapshotBytes;
    {
        FMemoryWriter Writer(SnapshotBytes);
        Subsystem.ExportStreams(Writer);
    }

    const uint32 ExpectedNextCombat = Subsystem.GetOrCreateStream(TEXT("Combat")).NextUInt32();
    const uint32 ExpectedNextCosmetic = Subsystem.GetOrCreateStream(TEXT("Cosmetic")).NextUInt32();

    Subsystem.ClearAllStreams();
    TestEqual(TEXT("清空后通道数为 0"), Subsystem.GetStreamCount(), 0);

    {
        FMemoryReader Reader(SnapshotBytes);
        TestTrue(TEXT("合法二进制快照导入成功"), Subsystem.ImportStreams(Reader));
    }
    TestEqual(TEXT("导入后通道数恢复为 3"), Subsystem.GetStreamCount(), 3);
    TestEqual(TEXT("导入后 Combat 通道后续序列精确复现"), Subsystem.GetOrCreateStream(TEXT("Combat")).NextUInt32(), ExpectedNextCombat);
    TestEqual(TEXT("导入后 Cosmetic 通道后续序列精确复现"), Subsystem.GetOrCreateStream(TEXT("Cosmetic")).NextUInt32(), ExpectedNextCosmetic);

    const uint64 GuardStateBeforeCorrupt = Subsystem.GetOrCreateStream(TEXT("Combat")).GetState();
    {
        TArray<uint8> BadMagicBytes;
        FMemoryWriter Writer(BadMagicBytes);
        uint32 BadMagic = 0xDEADBEEFu;
        uint32 Ver = URandomSubsystem::ARCHIVE_VERSION;
        int32 Count = 0;
        Writer << BadMagic;
        Writer << Ver;
        Writer << Count;
        FMemoryReader BadReader(BadMagicBytes);
        TestFalse(TEXT("错误魔数归档被拒绝"), Subsystem.ImportStreams(BadReader));
    }
    {
        TArray<uint8> ExcessiveCountBytes;
        FMemoryWriter Writer(ExcessiveCountBytes);
        uint32 Magic = URandomSubsystem::ARCHIVE_MAGIC;
        uint32 Ver = URandomSubsystem::ARCHIVE_VERSION;
        int32 Count = URandomSubsystem::MAX_IMPORT_STREAMS + 1;
        Writer << Magic;
        Writer << Ver;
        Writer << Count;
        FMemoryReader BadReader(ExcessiveCountBytes);
        TestFalse(TEXT("超上限通道数量归档被拦截防 OOM"), Subsystem.ImportStreams(BadReader));
    }
    {
        TArray<uint8> TruncatedBytes;
        FMemoryWriter Writer(TruncatedBytes);
        uint32 Magic = URandomSubsystem::ARCHIVE_MAGIC;
        uint32 Ver = URandomSubsystem::ARCHIVE_VERSION;
        int32 Count = 2;
        Writer << Magic;
        Writer << Ver;
        Writer << Count;
        FMemoryReader BadReader(TruncatedBytes);
        TestFalse(TEXT("截断残缺归档被拦截"), Subsystem.ImportStreams(BadReader));
    }
    TestEqual(
        TEXT("损坏归档导入失败后既有通道状态保持 100% 原子无损"),
        Subsystem.GetOrCreateStream(TEXT("Combat")).GetState(),
        GuardStateBeforeCorrupt
    );

    TestTrue(TEXT("RemoveStream 移除已存在通道返回 true"), Subsystem.RemoveStream(TEXT("Cosmetic")));
    TestFalse(TEXT("RemoveStream 重复移除返回 false"), Subsystem.RemoveStream(TEXT("Cosmetic")));

    TSharedRef<FGameRandomStream> SharedStream = Subsystem.GetOrCreateStreamShared(TEXT("SharedChannel"));
    const uint32 Val1 = SharedStream->NextUInt32();
    TestTrue(TEXT("子系统包含 SharedChannel"), Subsystem.HasStream(TEXT("SharedChannel")));
    Subsystem.RemoveStream(TEXT("SharedChannel"));
    TestFalse(TEXT("移除后子系统不包含 SharedChannel"), Subsystem.HasStream(TEXT("SharedChannel")));
    const uint32 Val2 = SharedStream->NextUInt32();
    TestNotEqual(TEXT("持有共享引用在通道移除后仍可安全继续推进状态且内存有效"), Val2, Val1);

    Subsystem.ClearAllStreams();

    return true;
}

#endif
