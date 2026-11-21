/**
 * @file GamePoolPerformanceTest.cpp
 * @brief GamePool 全组件微基准自动化测试（结构体池 / UObject 池 / Actor 池 / 组件重置 / 句柄校验 / 延迟归还 / 冷淘汰）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮预热与采样（>=5 轮），统计中位数、均值、Min/Max 与变异系数 (CV%)。
 * 预算用于防御数量级性能退化。
 */

#include "Memory/TStructPool.h"
#include "Subsystem/UObjectPoolSubsystem.h"
#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Logging/GameLogCategories.h"
#include "Misc/AutomationTest.h"
#include "Particles/ParticleSystemComponent.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 StructPoolIterations = 200000;
constexpr double StructPoolBudgetNsPerOp = 200.0;

constexpr int32 UObjectPoolIterations = 2000;
constexpr double UObjectPoolBudgetNsPerOp = 5000.0;

constexpr int32 ActorPoolIterations = 50;
constexpr double ActorPoolBudgetNsPerOp = 50000.0;

constexpr int32 HandleValidationIterations = 100000;
constexpr double HandleValidationBudgetNsPerOp = 50.0;

constexpr int32 DeferredReleaseBatchSize = 512;
constexpr double DeferredReleaseBudgetNsPerOp = 5000.0;

constexpr int32 EvictionBatchSize = 256;
constexpr double EvictionBatchBudgetNsPerOp = 25000.0;

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

/**
 * @brief 执行多轮基准测试，消除预热偏差并统计中位数、均值、极值与离散度 (CV%)
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
 * @brief 原生结构体池租借/归还对比堆分配 (Malloc/Free) 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且吞吐符合预期返回 true
 */
bool FGameStructPoolPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    struct FPerfTestPayload {
        uint64 FieldA = 0;
        uint64 FieldB = 0;
        uint64 FieldC = 0;
        uint64 FieldD = 0;
    };

    volatile uint64 Sink = 0;
    TStructPool<FPerfTestPayload, 2048> StructPool;
    StructPool.ReserveChunks(1);

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

    const FBenchmarkMetrics ScopedMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StructPoolIterations,
        [&StructPool, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                TScopedStructRental<FPerfTestPayload, 2048> Rental = StructPool.AcquireScoped();
                Rental->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Rental->FieldA;
            }
        }
    );

    const FBenchmarkMetrics HeapMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        StructPoolIterations,
        [&Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                FPerfTestPayload* Item = static_cast<FPerfTestPayload*>(FMemory::Malloc(sizeof(FPerfTestPayload)));
                Item->FieldA = static_cast<uint64>(Index);
                Sink = Sink + Item->FieldA;
                FMemory::Free(Item);
            }
        }
    );

    const double SpeedupRatio = (PoolMetrics.MedianNs > 0.0) ? (HeapMetrics.MedianNs / PoolMetrics.MedianNs) : 0.0;

    TestTrue(
        FString::Printf(TEXT("StructPool 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), PoolMetrics.MedianNs, StructPoolBudgetNsPerOp),
        PoolMetrics.MedianNs < StructPoolBudgetNsPerOp
    );
    TestTrue(
        FString::Printf(TEXT("StructPool Scoped RAII 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), ScopedMetrics.MedianNs, StructPoolBudgetNsPerOp),
        ScopedMetrics.MedianNs < StructPoolBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] StructPool Raw: %.2f ns/op (CV: %.2f%%, %.2f Mops/s) | Scoped RAII: %.2f ns/op (CV: %.2f%%) | Heap Malloc/Free: %.2f ns/op | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e6,
        ScopedMetrics.MedianNs,
        ScopedMetrics.CvPercent,
        HeapMetrics.MedianNs,
        SpeedupRatio
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameUObjectPoolPerfTest,
    "Game.Pool.Perf.UObjectPoolVsNewObject",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief UObject 对象池借出/归还（含带指纹载荷与 RAII 租约）对比原生 NewObject 吞吐基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内且复用开销符合预期返回 true
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
                    (void)Subsystem->ReleaseObject(Obj);
                }
            }
        }
    );

    const FBenchmarkMetrics ScopedPayloadMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        UObjectPoolIterations,
        [Subsystem, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                TGameStatusResult<TScopedPoolRental<UGamePoolableTestObject>> RentalRes =
                    Subsystem->AcquireScopedObjectWithPayload<UGamePoolableTestObject>(
                        UGamePoolableTestObject::StaticClass(),
                        Index
                    );
                if (RentalRes.IsSuccess()) {
                    TScopedPoolRental<UGamePoolableTestObject> Rental = RentalRes.ExtractValue();
                    Sink = Sink + Rental->PayloadValue;
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
    TestTrue(
        FString::Printf(TEXT("UObjectPool Scoped+Payload 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), ScopedPayloadMetrics.MedianNs, UObjectPoolBudgetNsPerOp),
        ScopedPayloadMetrics.MedianNs < UObjectPoolBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] UObjectPool: %.1f ns/op (CV: %.2f%%, %.1f Kops/s) | Scoped+Payload: %.1f ns/op (CV: %.2f%%) | Native NewObject: %.1f ns/op | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e3,
        ScopedPayloadMetrics.MedianNs,
        ScopedPayloadMetrics.CvPercent,
        NativeMetrics.MedianNs,
        SpeedupRatio
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

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
                    (void)Subsystem->ReleaseActor(Actor);
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

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] ActorPool: %.1f ns/op (CV: %.2f%%, %.1f Kops/s) | Native Spawn/Destroy: %.1f ns/op (CV: %.2f%%) | Speedup: %.2fx"),
        PoolMetrics.MedianNs,
        PoolMetrics.CvPercent,
        PoolMetrics.OpsPerSec / 1.0e3,
        NativeMetrics.MedianNs,
        NativeMetrics.CvPercent,
        SpeedupRatio
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameActorResetFlagsPerfTest,
    "Game.Pool.Perf.ActorResetFlagsNoneVsAll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 对比 1-pass 单次遍历 EPoolResetFlags::All 与 EPoolResetFlags::None 在多组件 Actor 上的归还耗时
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部基准落在预算内返回 true
 */
bool FGameActorResetFlagsPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Subsystem->ClearAllPools();

    AGamePoolableTestActor* RichActor = Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
    TestNotNull(TEXT("多组件测试 Actor 借出成功"), RichActor);
    if (!RichActor) {
        return false;
    }

    UProjectileMovementComponent* MoveComp = NewObject<UProjectileMovementComponent>(RichActor);
    MoveComp->SetUpdatedComponent(RichActor->GetRootComponent());
    MoveComp->RegisterComponent();

    UAudioComponent* AudioComp = NewObject<UAudioComponent>(RichActor);
    AudioComp->SetupAttachment(RichActor->GetRootComponent());
    AudioComp->RegisterComponent();

    UParticleSystemComponent* FXComp = NewObject<UParticleSystemComponent>(RichActor);
    FXComp->SetupAttachment(RichActor->GetRootComponent());
    FXComp->RegisterComponent();

    USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(RichActor);
    SkelComp->SetupAttachment(RichActor->GetRootComponent());
    SkelComp->RegisterComponent();

    (void)Subsystem->ReleaseActor(RichActor);

    Subsystem->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);
    const FBenchmarkMetrics AllFlagsMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        ActorPoolIterations,
        [Subsystem, ActorClass](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                AGamePoolableTestActor* Actor = Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
                (void)Subsystem->ReleaseActor(Actor);
            }
        }
    );

    Subsystem->SetPoolResetFlags(ActorClass, EPoolResetFlags::None);
    const FBenchmarkMetrics NoneFlagsMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        ActorPoolIterations,
        [Subsystem, ActorClass](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                AGamePoolableTestActor* Actor = Subsystem->AcquireActor<AGamePoolableTestActor>(ActorClass).GetValue();
                (void)Subsystem->ReleaseActor(Actor);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("1-pass ResetFlags::All 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), AllFlagsMetrics.MedianNs, ActorPoolBudgetNsPerOp),
        AllFlagsMetrics.MedianNs < ActorPoolBudgetNsPerOp
    );
    TestTrue(
        FString::Printf(TEXT("ResetFlags::None 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), NoneFlagsMetrics.MedianNs, ActorPoolBudgetNsPerOp),
        NoneFlagsMetrics.MedianNs < ActorPoolBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] Actor 1-Pass Reset All: %.1f ns/op (CV: %.2f%%) | Reset None: %.1f ns/op (CV: %.2f%%)"),
        AllFlagsMetrics.MedianNs,
        AllFlagsMetrics.CvPercent,
        NoneFlagsMetrics.MedianNs,
        NoneFlagsMetrics.CvPercent
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    Subsystem->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameHandleValidationPerfTest,
    "Game.Pool.Perf.HandleValidationVsRaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 世代版本受管安全句柄校验开销对比原生裸指针判空微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 句柄校验开销在预算范围内返回 true
 */
bool FGameHandleValidationPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TGameStatusResult<TPooledObjectHandle<UGamePoolableTestObject>> HandleResult =
        Subsystem->AcquireObjectHandle<UGamePoolableTestObject>();
    TestTrue(TEXT("借出安全句柄成功"), HandleResult.IsSuccess());

    const TPooledObjectHandle<UGamePoolableTestObject> ValidHandle = HandleResult.GetValue();
    UGamePoolableTestObject* RawPtr = ValidHandle.Get();
    TestNotNull(TEXT("底层原生对象指针非空"), RawPtr);

    volatile bool Sink = false;

    const FBenchmarkMetrics HandleMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        HandleValidationIterations,
        [&ValidHandle, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = ValidHandle.IsValid();
            }
        }
    );

    const FBenchmarkMetrics RawMetrics = RunBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        HandleValidationIterations,
        [&RawPtr, &Sink](const int32 Iterations) {
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Sink = (RawPtr != nullptr);
            }
        }
    );

    TestTrue(
        FString::Printf(TEXT("Handle.IsValid() 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), HandleMetrics.MedianNs, HandleValidationBudgetNsPerOp),
        HandleMetrics.MedianNs < HandleValidationBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] Handle Validation: %.2f ns/op (CV: %.2f%%, %.2f Mops/s) | Raw Pointer Check: %.2f ns/op (CV: %.2f%%) | Overhead: +%.2f ns"),
        HandleMetrics.MedianNs,
        HandleMetrics.CvPercent,
        HandleMetrics.OpsPerSec / 1.0e6,
        RawMetrics.MedianNs,
        RawMetrics.CvPercent,
        FMath::Max(0.0, HandleMetrics.MedianNs - RawMetrics.MedianNs)
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    (void)Subsystem->ReleaseObject(ValidHandle);
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameDeferredReleasePerfTest,
    "Game.Pool.Perf.DeferredReleaseThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 无锁 MPMC 环形缓冲延迟归还入队与主线程批量收割吞吐微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内返回 true
 */
bool FGameDeferredReleasePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Subsystem->ClearAllPools();
    Subsystem->SetMaxObjectPoolCapacity(ObjClass, DeferredReleaseBatchSize);
    Subsystem->PrewarmObjectPool(ObjClass, DeferredReleaseBatchSize);

    TArray<UGamePoolableTestObject*> Batch;
    Batch.Reserve(DeferredReleaseBatchSize);

    const FBenchmarkMetrics DeferredMetrics = RunBenchmark(
        1,
        DefaultSampleRounds,
        DeferredReleaseBatchSize,
        [Subsystem, ObjClass, &Batch](const int32 Iterations) {
            Batch.Reset();
            for (int32 Index = 0; Index < Iterations; ++Index) {
                Batch.Add(Subsystem->AcquireObject<UGamePoolableTestObject>(ObjClass).GetValue());
            }
            for (UGamePoolableTestObject* Obj : Batch) {
                (void)Subsystem->ReleaseObjectDeferred(Obj);
            }
            (void)Subsystem->ProcessDeferredReleases(Iterations);
        }
    );

    TestTrue(
        FString::Printf(TEXT("Deferred Acquire+Enqueue+Harvest 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), DeferredMetrics.MedianNs, DeferredReleaseBudgetNsPerOp),
        DeferredMetrics.MedianNs < DeferredReleaseBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] Deferred Release (batch %d): %.1f ns/op (CV: %.2f%%, %.1f Kops/s)"),
        DeferredReleaseBatchSize,
        DeferredMetrics.MedianNs,
        DeferredMetrics.CvPercent,
        DeferredMetrics.OpsPerSec / 1.0e3
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    Subsystem->SetMaxObjectPoolCapacity(ObjClass, 100);
    Subsystem->ClearAllPools();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameEvictionBatchPerfTest,
    "Game.Pool.Perf.EvictionBatchThroughput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 大规模空闲 UObject 池 TDeque O(1) FIFO 批量冷淘汰吞吐微基准
 * @param Parameters 自动化框架参数（未使用）
 * @return 预算内返回 true
 */
bool FGameEvictionBatchPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Subsystem->ClearAllPools();
    Subsystem->SetMaxObjectPoolCapacity(ObjClass, EvictionBatchSize);

    FPoolEvictionSettings Settings;
    Settings.bEnableEviction = true;
    Settings.IdleTimeToLiveSeconds = 1.0f;
    Settings.MinPreservedCount = 0;
    Subsystem->SetObjectPoolEvictionSettings(ObjClass, Settings);

    TArray<double> SamplesNs;
    SamplesNs.Reserve(DefaultSampleRounds);

    for (int32 Warmup = 0; Warmup < 1; ++Warmup) {
        Subsystem->PrewarmObjectPool(ObjClass, EvictionBatchSize);
        const double FutureNow = FPlatformTime::Seconds() + 10.0;
        (void)Subsystem->ForceEvictExpired(EvictionBatchSize, FutureNow);
    }

    double SumNs = 0.0;
    for (int32 Round = 0; Round < DefaultSampleRounds; ++Round) {
        Subsystem->PrewarmObjectPool(ObjClass, EvictionBatchSize);
        const double FutureNow = FPlatformTime::Seconds() + 10.0;
        const double StartTime = FPlatformTime::Seconds();
        const int32 Evicted = Subsystem->ForceEvictExpired(EvictionBatchSize, FutureNow);
        const double ElapsedSec = FPlatformTime::Seconds() - StartTime;
        TestEqual(TEXT("每轮完整淘汰全部 256 个冷实例"), Evicted, EvictionBatchSize);
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(EvictionBatchSize);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[DefaultSampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(DefaultSampleRounds);

    TestTrue(
        FString::Printf(TEXT("TDeque FIFO Eviction 耗时 %.1f ns/op 应低于预算 %.0f ns/op"), MedianNs, EvictionBatchBudgetNsPerOp),
        MedianNs < EvictionBatchBudgetNsPerOp
    );

    const FString Report = FString::Printf(
        TEXT("[GamePoolPerf] TDeque FIFO Eviction (batch %d): Median %.1f ns/op | Mean %.1f ns/op | Min %.1f | Max %.1f"),
        EvictionBatchSize,
        MedianNs,
        MeanNs,
        SamplesNs[0],
        SamplesNs.Last()
    );
    GAME_LOG(Display, TEXT("%s"), *Report);
    AddInfo(Report);

    Subsystem->SetObjectPoolEvictionSettings(ObjClass, FPoolEvictionSettings{});
    Subsystem->SetMaxObjectPoolCapacity(ObjClass, 100);
    Subsystem->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
