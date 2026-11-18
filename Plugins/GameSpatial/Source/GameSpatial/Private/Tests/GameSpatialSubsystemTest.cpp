/**
 * @file GameSpatialSubsystemTest.cpp
 * @brief UGameSpatialSubsystem 空间索引子系统与蓝图库自动化单元测试
 */

#include "Subsystem/UGameSpatialSubsystem.h"
#include "Library/UGameSpatialBlueprintLibrary.h"

#if WITH_AUTOMATION_TESTS

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"

namespace {

UWorld* GetSubsystemTestWorld() {
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

AActor* SpawnSpatialTestActor(UWorld* World, const FVector& Location) {
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

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemLifecycleAndLayerTest,
    "Game.Spatial.Subsystem.LifecycleAndLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间子系统对 Actor 的注册、注销、位置同步、Tag 分层隔离与统计指标
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemLifecycleAndLayerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetSubsystemTestWorld();
    TestNotNull(TEXT("世界上下文必须有效"), World);
    if (!World) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    TestNotNull(TEXT("空间子系统必须成功加载"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAll();
    TestEqual(TEXT("初始化后总 Actor 数为 0"), Subsystem->GetTotalActorCount(), 0);

    AActor* ActorA = SpawnSpatialTestActor(World, FVector(100.0f, 0.0f, 0.0f));
    AActor* ActorB = SpawnSpatialTestActor(World, FVector(0.0f, 100.0f, 0.0f));
    TestNotNull(TEXT("测试 ActorA 创建成功"), ActorA);
    TestNotNull(TEXT("测试 ActorB 创建成功"), ActorB);

    const FGameplayTag DefaultLayer = FGameplayTag();
    const FGameplayTag EnemyLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName("Spatial.Layer.Enemy"),
        TEXT("测试敌对图层")
    );

    TestTrue(TEXT("注册 ActorA 至默认层"), Subsystem->RegisterActor(ActorA, DefaultLayer));
    TestTrue(TEXT("注册 ActorB 至敌对层"), Subsystem->RegisterActor(ActorB, EnemyLayer));

    TestTrue(TEXT("ActorA 已注册"), Subsystem->IsActorRegistered(ActorA));
    TestTrue(TEXT("ActorB 已注册"), Subsystem->IsActorRegistered(ActorB));
    TestEqual(TEXT("默认层数量为 1"), Subsystem->GetActorCount(DefaultLayer), 1);
    TestEqual(TEXT("敌对层数量为 1"), Subsystem->GetActorCount(EnemyLayer), 1);
    TestEqual(TEXT("总 Actor 数为 2"), Subsystem->GetTotalActorCount(), 2);

    TestTrue(TEXT("手动更新 ActorA 位置成功"), Subsystem->UpdateActorLocation(ActorA, FVector(200.0f, 0.0f, 0.0f)));
    TestTrue(TEXT("自动同步 ActorB 当前坐标成功"), Subsystem->UpdateActor(ActorB));

    TestTrue(TEXT("注销 ActorA 成功"), Subsystem->UnregisterActor(ActorA));
    TestFalse(TEXT("注销后 ActorA 状态为未注册"), Subsystem->IsActorRegistered(ActorA));
    TestEqual(TEXT("注销后默认层数量为 0"), Subsystem->GetActorCount(DefaultLayer), 0);
    TestEqual(TEXT("总数降为 1"), Subsystem->GetTotalActorCount(), 1);

    Subsystem->ClearLayer(EnemyLayer);
    TestEqual(TEXT("清空敌对层后数量为 0"), Subsystem->GetActorCount(EnemyLayer), 0);
    TestEqual(TEXT("清空敌对层后总数为 0"), Subsystem->GetTotalActorCount(), 0);

    ActorA->Destroy();
    ActorB->Destroy();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemQueriesTest,
    "Game.Spatial.Subsystem.GeometricQueries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间子系统的球形、锥体、圆柱体、包围盒与最近邻 Actor 空间检索正确性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemQueriesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetSubsystemTestWorld();
    TestNotNull(TEXT("世界上下文必须有效"), World);
    if (!World) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    TestNotNull(TEXT("空间子系统必须成功加载"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAll();

    AActor* Actor1 = SpawnSpatialTestActor(World, FVector(100.0f, 0.0f, 0.0f));
    AActor* Actor2 = SpawnSpatialTestActor(World, FVector(70.0f, 70.0f, 0.0f));
    AActor* Actor3 = SpawnSpatialTestActor(World, FVector(0.0f, 200.0f, 0.0f));
    AActor* Actor4 = SpawnSpatialTestActor(World, FVector(0.0f, 0.0f, 300.0f));

    const FGameplayTag DefaultLayer = FGameplayTag();
    Subsystem->RegisterActor(Actor1, DefaultLayer);
    Subsystem->RegisterActor(Actor2, DefaultLayer);
    Subsystem->RegisterActor(Actor3, DefaultLayer);
    Subsystem->RegisterActor(Actor4, DefaultLayer);

    TArray<AActor*> RadiusResults;
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 150.0f, DefaultLayer, RadiusResults);
    TestEqual(TEXT("半径 150 命中 2 个目标 (Actor1, Actor2)"), RadiusResults.Num(), 2);
    TestTrue(TEXT("包含 Actor1"), RadiusResults.Contains(Actor1));
    TestTrue(TEXT("包含 Actor2"), RadiusResults.Contains(Actor2));

    TArray<AActor*> DefaultRadiusResults;
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 150.0f, DefaultRadiusResults);
    TestEqual(TEXT("缺省 Tag 重载球形查询命中 2 个目标"), DefaultRadiusResults.Num(), 2);

    TArray<AActor*> ConeResults;
    Subsystem->QueryActorsInCone(FVector::ZeroVector, FVector(1.0f, 0.0f, 0.0f), 150.0f, 30.0f, DefaultLayer, ConeResults);
    TestEqual(TEXT("30度半角前向锥体仅命中正前方 Actor1"), ConeResults.Num(), 1);
    if (!ConeResults.IsEmpty()) {
        TestEqual(TEXT("锥体命中项为 Actor1"), ConeResults[0], Actor1);
    }

    TArray<AActor*> CylinderResults;
    Subsystem->QueryActorsInCylinder(FVector::ZeroVector, 150.0f, 400.0f, DefaultLayer, CylinderResults);
    TestTrue(TEXT("圆柱体包含水平近处的 Actor1"), CylinderResults.Contains(Actor1));
    TestTrue(TEXT("圆柱体包含垂直高处的 Actor4"), CylinderResults.Contains(Actor4));
    TestFalse(TEXT("圆柱体不包含水平远处的 Actor3"), CylinderResults.Contains(Actor3));

    TArray<AActor*> BoxResults;
    Subsystem->QueryActorsInAABB(FBox(FVector(-10.0f, -10.0f, -10.0f), FVector(120.0f, 10.0f, 10.0f)), DefaultLayer, BoxResults);
    TestEqual(TEXT("AABB 范围仅框中 Actor1"), BoxResults.Num(), 1);
    if (!BoxResults.IsEmpty()) {
        TestEqual(TEXT("AABB 命中 Actor1"), BoxResults[0], Actor1);
    }

    AActor* NearestActor = nullptr;
    TestTrue(TEXT("最近 Actor 查询成功"), Subsystem->QueryNearestActor(FVector::ZeroVector, 500.0f, DefaultLayer, NearestActor));
    TestEqual(TEXT("距原点最近为 Actor1 或 Actor2 (距离 100)"), NearestActor == Actor1 || NearestActor == Actor2, true);

    TArray<AActor*> KNearestActors;
    Subsystem->QueryKNearestActors(FVector::ZeroVector, 500.0f, 2, DefaultLayer, KNearestActors);
    TestEqual(TEXT("K=2 命中 2 个最近 Actor"), KNearestActors.Num(), 2);

    const FGameplayTag UnrelatedLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName("Spatial.Layer.Ally"),
        TEXT("测试友军图层")
    );
    TArray<AActor*> EmptyLayerResults;
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 1000.0f, UnrelatedLayer, EmptyLayerResults);
    TestTrue(TEXT("不同层查询绝对隔离返回空"), EmptyLayerResults.IsEmpty());

    Actor1->Destroy();
    Actor2->Destroy();
    Actor3->Destroy();
    Actor4->Destroy();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemWeakPtrAndLibraryTest,
    "Game.Spatial.Subsystem.WeakPtrAndLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间子系统对销毁 Actor 的弱引用安全自愈、惰性剪枝与蓝图函数库调用链
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemWeakPtrAndLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetSubsystemTestWorld();
    TestNotNull(TEXT("世界上下文必须有效"), World);
    if (!World) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    TestNotNull(TEXT("空间子系统必须成功加载"), Subsystem);
    if (!Subsystem) {
        return false;
    }

    Subsystem->ClearAll();

    AActor* LiveActor = SpawnSpatialTestActor(World, FVector(100.0f, 0.0f, 0.0f));
    AActor* DeadActor = SpawnSpatialTestActor(World, FVector(50.0f, 0.0f, 0.0f));

    const FGameplayTag DefaultLayer = FGameplayTag();
    TestTrue(TEXT("蓝图库静态注册 LiveActor"), UGameSpatialBlueprintLibrary::RegisterSpatialActor(World, LiveActor, DefaultLayer));
    TestTrue(TEXT("蓝图库静态注册 DeadActor"), UGameSpatialBlueprintLibrary::RegisterSpatialActor(World, DeadActor, DefaultLayer));

    DeadActor->Destroy();

    TArray<AActor*> QueryResults;
    UGameSpatialBlueprintLibrary::QuerySpatialActorsInRadius(World, FVector::ZeroVector, 200.0f, DefaultLayer, QueryResults);
    TestEqual(TEXT("已销毁 Actor 自动从查询结果中排除"), QueryResults.Num(), 1);
    if (!QueryResults.IsEmpty()) {
        TestEqual(TEXT("仅存活的 LiveActor 被返回"), QueryResults[0], LiveActor);
    }

    AActor* NearestActor = nullptr;
    TestTrue(TEXT("蓝图库最近 Actor 检索成功"), UGameSpatialBlueprintLibrary::QueryNearestSpatialActor(World, FVector::ZeroVector, 200.0f, DefaultLayer, NearestActor));
    TestEqual(TEXT("最近存活目标为 LiveActor"), NearestActor, LiveActor);

    const int32 PrunedCount = Subsystem->PruneStaleActors(DefaultLayer);
    TestEqual(TEXT("PruneStaleActors 成功清理 1 个已失效 Actor"), PrunedCount, 1);
    TestEqual(TEXT("清理后子系统内仅保留 1 个 Actor"), Subsystem->GetActorCount(DefaultLayer), 1);

    AActor* ExtraDeadActor = SpawnSpatialTestActor(World, FVector(30.0f, 0.0f, 0.0f));
    const FGameplayTag TestLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(
        FName("Spatial.Layer.PruneTest"),
        TEXT("清理测试层")
    );
    Subsystem->RegisterActor(ExtraDeadActor, TestLayer);
    ExtraDeadActor->Destroy();
    const int32 TotalPruned = UGameSpatialBlueprintLibrary::PruneAllSpatialStaleActors(World);
    TestEqual(TEXT("PruneAllSpatialStaleActors 成功清理跨层失效 Actor"), TotalPruned, 1);

    TestTrue(TEXT("蓝图库查询 LiveActor 已注册"), UGameSpatialBlueprintLibrary::IsSpatialActorRegistered(World, LiveActor));
    TestEqual(TEXT("蓝图库获取默认层数量为 1"), UGameSpatialBlueprintLibrary::GetSpatialActorCount(World, DefaultLayer), 1);
    TestEqual(TEXT("蓝图库获取总数量为 1"), UGameSpatialBlueprintLibrary::GetTotalSpatialActorCount(World), 1);

    AActor* DyingActor = SpawnSpatialTestActor(World, FVector::ZeroVector);
    DyingActor->Destroy();
    TestFalse(TEXT("濒死销毁中的 Actor 注册被安全拦截"), Subsystem->RegisterActor(DyingActor));
    TestFalse(TEXT("濒死销毁中的 Actor 坐标更新被安全拦截"), Subsystem->UpdateActor(DyingActor));

    UGameSpatialBlueprintLibrary::ClearSpatialLayer(World, DefaultLayer);
    TestEqual(TEXT("蓝图库清空指定图层后数量归零"), Subsystem->GetActorCount(DefaultLayer), 0);

    UGameSpatialBlueprintLibrary::ClearAllSpatialLayers(World);
    TestEqual(TEXT("蓝图库清空全部图层后总数归零"), Subsystem->GetTotalActorCount(), 0);

    TSpatialHashGrid3D<int32> Grid3D(100.0f);
    Grid3D.Insert(42, FVector(10.0, 20.0, 30.0));
    const FVector* FoundLoc = Grid3D.FindLocation(42);
    TestNotNull(TEXT("FindLocation 返回有效非空指针"), FoundLoc);
    if (FoundLoc) {
        TestEqual(TEXT("FindLocation 坐标匹配"), FoundLoc->X, 10.0);
    }
    TestNull(TEXT("不存在元素 FindLocation 返回 nullptr"), Grid3D.FindLocation(999));

    AActor* BatchA = SpawnSpatialTestActor(World, FVector(10.0f, 0.0f, 0.0f));
    AActor* BatchB = SpawnSpatialTestActor(World, FVector(20.0f, 0.0f, 0.0f));
    TArray<AActor*> BatchGroup = {BatchA, BatchB};
    const int32 RegisteredBatch = UGameSpatialBlueprintLibrary::RegisterSpatialActors(World, BatchGroup, TestLayer);
    TestEqual(TEXT("批量注册 2 个 Actor 成功"), RegisteredBatch, 2);

    TestEqual(TEXT("反查 BatchA 图层匹配"), Subsystem->GetActorLayer(BatchA), TestLayer);
    TestEqual(TEXT("蓝图库反查 BatchB 图层匹配"), UGameSpatialBlueprintLibrary::GetSpatialActorLayer(World, BatchB), TestLayer);

    FVector LocA = FVector::ZeroVector;
    TestTrue(TEXT("子系统反查 BatchA 空间坐标成功"), Subsystem->GetActorLocation(BatchA, LocA));
    TestEqual(TEXT("BatchA 空间坐标 X 匹配"), LocA.X, 10.0);

    FVector LocB = FVector::ZeroVector;
    TestTrue(TEXT("蓝图库反查 BatchB 空间坐标成功"), UGameSpatialBlueprintLibrary::GetSpatialActorLocation(World, BatchB, LocB));
    TestEqual(TEXT("BatchB 空间坐标 X 匹配"), LocB.X, 20.0);

    FVector NonExistLoc = FVector::ZeroVector;
    TestFalse(TEXT("未注册 Actor 反查坐标返回 false"), Subsystem->GetActorLocation(nullptr, NonExistLoc));

    TArray<AActor*> ExportedActors;
    UGameSpatialBlueprintLibrary::GetSpatialActorsInLayer(World, TestLayer, ExportedActors);
    TestEqual(TEXT("导出图层全部有效 Actor 数量为 2"), ExportedActors.Num(), 2);
    TestTrue(TEXT("包含 BatchA"), ExportedActors.Contains(BatchA));
    TestTrue(TEXT("包含 BatchB"), ExportedActors.Contains(BatchB));

    TArray<int32> AllGridElements;
    Grid3D.GetAllElements(AllGridElements);
    TestEqual(TEXT("GetAllElements 导出 1 个元素"), AllGridElements.Num(), 1);
    TestEqual(TEXT("导出元素匹配 42"), AllGridElements[0], 42);

    const int32 UnregisteredBatch = UGameSpatialBlueprintLibrary::UnregisterSpatialActors(World, BatchGroup);
    TestEqual(TEXT("批量注销 2 个 Actor 成功"), UnregisteredBatch, 2);
    BatchA->Destroy();
    BatchB->Destroy();

    LiveActor->Destroy();

    return true;
}

#endif
