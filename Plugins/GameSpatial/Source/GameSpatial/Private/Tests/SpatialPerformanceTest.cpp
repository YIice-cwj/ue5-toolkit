/**
 * @file SpatialPerformanceTest.cpp
 * @brief GameSpatial 空间哈希网格与视锥节流驱动器基准性能测试
 */

#include "Spatial/FSpatialThrottleDriver.h"
#include "Spatial/TSpatialHashGrid2D.h"
#include "Spatial/TSpatialHashGrid3D.h"
#include "Subsystem/UGameSpatialSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include <algorithm>
#include <cmath>
#include <numeric>

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

constexpr int32 GridUpdateIterations = 10000;
constexpr double GridUpdateBudgetNsPerOp = 800.0;

constexpr int32 GridQueryIterations = 10000;
constexpr double GridQueryBudgetNsPerOp = 3500.0;

constexpr int32 NearestPruneIterations = 10000;
constexpr double NearestPruneBudgetNsPerOp = 3000.0;

constexpr int32 ThrottleUpdateIterations = 1000;
constexpr double ThrottleUpdateBudgetNsPerOp = 20000.0;

constexpr int32 SubsystemBenchmarkActorCount = 1000;
constexpr int32 SubsystemUpdateIterations = 1000;
constexpr double SubsystemUpdateBudgetNsPerOp = 3000.0;

constexpr int32 SubsystemQueryIterations = 500;
constexpr double SubsystemQueryBudgetNsPerOp = 8000.0;

UWorld* GetSubsystemBenchmarkWorld() {
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

AActor* SpawnSubsystemBenchmarkActor(UWorld* World, const FVector& Location) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), SpawnParams);
    if (Actor) {
        USceneComponent* RootComp = NewObject<USceneComponent>(Actor);
        Actor->SetRootComponent(RootComp);
        RootComp->RegisterComponent();
        Actor->SetActorLocation(Location);
    }
    return Actor;
}

struct FBenchmarkMetrics {
    double MedianNs = 0.0;
    double MeanNs = 0.0;
    double CvPercent = 0.0;
    double MinNs = 0.0;
    double MaxNs = 0.0;
    double OpsPerSec = 0.0;
};

template <typename TFunc>
FBenchmarkMetrics RunBenchmarkRounds(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> RoundNsPerOp;
    RoundNsPerOp.Reserve(SampleRounds);

    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const double StartTime = FPlatformTime::Seconds();
        Body(IterationsPerRound);
        const double EndTime = FPlatformTime::Seconds();

        const double ElapsedNs = (EndTime - StartTime) * 1e9;
        RoundNsPerOp.Add(ElapsedNs / static_cast<double>(IterationsPerRound));
    }

    RoundNsPerOp.Sort();

    FBenchmarkMetrics Metrics;
    Metrics.MinNs = RoundNsPerOp[0];
    Metrics.MaxNs = RoundNsPerOp.Last();
    Metrics.MedianNs = RoundNsPerOp[SampleRounds / 2];

    const double Sum = std::accumulate(RoundNsPerOp.GetData(), RoundNsPerOp.GetData() + SampleRounds, 0.0);
    Metrics.MeanNs = Sum / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (double Val : RoundNsPerOp) {
        const double Diff = Val - Metrics.MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = std::sqrt(VarianceSum / static_cast<double>(SampleRounds));
    Metrics.CvPercent = (Metrics.MeanNs > 0.0) ? (StdDev / Metrics.MeanNs) * 100.0 : 0.0;
    Metrics.OpsPerSec = (Metrics.MedianNs > 0.0) ? (1e9 / Metrics.MedianNs) : 0.0;

    return Metrics;
}

void RecordBenchmarkResult(
    FAutomationTestBase* Test,
    const TCHAR* TestName,
    const FBenchmarkMetrics& Metrics,
    double BudgetNs
) {
    const FString Msg = FString::Printf(
        TEXT("[SpatialPerf] %s: 中位数 %.2f ns (预算 %.2f ns), 均值 %.2f ns, 离散度 CV=%.2f%%, 吞吐量 %.0f ops/s"),
        TestName,
        Metrics.MedianNs,
        BudgetNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec
    );
    UE_LOG(LogTemp, Display, TEXT("%s"), *Msg);
    Test->TestTrue(Msg, Metrics.MedianNs <= BudgetNs);
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DBenchmarkTest,
    "Game.Spatial.Benchmark.HashGrid2D",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证二维空间哈希网格高频更新、圆形范围检索与近邻剪枝查询性能预算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid2DBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid2D<int32> Grid(500.0f);

    constexpr int32 InitialEntityCount = 1000;
    for (int32 Index = 0; Index < InitialEntityCount; ++Index) {
        const float X = static_cast<float>((Index % 32) * 200 - 3200);
        const float Y = static_cast<float>((Index / 32) * 200 - 3200);
        Grid.Insert(Index, FVector2D(X, Y));
    }

    const FBenchmarkMetrics UpdateMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&Grid, InitialEntityCount](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 TargetId = Step % InitialEntityCount;
                const float Offset = static_cast<float>(Step % 100);
                Grid.Update(TargetId, FVector2D(Offset, Offset));
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("2D 网格更新"), UpdateMetrics, GridUpdateBudgetNsPerOp);

    TArray<int32> QueryBuffer;
    const FBenchmarkMetrics QueryMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&Grid, &QueryBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const float CenterX = static_cast<float>((Step % 20) * 100 - 1000);
                Grid.QueryRadius(FVector2D(CenterX, CenterX), 600.0f, QueryBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("2D 网格半径查询"), QueryMetrics, GridQueryBudgetNsPerOp);

    int32 NearestResult = 0;
    const FBenchmarkMetrics NearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        NearestPruneIterations,
        [&Grid, &NearestResult](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const float QueryX = static_cast<float>((Step % 10) * 50);
                Grid.QueryNearest(FVector2D(QueryX, QueryX), 3000.0f, NearestResult);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("2D 网格近邻剪枝查询"), NearestMetrics, NearestPruneBudgetNsPerOp);

    TArray<int32> SectorBuffer;
    const FBenchmarkMetrics SectorMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&Grid, &SectorBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const float CenterX = static_cast<float>((Step % 20) * 100 - 1000);
                Grid.QuerySector(FVector2D(CenterX, CenterX), FVector2D(1.0f, 0.0f), 600.0f, 45.0f, SectorBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("2D 网格扇形查询"), SectorMetrics, GridQueryBudgetNsPerOp);

    TArray<int32> KNearest2DBuffer;
    const FBenchmarkMetrics KNearest2DMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        NearestPruneIterations,
        [&Grid, &KNearest2DBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const float QueryX = static_cast<float>((Step % 10) * 50);
                Grid.QueryKNearest(FVector2D(QueryX, QueryX), 3000.0f, 5, KNearest2DBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("2D 网格 K 近邻剪枝查询"), KNearest2DMetrics, NearestPruneBudgetNsPerOp);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DBenchmarkTest,
    "Game.Spatial.Benchmark.HashGrid3D",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证三维空间哈希网格更新、球形/圆柱体查询与动态剪枝近邻查询性能预算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialHashGrid3DBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TSpatialHashGrid3D<int32> Grid(500.0f);

    constexpr int32 InitialEntityCount = 1000;
    for (int32 Index = 0; Index < InitialEntityCount; ++Index) {
        const double X = static_cast<double>((Index % 10) * 300 - 1500);
        const double Y = static_cast<double>(((Index / 10) % 10) * 300 - 1500);
        const double Z = static_cast<double>((Index / 100) * 200 - 1000);
        Grid.Insert(Index, FVector(X, Y, Z));
    }

    const FBenchmarkMetrics UpdateMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&Grid, InitialEntityCount](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 TargetId = Step % InitialEntityCount;
                const double Offset = static_cast<double>(Step % 50);
                Grid.Update(TargetId, FVector(Offset, Offset, Offset));
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格更新"), UpdateMetrics, GridUpdateBudgetNsPerOp);

    TArray<int32> QueryBuffer;
    const FBenchmarkMetrics QueryMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&Grid, &QueryBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double QueryCoord = static_cast<double>((Step % 15) * 100 - 750);
                Grid.QueryRadius(FVector(QueryCoord, QueryCoord, 0.0), 800.0f, QueryBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格球形查询"), QueryMetrics, GridQueryBudgetNsPerOp);

    int32 NearestResult = 0;
    const FBenchmarkMetrics NearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        NearestPruneIterations,
        [&Grid, &NearestResult](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double QueryCoord = static_cast<double>((Step % 10) * 40);
                Grid.QueryNearest(FVector(QueryCoord, QueryCoord, 0.0), 4000.0f, NearestResult);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格近邻剪枝查询"), NearestMetrics, NearestPruneBudgetNsPerOp);

    TArray<int32> ConeBuffer;
    const FBenchmarkMetrics ConeMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&Grid, &ConeBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double QueryCoord = static_cast<double>((Step % 15) * 100 - 750);
                Grid.QueryCone(FVector(QueryCoord, QueryCoord, 0.0), FVector(1.0, 0.0, 0.0), 800.0f, 45.0f, ConeBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格锥体查询"), ConeMetrics, GridQueryBudgetNsPerOp);

    TArray<int32> CylinderBuffer;
    const FBenchmarkMetrics CylinderMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&Grid, &CylinderBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double QueryCoord = static_cast<double>((Step % 15) * 100 - 750);
                Grid.QueryCylinder(FVector(QueryCoord, QueryCoord, 0.0), 600.0f, 300.0f, CylinderBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格圆柱体查询"), CylinderMetrics, GridQueryBudgetNsPerOp);

    TArray<int32> KNearestBuffer;
    const FBenchmarkMetrics KNearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        NearestPruneIterations,
        [&Grid, &KNearestBuffer](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double QueryCoord = static_cast<double>((Step % 10) * 40);
                Grid.QueryKNearest(FVector(QueryCoord, QueryCoord, 0.0), 4000.0f, 5, KNearestBuffer);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("3D 网格 K 近邻剪枝查询"), KNearestMetrics, NearestPruneBudgetNsPerOp);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverBenchmarkTest,
    "Game.Spatial.Benchmark.ThrottleDriver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间节流驱动器在 1000 个实体下的集中批量评估更新耗时预算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FSpatialThrottleDriverBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);

    constexpr int32 EntityCount = 500;
    TArray<FVector> EntityPositions;
    EntityPositions.SetNum(EntityCount);

    for (int32 Index = 0; Index < EntityCount; ++Index) {
        EntityPositions[Index] = FVector(
            static_cast<double>((Index % 25) * 200 - 2500),
            static_cast<double>((Index / 25) * 200 - 2500),
            0.0
        );

        Driver.RegisterSpatialEntity(
            nullptr,
            [&EntityPositions, Index]() { return EntityPositions[Index]; },
            [](float) {},
            (Index % 50 == 0)
        );
    }

    const FSpatialViewContext View(
        FVector::ZeroVector,
        FVector(1.0f, 0.0f, 0.0f),
        90.0f,
        10000.0f
    );

    const FBenchmarkMetrics ThrottleMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ThrottleUpdateIterations,
        [&Driver, &View](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                Driver.UpdateSpatialThrottle(View);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("节流驱动器 500 实体集中评估"), ThrottleMetrics, ThrottleUpdateBudgetNsPerOp);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemBenchmarkTest,
    "Game.Spatial.Benchmark.SubsystemActors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间索引子系统在千级 Actor 场景下的高频更新与流式范围查询性能预算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetSubsystemBenchmarkWorld();
    TestNotNull(TEXT("空间子系统基准测试必须具备有效世界上下文"), World);
    if (!World) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    TestNotNull(TEXT("空间子系统必须成功加载"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAll();

    TArray<AActor*> SpawnedActors;
    SpawnedActors.Reserve(SubsystemBenchmarkActorCount);

    for (int32 Index = 0; Index < SubsystemBenchmarkActorCount; ++Index) {
        const double PosX = static_cast<double>((Index % 25) * 200 - 2500);
        const double PosY = static_cast<double>(((Index / 25) % 25) * 200 - 2500);
        const double PosZ = static_cast<double>((Index / 625) * 200);

        AActor* Actor = SpawnSubsystemBenchmarkActor(World, FVector(PosX, PosY, PosZ));
        if (Actor) {
            SpawnedActors.Add(Actor);
            Subsystem->RegisterActor(Actor);
        }
    }

    TestEqual(TEXT("已生成并注册 1000 个测试 Actor"), SpawnedActors.Num(), SubsystemBenchmarkActorCount);
    TestEqual(TEXT("子系统当前 Actor 总数匹配"), Subsystem->GetTotalActorCount(), SubsystemBenchmarkActorCount);

    const FBenchmarkMetrics UpdateMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemUpdateIterations,
        [Subsystem, &SpawnedActors](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                AActor* Actor = SpawnedActors[Step % SubsystemBenchmarkActorCount];
                const FVector CurrentLoc = Actor->GetActorLocation();
                const FVector NewLoc(CurrentLoc.X + 1.0, CurrentLoc.Y + 1.0, CurrentLoc.Z);
                Subsystem->UpdateActorLocation(Actor, NewLoc);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("子系统千级 Actor 更新"), UpdateMetrics, SubsystemUpdateBudgetNsPerOp);

    TArray<AActor*> QueryResults;
    const FBenchmarkMetrics QueryMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemQueryIterations,
        [Subsystem, &QueryResults](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                QueryResults.Reset();
                const double Offset = static_cast<double>(Step % 10) * 100.0;
                Subsystem->QueryActorsInRadius(FVector(Offset, Offset, 0.0), 600.0f, QueryResults);
            }
        }
    );

    RecordBenchmarkResult(this, TEXT("子系统千级 Actor 球形范围流式查询"), QueryMetrics, SubsystemQueryBudgetNsPerOp);

    for (AActor* Actor : SpawnedActors) {
        if (Actor) {
            Subsystem->UnregisterActor(Actor);
            World->DestroyActor(Actor);
        }
    }
    SpawnedActors.Empty();
    Subsystem->ClearAll();

    return true;
}

#endif
