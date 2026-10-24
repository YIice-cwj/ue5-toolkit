/**
 * @file UGameStatPerformanceTest.cpp
 * @brief GameGameplayCore 数值组件查询吞吐基准，Shipping 构建自动剔除
 */

#include "Combat/UGameStatComponent.h"

#if WITH_AUTOMATION_TESTS

#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 PerfStatCount = 64;
constexpr int32 PerfStatIterations = 200000;
constexpr double PerfStatBudgetNsPerOp = 2000.0;

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameStatLookupPerfTest, "Game.GameplayCore.Perf.StatLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 属性查询吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且查询结果正确返回 true
 *
 * 实现思路：注册 64 个属性并连续按 Tag 查询当前值，按总耗时折算单次纳秒；
 * 预算 2000ns/op 仅拦截数量级退化（正常为百纳秒以内）。
 */
bool FGameStatLookupPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameStatComponent* Component = NewObject<UGameStatComponent>();

    TArray<FGameplayTag> Tags;
    Tags.Reserve(PerfStatCount);
    for (int32 Index = 0; Index < PerfStatCount; ++Index) {
        const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(
            FName(*FString::Printf(TEXT("Perf.Attribute.%d"), Index)));
        Tags.Add(Tag);
        Component->RegisterStat(Tag, 100.0f, 100.0f, 0.0f);
    }

    int32 HitCount = 0;
    volatile float Accumulated = 0.0f;
    const double StartSeconds = FPlatformTime::Seconds();
    for (int32 Index = 0; Index < PerfStatIterations; ++Index) {
        const float Value = Component->GetStatCurrent(Tags[Index % PerfStatCount]);
        Accumulated = Accumulated + Value;
        if (FMath::IsNearlyEqual(Value, 100.0f)) {
            ++HitCount;
        }
    }
    const double ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
    const double NsPerOp = (ElapsedSeconds * 1.0e9) / static_cast<double>(PerfStatIterations);

    TestEqual(TEXT("全部查询均应命中"), HitCount, PerfStatIterations);
    TestTrue(
        FString::Printf(TEXT("属性查询 %.1f ns/op 应低于预算 %.0f ns/op"), NsPerOp, PerfStatBudgetNsPerOp),
        NsPerOp < PerfStatBudgetNsPerOp);

    AddInfo(FString::Printf(TEXT("StatLookup: %.1f ns/op (%d ops)"), NsPerOp, PerfStatIterations));
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
