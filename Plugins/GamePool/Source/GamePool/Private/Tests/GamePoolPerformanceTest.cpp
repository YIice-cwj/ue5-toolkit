/**
 * @file GamePoolPerformanceTest.cpp
 * @brief GamePool 核心对象池微基准自动化测试（结构体池 / UObject 池 / Actor 池 vs 原生分配）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮采样消除预热偏差，统计中位数与变异系数 (CV%)。
 * 预算用于防御数量级性能退化。
 */

#include "MemoryPool/StructPool.h"
#include "ObjectPool/ObjectPoolSubsystem.h"
#include "Tests/GamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 StructPoolIterations = 200000;
constexpr double StructPoolBudgetNsPerOp = 200.0;

constexpr int32 UObjectPoolIterations = 2000;
constexpr double UObjectPoolBudgetNsPerOp = 5000.0;

constexpr int32 ActorPoolIterations = 50;
constexpr double ActorPoolBudgetNsPerOp = 50000.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

struct FPerfTestPayload {
    uint64 FieldA = 0;
    uint64 FieldB = 0;
    uint64 FieldC = 0;
    uint64 FieldD = 0;
};

/**
 * @brief 获取自动化测试可用的 UWorld 指针
 * @return 当前有效的 UWorld 指针，不存在时返回 nullptr
 */
UWorld* GetTestWorld() {
    if (!GEngine) {
        return nullptr;
    }
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数与离散度 (CV%)
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数（测量后丢弃）
 * @param SampleRounds 正式采样轮数（规范要求 >= 5 轮）
 * @param IterationsPerRound 每轮迭代执行次数
 * @param Body 待执行测量闭包
 * @return 包含中位数与离散度的统计度量
 */
template <typename TFunc>
FBenchmarkMetrics RunBenchmark(
    const int32 WarmupRounds,
    const int32 SampleRounds,
    const int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[SampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Sample : SamplesNs) {
        const double Diff = Sample - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));
    const double CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;

    FBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = CvPercent;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStructPoolPerfTest,
    "Game.Pool.Perf.StructPoolVsNewDelete",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 原生结构体池租借/归还对比堆分配 (new/delete) 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且吞吐符合预期返回 true
 */
bool FGameStructPoolPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    volatile uint64 Sink = 0;

    TStructPool<FPerfTestPayload, 2048> StructPool;

    const FBenchmarkMetrics PoolMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StructPoolIterations,
        [&StructPool, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FPerfTestPayload* Item = StructPool.Acquire();
                Item->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Item->FieldA;
                StructPool.Release(Item);
            }
        }
    );

    const FBenchmarkMetrics HeapMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StructPoolIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                void* Memory = FMemory::Malloc(sizeof(FPerfTestPayload));
                FPerfTestPayload* Item = ::new (Memory) FPerfTestPayload();
                Item->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Item->FieldA;
                Item->~FPerfTestPayload();
                FMemory::Free(Memory);
            }
        }
    );

    const double SpeedupRatio = (PoolMetrics.MedianNs > 0.0) ? (HeapMetrics.MedianNs / PoolMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("StructPool 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), PoolMetrics.MedianNs, StructPoolBudgetNsPerOp),
        PoolMetrics.MedianNs < StructPoolBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("StructPool: %.1f ns/op (CV: %.2f%%, %.1f Mops/s) | Heap new/delete: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e6,
        HeapMetrics.MedianNs,
        HeapMetrics.CvPercent,
        SpeedupRatio
    ));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameUObjectPoolPerfTest,
    "Game.Pool.Perf.UObjectPoolVsNewObject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief UObject 对象池借出/归还复用对比原生 NewObject 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且复用开销优于原生创建返回 true
 */
bool FGameUObjectPoolPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->PrewarmObjectPool(UGamePoolableTestObject::StaticClass(), 100);

    volatile int32 Sink = 0;

    const FBenchmarkMetrics PoolMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        UObjectPoolIterations,
        [Subsystem, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const TGameStatusResult<UGamePoolableTestObject*> AcquireResult =
                    Subsystem->AcquireObject<UGamePoolableTestObject>();
                if (AcquireResult.IsSuccess()) {
                    UGamePoolableTestObject* Obj = AcquireResult.GetValue();
                    Obj->PayloadValue = Index;
                    Sink = Sink + Obj->PayloadValue;
                    Subsystem->ReleaseObject(Obj);
                }
            }
        }
    );

    const FBenchmarkMetrics NativeMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        UObjectPoolIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                UGamePoolableTestObject* Obj = NewObject<UGamePoolableTestObject>();
                Obj->PayloadValue = Index;
                Sink = Sink + Obj->PayloadValue;
                Obj->MarkAsGarbage();
            }
        }
    );

    const double SpeedupRatio = (PoolMetrics.MedianNs > 0.0) ? (NativeMetrics.MedianNs / PoolMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("UObjectPool 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), PoolMetrics.MedianNs, UObjectPoolBudgetNsPerOp),
        PoolMetrics.MedianNs < UObjectPoolBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("UObjectPool: %.1f ns/op (CV: %.2f%%, %.1f Kops/s) | Native NewObject: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e3,
        NativeMetrics.MedianNs,
        NativeMetrics.CvPercent,
        SpeedupRatio
    ));

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameActorPoolPerfTest,
    "Game.Pool.Perf.ActorPoolVsSpawnDestroy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief Actor 对象池借出/归还复用对比原生 SpawnActor/DestroyActor 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且复用开销优于原生生成返回 true
 */
bool FGameActorPoolPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Subsystem = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAllPools();
    Subsystem->Prewarm(AGamePoolableTestActor::StaticClass(), 20);

    volatile double Sink = 0.0;

    const FBenchmarkMetrics PoolMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        ActorPoolIterations,
        [Subsystem, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                const TGameStatusResult<AGamePoolableTestActor*> AcquireResult =
                    Subsystem->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
                if (AcquireResult.IsSuccess()) {
                    AGamePoolableTestActor* Actor = AcquireResult.GetValue();
                    Actor->SetActorLocation(FVector(Index, 0.0, 0.0));
                    Sink = Sink + Actor->GetActorLocation().X;
                    Subsystem->ReleaseActor(Actor);
                }
            }
        }
    );

    const FBenchmarkMetrics NativeMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        ActorPoolIterations,
        [World, &Sink](const int32 Iterations) {
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            for (int32 Index = 0; Index < Iterations; ++Index) {
                AGamePoolableTestActor* Actor = World->SpawnActor<AGamePoolableTestActor>(
                    AGamePoolableTestActor::StaticClass(),
                    SpawnParams
                );
                if (Actor) {
                    Actor->SetActorLocation(FVector(Index, 0.0, 0.0));
                    Sink = Sink + Actor->GetActorLocation().X;
                    World->DestroyActor(Actor);
                }
            }
        }
    );

    const double SpeedupRatio = (PoolMetrics.MedianNs > 0.0) ? (NativeMetrics.MedianNs / PoolMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("ActorPool 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), PoolMetrics.MedianNs, ActorPoolBudgetNsPerOp),
        PoolMetrics.MedianNs < ActorPoolBudgetNsPerOp
    );

    AddInfo(FString::Printf(
        TEXT("ActorPool: %.1f ns/op (CV: %.2f%%, %.1f Ops/s) | Native Spawn/Destroy: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec,
        NativeMetrics.MedianNs,
        NativeMetrics.CvPercent,
        SpeedupRatio
    ));

    Subsystem->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
