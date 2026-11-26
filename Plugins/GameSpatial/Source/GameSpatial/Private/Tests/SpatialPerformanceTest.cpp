/**
 * @file SpatialPerformanceTest.cpp
 * @brief GameSpatial 全组件热路径微基准性能测试套件（真实分布式数据、7 轮采样、防 DCE Sink 与 GameLogging 输出）
 */

#include "Grid/TSpatialHashGrid2D.h"
#include "Grid/TSpatialHashGrid3D.h"
#include "Subsystem/UGameSpatialSubsystem.h"
#include "Throttle/FSpatialThrottleDriver.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Logging/GameLogCategories.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"

namespace {

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 7;

constexpr int32 GridUpdateIterations = 10000;
constexpr double GridSameCellUpdateBudgetNs = 500.0;
constexpr double GridCrossCellUpdateBudgetNs = 1000.0;
constexpr double GridChurnBudgetNs = 1200.0;

constexpr int32 GridQueryIterations = 10000;
constexpr double GridQuery2DBudgetNs = 2500.0;
constexpr double GridQuery3DBudgetNs = 3500.0;
constexpr double GridNearestBudgetNs = 2500.0;
constexpr double GridSparseHugeBudgetNs = 2500.0;

constexpr int32 ThrottleEntityCount = 500;
constexpr int32 ThrottleUpdateIterations = 1000;
constexpr double ThrottleSteadyBudgetNs = 15000.0;
constexpr double ThrottleDynamicBudgetNs = 30000.0;
constexpr double ThrottleChurnBudgetNs = 1500.0;

constexpr int32 SubsystemActorCount = 1000;
constexpr int32 SubsystemUpdateIterations = 2000;
constexpr double SubsystemUpdateBudgetNs = 2000.0;
constexpr int32 SubsystemQueryIterations = 1000;
constexpr double SubsystemQueryBudgetNs = 5000.0;

UWorld* GetSubsystemBenchmarkWorld() {
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

AActor* SpawnSubsystemBenchmarkActor(UWorld* World, const FVector& Location) {
    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), SpawnParams);
    if (Actor != nullptr) {
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
    for (const double Val : SamplesNs) {
        const double Diff = Val - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));

    FBenchmarkMetrics Metrics;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

void RecordBenchmarkResult(
    FAutomationTestBase* Test,
    const TCHAR* TestName,
    const FBenchmarkMetrics& Metrics,
    double BudgetNs
) {
    const FString Msg = FString::Printf(
        TEXT("[SpatialPerf] %s: 中位数 %.2f ns (预算 %.0f ns), 均值 %.2f ns, 离散度 CV=%.2f%%, 吞吐量 %.0f ops/s"),
        TestName,
        Metrics.MedianNs,
        BudgetNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec
    );
    GAME_LOG(Display, TEXT("%s"), *Msg);
    Test->AddInfo(Msg);
    Test->TestTrue(Msg, Metrics.MedianNs <= BudgetNs);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid2DBenchmarkTest,
    "Game.Spatial.Benchmark.HashGrid2D",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：2D 空间网格真实同格/跨格更新、增删抖动及分布式千实体全几何查询
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FSpatialHashGrid2DBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 EntityCount = 1000;
    volatile int64 Sink = 0;

    TSpatialHashGrid2D<int32> UpdateGrid(500.0f);
    UpdateGrid.Reserve(EntityCount);
    TArray<FVector2D> BasePositions;
    BasePositions.Reserve(EntityCount);
    for (int32 Idx = 0; Idx < EntityCount; ++Idx) {
        const FVector2D Pos(static_cast<double>((Idx % 32) * 200 - 3200), static_cast<double>((Idx / 32) * 200 - 3200));
        BasePositions.Add(Pos);
        UpdateGrid.Insert(Idx, Pos);
    }

    int32 TickEpoch = 1;
    const FBenchmarkMetrics SameCellMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Id = Step % EntityCount;
                const int32 Pass = TickEpoch + (Step / EntityCount);
                const double Delta = static_cast<double>(Pass % 13) * 0.5 + 0.25;
                UpdateGrid.Update(Id, FVector2D(BasePositions[Id].X + Delta, BasePositions[Id].Y + Delta));
            }
            TickEpoch += 10;
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 同格 O(1) 真实更新"), SameCellMetrics, GridSameCellUpdateBudgetNs);

    const FBenchmarkMetrics CrossCellMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Id = Step % EntityCount;
                const int32 Pass = TickEpoch + (Step / EntityCount);
                const double CellShift = static_cast<double>(Pass % 7) * 520.0;
                UpdateGrid.Update(Id, FVector2D(BasePositions[Id].X + CellShift, BasePositions[Id].Y + CellShift));
            }
            TickEpoch += 10;
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 跨格 O(1) 迁移更新"), CrossCellMetrics, GridCrossCellUpdateBudgetNs);

    const FBenchmarkMetrics ChurnMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Id = Step % EntityCount;
                UpdateGrid.Remove(Id);
                UpdateGrid.Insert(Id, BasePositions[Id]);
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("2D Remove+Insert 往返"), ChurnMetrics, GridChurnBudgetNs);

    TSpatialHashGrid2D<int32> QueryGrid(500.0f);
    QueryGrid.Reserve(EntityCount);
    for (int32 Idx = 0; Idx < EntityCount; ++Idx) {
        QueryGrid.Insert(Idx, BasePositions[Idx]);
    }

    TArray<int32> QueryBuffer;
    const FBenchmarkMetrics RadiusArrayMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double CenterCoord = static_cast<double>((Step % 20) * 150 - 1500);
                QueryGrid.QueryRadius(FVector2D(CenterCoord, CenterCoord), 600.0f, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体半径查询 (TArray)"), RadiusArrayMetrics, GridQuery2DBudgetNs);

    const FBenchmarkMetrics RadiusStreamMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            int64 LocalSum = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double CenterCoord = static_cast<double>((Step % 20) * 150 - 1500);
                QueryGrid.QueryRadiusMatching(FVector2D(CenterCoord, CenterCoord), 600.0f, [&LocalSum](int32 Id) {
                    LocalSum += Id;
                });
            }
            Sink = Sink + LocalSum;
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体流式半径查询 (零分配)"), RadiusStreamMetrics, GridQuery2DBudgetNs);

    const FBenchmarkMetrics AabbMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 20) * 150 - 1500);
                QueryGrid.QueryAABB(FBox2D(FVector2D(C - 500.0, C - 500.0), FVector2D(C + 500.0, C + 500.0)), QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体 AABB 查询"), AabbMetrics, GridQuery2DBudgetNs);

    const FBenchmarkMetrics SectorMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 20) * 150 - 1500);
                QueryGrid.QuerySector(FVector2D(C, C), FVector2D(1.0, 0.0), 800.0f, 30.0f, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体 30° 紧致扇形查询"), SectorMetrics, GridQuery2DBudgetNs);

    const FBenchmarkMetrics NearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            int64 LocalSum = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 20) * 150 - 1500);
                int32 BestId = 0;
                if (QueryGrid.QueryNearest(FVector2D(C, C), 3000.0f, BestId)) {
                    LocalSum += BestId;
                }
            }
            Sink = Sink + LocalSum;
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体最近邻查询"), NearestMetrics, GridNearestBudgetNs);

    const FBenchmarkMetrics KNearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 20) * 150 - 1500);
                QueryGrid.QueryKNearest(FVector2D(C, C), 3000.0f, 5, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("2D 分布式千实体 K=5 内联近邻查询"), KNearestMetrics, GridNearestBudgetNs);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialHashGrid3DBenchmarkTest,
    "Game.Spatial.Benchmark.HashGrid3D",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：3D 空间网格真实更新、立体分布式查询、平面地图抗碰撞查询及稀疏大半径快路径
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FSpatialHashGrid3DBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    constexpr int32 EntityCount = 1000;
    volatile int64 Sink = 0;

    TSpatialHashGrid3D<int32> UpdateGrid(500.0f);
    UpdateGrid.Reserve(EntityCount);
    TArray<FVector> BasePositions;
    BasePositions.Reserve(EntityCount);
    for (int32 Idx = 0; Idx < EntityCount; ++Idx) {
        const FVector Pos(
            static_cast<double>((Idx % 10) * 300 - 1500),
            static_cast<double>(((Idx / 10) % 10) * 300 - 1500),
            static_cast<double>((Idx / 100) * 200 - 1000)
        );
        BasePositions.Add(Pos);
        UpdateGrid.Insert(Idx, Pos);
    }

    int32 TickEpoch = 1;
    const FBenchmarkMetrics SameCellMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Id = Step % EntityCount;
                const int32 Pass = TickEpoch + (Step / EntityCount);
                const double Delta = static_cast<double>(Pass % 13) * 0.5 + 0.25;
                UpdateGrid.Update(Id, BasePositions[Id] + FVector(Delta, Delta, Delta));
            }
            TickEpoch += 10;
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 同格 O(1) 真实更新"), SameCellMetrics, GridSameCellUpdateBudgetNs);

    const FBenchmarkMetrics CrossCellMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Id = Step % EntityCount;
                const int32 Pass = TickEpoch + (Step / EntityCount);
                const double CellShift = static_cast<double>(Pass % 7) * 520.0;
                UpdateGrid.Update(Id, BasePositions[Id] + FVector(CellShift, CellShift, 0.0));
            }
            TickEpoch += 10;
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 跨格 O(1) 迁移更新"), CrossCellMetrics, GridCrossCellUpdateBudgetNs);

    TSpatialHashGrid3D<int32> QueryGrid(500.0f);
    QueryGrid.Reserve(EntityCount);
    for (int32 Idx = 0; Idx < EntityCount; ++Idx) {
        QueryGrid.Insert(Idx, BasePositions[Idx]);
    }

    TArray<int32> QueryBuffer;
    const FBenchmarkMetrics SphereMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 100 - 750);
                QueryGrid.QueryRadius(FVector(C, C, 0.0), 800.0f, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体球形查询"), SphereMetrics, GridQuery3DBudgetNs);

    const FBenchmarkMetrics AabbMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 100 - 750);
                QueryGrid.QueryAABB(FBox(FVector(C - 500.0, C - 500.0, -300.0), FVector(C + 500.0, C + 500.0, 300.0)), QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体 AABB 查询"), AabbMetrics, GridQuery3DBudgetNs);

    const FBenchmarkMetrics ConeMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 100 - 750);
                QueryGrid.QueryCone(FVector(C, C, 0.0), FVector(1.0, 0.0, 0.0), 900.0f, 30.0f, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体 30° 紧致锥体查询"), ConeMetrics, GridQuery3DBudgetNs);

    const FBenchmarkMetrics CylinderMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 100 - 750);
                QueryGrid.QueryCylinder(FVector(C, C, 0.0), 600.0f, 300.0f, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体圆柱体查询"), CylinderMetrics, GridQuery3DBudgetNs);

    const FBenchmarkMetrics NearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            int64 LocalSum = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 80 - 600);
                int32 BestId = 0;
                if (QueryGrid.QueryNearest(FVector(C, C, 0.0), 4000.0f, BestId)) {
                    LocalSum += BestId;
                }
            }
            Sink = Sink + LocalSum;
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体最近邻查询"), NearestMetrics, GridNearestBudgetNs);

    const FBenchmarkMetrics KNearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 15) * 80 - 600);
                QueryGrid.QueryKNearest(FVector(C, C, 0.0), 4000.0f, 5, QueryBuffer);
                Sink = Sink + QueryBuffer.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 分布式千实体 K=5 内联近邻查询"), KNearestMetrics, GridNearestBudgetNs);

    TSpatialHashGrid3D<int32> FlatMapGrid(500.0f);
    FlatMapGrid.Reserve(EntityCount);
    for (int32 Idx = 0; Idx < EntityCount; ++Idx) {
        FlatMapGrid.Insert(
            Idx,
            FVector(static_cast<double>((Idx % 32) * 520), static_cast<double>((Idx / 32) * 520), 0.0)
        );
    }
    const FBenchmarkMetrics FlatMapMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            int64 LocalSum = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double C = static_cast<double>((Step % 20) * 520 + 200);
                FlatMapGrid.QueryRadiusMatching(FVector(C, C, 0.0), 700.0f, [&LocalSum](int32 Id) { LocalSum += Id; });
            }
            Sink = Sink + LocalSum;
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 平面地图 Z=0 (1000 独立桶) 雪崩哈希球形流式查询"), FlatMapMetrics, GridQuery3DBudgetNs);

    TSpatialHashGrid3D<int32> SparseGrid(500.0f);
    for (int32 Idx = 0; Idx < 100; ++Idx) {
        SparseGrid.Insert(Idx, FVector(static_cast<double>(Idx) * 1200.0, 0.0, 0.0));
    }
    const FBenchmarkMetrics SparseHugeMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        GridQueryIterations,
        [&](int32 Iterations) {
            int64 LocalSum = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                int32 BestId = 0;
                if (SparseGrid.QueryNearest(FVector(-5000.0, 0.0, 0.0), 50000.0f, BestId)) {
                    LocalSum += BestId;
                }
            }
            Sink = Sink + LocalSum;
        }
    );
    RecordBenchmarkResult(this, TEXT("3D 稀疏图超大半径 (500m / 100 实体) 快路径近邻查询"), SparseHugeMetrics, GridSparseHugeBudgetNs);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSpatialThrottleDriverBenchmarkTest,
    "Game.Spatial.Benchmark.ThrottleDriver",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：视锥节流驱动器 500 实体稳态评估、动态旋转跳档及 O(1) 注册/注销吞吐
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FSpatialThrottleDriverBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FThrottledTickScheduler Scheduler;
    FSpatialThrottleDriver Driver(Scheduler);

    TArray<FVector> EntityPositions;
    EntityPositions.SetNum(ThrottleEntityCount);
    for (int32 Idx = 0; Idx < ThrottleEntityCount; ++Idx) {
        EntityPositions[Idx] = FVector(
            static_cast<double>((Idx % 25) * 200 - 2500),
            static_cast<double>((Idx / 25) * 200 - 2000),
            0.0
        );
        Driver.RegisterSpatialEntity(
            nullptr,
            [&EntityPositions, Idx]() { return EntityPositions[Idx]; },
            [](float) {},
            (Idx % 50 == 0)
        );
    }

    const FSpatialViewContext SteadyView(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 90.0f, 10000.0f);
    Driver.UpdateSpatialThrottle(SteadyView);

    const FBenchmarkMetrics SteadyMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ThrottleUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                Driver.UpdateSpatialThrottle(SteadyView);
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("节流驱动器 500 实体稳态集中评估"), SteadyMetrics, ThrottleSteadyBudgetNs);

    const FBenchmarkMetrics DynamicMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ThrottleUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double Angle = static_cast<double>(Step % 36) * (UE_TWO_PI / 36.0);
                const FSpatialViewContext PanningView(
                    FVector::ZeroVector,
                    FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0),
                    90.0f,
                    10000.0f
                );
                Driver.UpdateSpatialThrottle(PanningView);
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("节流驱动器 500 实体动态旋转视锥评估"), DynamicMetrics, ThrottleDynamicBudgetNs);

    TArray<uint64> ChurnHandles;
    ChurnHandles.Reserve(ThrottleEntityCount);
    const FBenchmarkMetrics ChurnMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        ThrottleEntityCount,
        [&](int32 Iterations) {
            ChurnHandles.Reset();
            for (int32 Step = 0; Step < Iterations; ++Step) {
                ChurnHandles.Add(Driver.RegisterSpatialEntity(nullptr, []() { return FVector::ZeroVector; }, [](float) {}));
            }
            for (int32 Step = Iterations - 1; Step >= 0; --Step) {
                Driver.UnregisterSpatialEntity(ChurnHandles[Step]);
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("节流驱动器单实体 O(1) 注册+注销往返"), ChurnMetrics, ThrottleChurnBudgetNs);

    Driver.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemBenchmarkTest,
    "Game.Spatial.Benchmark.SubsystemActors",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 微基准：空间索引子系统千级 Actor 真实位移更新、零分配流式查询与 K 近邻检索
 * @param Parameters 自动化测试参数
 * @return 全部预算断言通过返回 true
 */
bool FGameSpatialSubsystemBenchmarkTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetSubsystemBenchmarkWorld();
    TestNotNull(TEXT("世界上下文必须有效"), World);
    if (World == nullptr) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    TestNotNull(TEXT("空间子系统必须成功加载"), Subsystem);
    if (Subsystem == nullptr) {
        return false;
    }

    Subsystem->ClearAll();
    volatile int64 Sink = 0;

    TArray<AActor*> SpawnedActors;
    TArray<FVector> BaseLocations;
    SpawnedActors.Reserve(SubsystemActorCount);
    BaseLocations.Reserve(SubsystemActorCount);

    for (int32 Idx = 0; Idx < SubsystemActorCount; ++Idx) {
        const FVector Loc(
            static_cast<double>((Idx % 25) * 200 - 2500),
            static_cast<double>(((Idx / 25) % 25) * 200 - 2500),
            static_cast<double>((Idx / 625) * 200)
        );
        if (AActor* Actor = SpawnSubsystemBenchmarkActor(World, Loc)) {
            SpawnedActors.Add(Actor);
            BaseLocations.Add(Loc);
            Subsystem->RegisterActor(Actor);
        }
    }

    TestEqual(TEXT("成功注册 1000 个测试 Actor"), Subsystem->GetTotalActorCount(), SubsystemActorCount);

    int32 UpdateEpoch = 1;
    const FBenchmarkMetrics UpdateMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemUpdateIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const int32 Idx = Step % SubsystemActorCount;
                const int32 Pass = UpdateEpoch + (Step / SubsystemActorCount);
                const double Delta = static_cast<double>(Pass % 13) * 1.5 + 0.25;
                Subsystem->UpdateActorLocation(SpawnedActors[Idx], BaseLocations[Idx] + FVector(Delta, Delta, 0.0));
            }
            UpdateEpoch += 2;
        }
    );
    RecordBenchmarkResult(this, TEXT("子系统千级 Actor 真实位移更新"), UpdateMetrics, SubsystemUpdateBudgetNs);

    const FBenchmarkMetrics StreamingMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemQueryIterations,
        [&](int32 Iterations) {
            int64 HitCount = 0;
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double Offset = static_cast<double>((Step % 10) * 150 - 750);
                Subsystem->ForEachActorInRadius(FVector(Offset, Offset, 0.0), 600.0f, [&HitCount](AActor*) {
                    ++HitCount;
                });
            }
            Sink = Sink + HitCount;
        }
    );
    RecordBenchmarkResult(this, TEXT("子系统千级 Actor 零分配流式球形查询"), StreamingMetrics, SubsystemQueryBudgetNs);

    TArray<AActor*> QueryResults;
    const FBenchmarkMetrics KNearestMetrics = RunBenchmarkRounds(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        SubsystemQueryIterations,
        [&](int32 Iterations) {
            for (int32 Step = 0; Step < Iterations; ++Step) {
                const double Offset = static_cast<double>((Step % 10) * 150 - 750);
                Subsystem->QueryKNearestActors(FVector(Offset, Offset, 0.0), 3000.0f, 5, QueryResults);
                Sink = Sink + QueryResults.Num();
            }
        }
    );
    RecordBenchmarkResult(this, TEXT("子系统千级 Actor K=5 近邻检索"), KNearestMetrics, SubsystemQueryBudgetNs);

    for (AActor* Actor : SpawnedActors) {
        if (Actor != nullptr) {
            Subsystem->UnregisterActor(Actor);
            World->DestroyActor(Actor);
        }
    }
    Subsystem->ClearAll();
    return true;
}

#endif
