/**
 * @file UGameGameplayPerformanceTest.cpp
 * @brief GameGameplay 关键链路微基准性能测试套件（通道查询 / 修饰器演算 / 上下文网络打包 / ExecCalc 管线）
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Ability/UGameAbilitySystemComponent.h"
#include "Ability/UGameAbilitySystemBootstrap.h"
#include "AbilitySystemTestAttributeSet.h"
#include "Core/IGamePoolable.h"
#include "Damage/FDamageChannelBinding.h"
#include "Damage/FDamageModifier.h"
#include "Damage/FGameDamageChannelRegistry.h"
#include "Damage/FGameGameplayEffectContext.h"
#include "Damage/UGameDamageExecution.h"
#include "GameplayEffect.h"
#include "GameplayTagsManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace {

class FTestDamagePerfExecutionProxy : public UGameDamageExecution {
public:
    static void TestRegisterChannelBinding(
        UGameDamageExecution* Target,
        const FGameplayTag& ChannelTag,
        const FGameplayAttribute& TargetAttribute,
        EGameplayModOp::Type ModOp = EGameplayModOp::Additive
    ) {
        static_cast<FTestDamagePerfExecutionProxy*>(Target)->RegisterChannelBinding(ChannelTag, TargetAttribute, ModOp);
    }
};

}  // namespace

/**
 * @brief 通道注册表查询性能基准测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayChannelRegistryPerfTest,
    "Game.Gameplay.Perf.ChannelRegistryLookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行通道注册表读锁热路径查询性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayChannelRegistryPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag TagPhys = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Phys")));
    const FGameplayTag TagFire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.Fire")));
    const FGameplayTag TagTrue = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Perf.True")));

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    Registry.UnregisterAll(ExecClass);
    Registry.Register(ExecClass, FDamageChannelBinding(TagPhys, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(TagFire, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(TagTrue, HealthAttr));

    constexpr int32 WarmupIterations = 5000;
    volatile int32 WarmupSink = 0;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        Registry.ForEachBinding(ExecClass, [&WarmupSink](TConstArrayView<FDamageChannelBinding> Bindings) {
            WarmupSink = Bindings.Num();
        });
    }
    (void)WarmupSink;

    constexpr int32 Iterations = 100000;
    int32 TotalBindingCount = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Registry.ForEachBinding(ExecClass, [&TotalBindingCount](TConstArrayView<FDamageChannelBinding> Bindings) {
            TotalBindingCount += Bindings.Num();
        });
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedNs = FPlatformTime::ToSeconds64(ElapsedCycles) * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / (ElapsedNs / 1e9)) / 1e6;

    TestEqual(TEXT("累计绑定数符合预期"), TotalBindingCount, Iterations * 3);

    AddInfo(FString::Printf(
        TEXT("【性能指标】ChannelRegistryLookup: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations, ElapsedNs / 1e6, PerOpNs, Mops
    ));

    constexpr double BudgetPerOpNs = 150.0;
    TestTrue(
        FString::Printf(TEXT("单次查询耗时需低于预算门禁 %.1f ns (实际: %.2f ns)"), BudgetPerOpNs, PerOpNs),
        PerOpNs < BudgetPerOpNs
    );

    Registry.UnregisterAll(ExecClass);
    return true;
}

/**
 * @brief 伤害修饰器纯数值演算性能基准测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayDamageModifierPerfTest,
    "Game.Gameplay.Perf.DamageModifierEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行伤害修饰器复合运算性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayDamageModifierPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FDamageModifier Modifiers[4] = {
        FDamageModifier(1.25f, 15.0f),
        FDamageModifier(0.80f, -10.0f),
        FDamageModifier::MakeOverride(999.0f),
        FDamageModifier(1.0f, 0.0f)
    };

    constexpr int32 WarmupIterations = 10000;
    volatile float WarmupSink = 0.0f;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        WarmupSink = Modifiers[Index % 4].Evaluate(100.0f);
    }
    (void)WarmupSink;

    constexpr int32 Iterations = 1000000;
    float AccumulatedDamage = 0.0f;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        const float BaseDmg = 50.0f + static_cast<float>(Index & 0x7F);
        AccumulatedDamage += Modifiers[Index & 3].Evaluate(BaseDmg);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedNs = FPlatformTime::ToSeconds64(ElapsedCycles) * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / (ElapsedNs / 1e9)) / 1e6;

    TestTrue(TEXT("累加演算值大于零"), AccumulatedDamage > 0.0f);

    AddInfo(FString::Printf(
        TEXT("【性能指标】DamageModifierEvaluation: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations, ElapsedNs / 1e6, PerOpNs, Mops
    ));

    constexpr double BudgetPerOpNs = 15.0;
    TestTrue(
        FString::Printf(TEXT("单次修饰计算耗时需低于预算门禁 %.1f ns (实际: %.2f ns)"), BudgetPerOpNs, PerOpNs),
        PerOpNs < BudgetPerOpNs
    );

    return true;
}

/**
 * @brief 打击反馈上下文网络序列化性能基准测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayEffectContextNetPerfTest,
    "Game.Gameplay.Perf.EffectContextNetSerialize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行打击反馈上下文全量往返序列化性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayEffectContextNetPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameplayTag TagPhys = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.NetPerf.Phys")));
    const FGameplayTag TagFire = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.NetPerf.Fire")));
    const FGameplayTag TagCrit = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.NetPerf.Crit")));
    const FGameplayTag TagHitStop = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Dynamic.NetPerf.HitStop")));

    FGameGameplayEffectContext SourceContext;
    SourceContext.SetFeedbackEventId(42);
    SourceContext.SetActualDamage(150.0f);
    SourceContext.SetActualChannelDamage(TagPhys, 100.0f);
    SourceContext.SetActualChannelDamage(TagFire, 50.0f);
    SourceContext.AddFeedbackTag(TagCrit);
    SourceContext.SetDynamicValue(TagHitStop, 0.12f);

    constexpr int32 WarmupIterations = 1000;
    TArray<uint8> WarmupBuffer;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        WarmupBuffer.Reset();
        FMemoryWriter Writer(WarmupBuffer);
        bool bWriteSuccess = true;
        SourceContext.NetSerialize(Writer, nullptr, bWriteSuccess);

        FMemoryReader Reader(WarmupBuffer);
        FGameGameplayEffectContext DestContext;
        bool bReadSuccess = true;
        DestContext.NetSerialize(Reader, nullptr, bReadSuccess);
    }

    constexpr int32 Iterations = 20000;
    TArray<uint8> Buffer;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        Buffer.Reset();
        FMemoryWriter Writer(Buffer);
        bool bWriteSuccess = true;
        SourceContext.NetSerialize(Writer, nullptr, bWriteSuccess);

        FMemoryReader Reader(Buffer);
        FGameGameplayEffectContext DestContext;
        bool bReadSuccess = true;
        DestContext.NetSerialize(Reader, nullptr, bReadSuccess);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedNs = FPlatformTime::ToSeconds64(ElapsedCycles) * 1e9;
    const double PerOpUs = (ElapsedNs / 1000.0) / static_cast<double>(Iterations);
    const double OpsPerSec = static_cast<double>(Iterations) / (ElapsedNs / 1e9);

    AddInfo(FString::Printf(
        TEXT("【性能指标】EffectContextNetSerialize: 迭代=%d 次, 总耗时=%.2f ms, 往返均耗=%.2f μs, 吞吐=%.0f ops/s, 字节数=%d"),
        Iterations, ElapsedNs / 1e6, PerOpUs, OpsPerSec, Buffer.Num()
    ));

    constexpr double BudgetPerOpUs = 10.0;
    TestTrue(
        FString::Printf(TEXT("单次网络往返耗时需低于预算门禁 %.1f μs (实际: %.2f μs)"), BudgetPerOpUs, PerOpUs),
        PerOpUs < BudgetPerOpUs
    );

    return true;
}

/**
 * @brief 完整伤害结算执行管线微基准测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayDamageExecutionPipelinePerfTest,
    "Game.Gameplay.Perf.DamageExecutionPipeline",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行完整 ExecCalc 伤害管线性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayDamageExecutionPipelinePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    AActor* SourceActor = NewObject<AActor>(GetTransientPackage());
    AActor* TargetActor = NewObject<AActor>(GetTransientPackage());

    UGameAbilitySystemComponent* SourceASC = NewObject<UGameAbilitySystemComponent>(SourceActor);
    SourceASC->InitAbilityActorInfo(SourceActor, SourceActor);

    UGameAbilitySystemComponent* TargetASC = NewObject<UGameAbilitySystemComponent>(TargetActor);
    TargetASC->InitAbilityActorInfo(TargetActor, TargetActor);

    UAbilitySystemTestAttributeSet* TargetAttrSet = NewObject<UAbilitySystemTestAttributeSet>(TargetASC);
    TargetASC->AddAttributeSetSubobject(TargetAttrSet);

    const FGameplayAttribute HealthAttr(FindFieldChecked<FProperty>(
        UAbilitySystemTestAttributeSet::StaticClass(),
        GET_MEMBER_NAME_CHECKED(UAbilitySystemTestAttributeSet, Health)
    ));
    TargetASC->SetNumericAttributeBase(HealthAttr, 100000.0f);

    const FGameplayTag PhysTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.PipePerf.Phys")));
    const FGameplayTag FireTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.PipePerf.Fire")));

    UClass* ExecClass = UGameDamageExecution::StaticClass();
    FGameDamageChannelRegistry& Registry = FGameDamageChannelRegistry::Get();
    Registry.UnregisterAll(ExecClass);
    Registry.Register(ExecClass, FDamageChannelBinding(PhysTag, HealthAttr));
    Registry.Register(ExecClass, FDamageChannelBinding(FireTag, HealthAttr));

    UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
    Effect->DurationPolicy = EGameplayEffectDurationType::Instant;

    FGameplayEffectContextHandle ContextHandle(new FGameGameplayEffectContext());
    ContextHandle.AddInstigator(SourceActor, SourceActor);

    FGameplayEffectSpec Spec(Effect, ContextHandle, 1.0f);
    Spec.SetSetByCallerMagnitude(PhysTag, 40.0f);
    Spec.SetSetByCallerMagnitude(FireTag, 25.0f);

    FGameplayEffectCustomExecutionParameters ExecParams(
        Spec,
        TArray<FGameplayEffectExecutionScopedModifierInfo>(),
        TargetASC,
        FGameplayTagContainer(),
        FPredictionKey()
    );

    const UGameDamageExecution* ExecCalc = GetDefault<UGameDamageExecution>();

    constexpr int32 WarmupIterations = 1000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        FGameplayEffectCustomExecutionOutput Output;
        ExecCalc->Execute_Implementation(ExecParams, Output);
    }

    constexpr int32 Iterations = 10000;
    int32 OutputSum = 0;
    TArray<FGameplayModifierEvaluatedData> Modifiers;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        FGameplayEffectCustomExecutionOutput Output;
        ExecCalc->Execute_Implementation(ExecParams, Output);
        Modifiers.Reset();
        Output.GetOutputModifiers(Modifiers);
        OutputSum += Modifiers.Num();
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedNs = FPlatformTime::ToSeconds64(ElapsedCycles) * 1e9;
    const double PerOpUs = (ElapsedNs / 1000.0) / static_cast<double>(Iterations);
    const double OpsPerSec = static_cast<double>(Iterations) / (ElapsedNs / 1e9);

    TestTrue(TEXT("结算修饰器输出数量正确"), OutputSum >= Iterations * 2);

    AddInfo(FString::Printf(
        TEXT("【性能指标】DamageExecutionPipeline: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f μs, 吞吐=%.0f ops/s"),
        Iterations, ElapsedNs / 1e6, PerOpUs, OpsPerSec
    ));

    constexpr double BudgetPerOpUs = 15.0;
    TestTrue(
        FString::Printf(TEXT("单次结算全管线耗时需低于预算门禁 %.1f μs (实际: %.2f μs)"), BudgetPerOpUs, PerOpUs),
        PerOpUs < BudgetPerOpUs
    );

    Registry.UnregisterAll(ExecClass);
    return true;
}

/**
 * @brief Bootstrap 组件对象池生命周期重置微基准测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameGameplayBootstrapPoolablePerfTest,
    "Game.Gameplay.Perf.BootstrapPoolableReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行 Bootstrap 接入 IGamePoolable 的出入池状态重置性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameGameplayBootstrapPoolablePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    IGamePoolable* Poolable = Cast<IGamePoolable>(Bootstrap);
    TestNotNull(TEXT("Bootstrap 成功转换为 IGamePoolable 接口"), Poolable);
    if (Poolable == nullptr) {
        return false;
    }

    constexpr int32 WarmupIterations = 1000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        IGamePoolable::Execute_OnReleaseToPool(Bootstrap);
        IGamePoolable::Execute_OnAcquireFromPool(Bootstrap);
    }

    constexpr int32 Iterations = 50000;
    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        IGamePoolable::Execute_OnReleaseToPool(Bootstrap);
        IGamePoolable::Execute_OnAcquireFromPool(Bootstrap);
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedNs = FPlatformTime::ToSeconds64(ElapsedCycles) * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Mops = (static_cast<double>(Iterations) / (ElapsedNs / 1e9)) / 1e6;

    TestFalse(TEXT("入池后标记已重置"), Bootstrap->IsDefaultAbilitiesGranted());

    AddInfo(FString::Printf(
        TEXT("【性能指标】BootstrapPoolableReset: 迭代=%d 次, 总耗时=%.2f ms, 往返均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations, ElapsedNs / 1e6, PerOpNs, Mops
    ));

    constexpr double BudgetPerOpNs = 500.0;
    TestTrue(
        FString::Printf(TEXT("出入池单次往返耗时需低于预算门禁 %.1f ns (实际: %.2f ns)"), BudgetPerOpNs, PerOpNs),
        PerOpNs < BudgetPerOpNs
    );

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
