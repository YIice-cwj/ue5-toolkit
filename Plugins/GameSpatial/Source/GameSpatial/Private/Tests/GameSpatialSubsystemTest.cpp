/**
 * @file GameSpatialSubsystemTest.cpp
 * @brief UGameSpatialSubsystem 世界空间索引子系统全路径、分层迁移、静止早退、流式原语与失效清理单元测试
 */

#include "Subsystem/UGameSpatialSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include <limits>
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Tests/SpatialTestHelpers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemLifecycleAndLayerTest,
    "Game.Spatial.Subsystem.LifecycleAndLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证空间子系统注册、同层静止早退、跨层迁移、批量操作、未注册探查与图层清空隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemLifecycleAndLayerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = Game::Spatial::TestHelpers::GetTestWorld();
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
    TestEqual(TEXT("默认原生标签名称精确匹配"), UGameSpatialSubsystem::GetDefaultLayerTag().ToString(), FString(TEXT("Spatial.Layer.Default")));
    TestEqual(TEXT("清空后总数为 0"), Subsystem->GetTotalActorCount(), 0);

    TestFalse(TEXT("空指针注册被拒绝"), Subsystem->RegisterActor(nullptr));
    TestFalse(TEXT("空指针注销返回 false"), Subsystem->UnregisterActor(nullptr));
    TestFalse(TEXT("空指针位置更新返回 false"), Subsystem->UpdateActorLocation(nullptr, FVector::ZeroVector));
    TestFalse(TEXT("空指针自动更新返回 false"), Subsystem->UpdateActor(nullptr));
    TestFalse(TEXT("空指针 IsActorRegistered 返回 false"), Subsystem->IsActorRegistered(nullptr));
    TestFalse(TEXT("空指针 GetActorLayer 返回空标签"), Subsystem->GetActorLayer(nullptr).IsValid());

    FVector OutLoc(99.0, 99.0, 99.0);
    TestFalse(TEXT("空指针 GetActorLocation 返回 false"), Subsystem->GetActorLocation(nullptr, OutLoc));
    TestEqual(TEXT("空指针 GetActorLocation 重置输出为零向量"), OutLoc, FVector::ZeroVector);

    AActor* ActorA = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(100.0, 0.0, 0.0));
    AActor* ActorB = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(0.0, 100.0, 0.0));
    TestNotNull(TEXT("ActorA 创建成功"), ActorA);
    TestNotNull(TEXT("ActorB 创建成功"), ActorB);
    if (ActorA == nullptr || ActorB == nullptr) {
        return false;
    }

    const FGameplayTag EnemyLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Spatial.Layer.Enemy"), TEXT("测试敌对图层"));
    const FGameplayTag AllyLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Spatial.Layer.Ally"), TEXT("测试友军图层"));

    TestFalse(TEXT("未注册存活 Actor IsActorRegistered 返回 false"), Subsystem->IsActorRegistered(ActorA));
    TestFalse(TEXT("未注册 Actor 注销返回 false"), Subsystem->UnregisterActor(ActorA));
    TestFalse(TEXT("未注册 Actor 更新返回 false"), Subsystem->UpdateActor(ActorA));
    TestFalse(TEXT("未注册 Actor 反查图层返回无效标签"), Subsystem->GetActorLayer(ActorA).IsValid());
    OutLoc = FVector(77.0, 77.0, 77.0);
    TestFalse(TEXT("未注册存活 Actor GetActorLocation 返回 false"), Subsystem->GetActorLocation(ActorA, OutLoc));
    TestEqual(TEXT("未注册存活 Actor GetActorLocation 重置输出为零向量"), OutLoc, FVector::ZeroVector);

    TestTrue(TEXT("注册 ActorA 至默认层"), Subsystem->RegisterActor(ActorA));
    TestTrue(TEXT("同层未移动重复注册命中静止早退"), Subsystem->RegisterActor(ActorA));
    TestTrue(TEXT("注册 ActorB 至敌对层"), Subsystem->RegisterActor(ActorB, EnemyLayer));
    TestEqual(TEXT("ActorA 归属默认层"), Subsystem->GetActorLayer(ActorA), UGameSpatialSubsystem::GetDefaultLayerTag());
    TestEqual(TEXT("ActorB 归属敌对层"), Subsystem->GetActorLayer(ActorB), EnemyLayer);

    ActorA->SetActorLocation(FVector(250.0, 0.0, 0.0));
    TestTrue(TEXT("同层移动后重注册原地更新位置"), Subsystem->RegisterActor(ActorA));
    TestTrue(TEXT("获取同层重注册后坐标"), Subsystem->GetActorLocation(ActorA, OutLoc));
    TestEqual(TEXT("同层重注册后 X=250"), OutLoc.X, 250.0);
    TestEqual(TEXT("同层重注册总数不变"), Subsystem->GetTotalActorCount(), 2);

    TestTrue(TEXT("跨层重注册将 ActorA 从默认层迁移至友军层"), Subsystem->RegisterActor(ActorA, AllyLayer));
    TestEqual(TEXT("原默认层数量归零"), Subsystem->GetActorCount(), 0);
    TestEqual(TEXT("新友军层数量为 1"), Subsystem->GetActorCount(AllyLayer), 1);
    TestEqual(TEXT("ActorA 图层已更新为友军层"), Subsystem->GetActorLayer(ActorA), AllyLayer);
    TestEqual(TEXT("跨层迁移后总数仍为 2"), Subsystem->GetTotalActorCount(), 2);

    const double NaNVal = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN 坐标更新被拒绝"), Subsystem->UpdateActorLocation(ActorA, FVector(NaNVal, 0.0, 0.0)));
    TestTrue(TEXT("NaN 拒绝后保留旧坐标"), Subsystem->GetActorLocation(ActorA, OutLoc));
    TestEqual(TEXT("旧坐标 X 未被污染"), OutLoc.X, 250.0);

    TestTrue(TEXT("静止坐标 UpdateActorLocation 早退成功"), Subsystem->UpdateActorLocation(ActorA, FVector(250.0, 0.0, 0.0)));
    TestTrue(TEXT("手动更新 ActorA 坐标"), Subsystem->UpdateActorLocation(ActorA, FVector(300.0, 0.0, 0.0)));
    TestTrue(TEXT("自动同步 ActorB 坐标"), Subsystem->UpdateActor(ActorB));

    Subsystem->ClearLayer(AllyLayer);
    TestEqual(TEXT("清空友军层后友军层为 0"), Subsystem->GetActorCount(AllyLayer), 0);
    TestEqual(TEXT("敌对层不受影响仍为 1"), Subsystem->GetActorCount(EnemyLayer), 1);
    TestEqual(TEXT("总数为 1"), Subsystem->GetTotalActorCount(), 1);
    Subsystem->ClearLayer(AllyLayer);

    TArray<AActor*> BatchList = {ActorA, nullptr, ActorB};
    TestEqual(TEXT("批量注册混合数组返回成功数 2"), Subsystem->RegisterActors(BatchList, EnemyLayer), 2);
    TestEqual(TEXT("批量注销混合数组返回成功数 2"), Subsystem->UnregisterActors(BatchList), 2);
    TestEqual(TEXT("批量注销后总数归零"), Subsystem->GetTotalActorCount(), 0);

    ActorA->Destroy();
    ActorB->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemQueriesAndStreamingTest,
    "Game.Spatial.Subsystem.QueriesAndStreaming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证子系统全部六类几何检索、超距无命中返回、C++ 零分配流式原语及空图层隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemQueriesAndStreamingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = Game::Spatial::TestHelpers::GetTestWorld();
    if (World == nullptr) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    if (Subsystem == nullptr) {
        return false;
    }

    Subsystem->ClearAll();

    AActor* Actor1 = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(100.0, 0.0, 0.0));
    AActor* Actor2 = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(70.0, 70.0, 0.0));
    AActor* Actor3 = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(0.0, 200.0, 0.0));
    AActor* Actor4 = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(0.0, 0.0, 300.0));

    Subsystem->RegisterActors({Actor1, Actor2, Actor3, Actor4});

    TArray<AActor*> Results;
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 150.0f, Results);
    TestEqual(TEXT("默认层球形查询命中 2 个"), Results.Num(), 2);
    TestTrue(TEXT("含 Actor1"), Results.Contains(Actor1));
    TestTrue(TEXT("含 Actor2"), Results.Contains(Actor2));

    Subsystem->QueryActorsInCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 30.0f, Results);
    TestEqual(TEXT("默认层 30度锥体仅命中 Actor1"), Results.Num(), 1);
    if (!Results.IsEmpty()) {
        TestEqual(TEXT("命中 Actor1"), Results[0], Actor1);
    }

    Subsystem->QueryActorsInCylinder(FVector::ZeroVector, 150.0f, 400.0f, Results);
    TestTrue(TEXT("默认层圆柱包含 Actor1"), Results.Contains(Actor1));
    TestTrue(TEXT("默认层圆柱包含高处 Actor4"), Results.Contains(Actor4));
    TestFalse(TEXT("默认层圆柱不含远端 Actor3"), Results.Contains(Actor3));

    Subsystem->QueryActorsInAABB(FBox(FVector(-10.0, -10.0, -10.0), FVector(120.0, 10.0, 10.0)), Results);
    TestEqual(TEXT("默认层 AABB 仅框中 Actor1"), Results.Num(), 1);

    AActor* NearestActor = nullptr;
    TestTrue(TEXT("默认层最近邻成功"), Subsystem->QueryNearestActor(FVector::ZeroVector, 500.0f, NearestActor));
    TestEqual(TEXT("最近为 Actor2 (距离 ~98.99)"), NearestActor, Actor2);

    NearestActor = Actor1;
    TestFalse(TEXT("非空图层超距 QueryNearestActor 返回 false"), Subsystem->QueryNearestActor(FVector(5000.0, 5000.0, 0.0), 100.0f, NearestActor));
    TestNull(TEXT("非空图层超距 QueryNearestActor 将输出置空"), NearestActor);

    Subsystem->QueryKNearestActors(FVector::ZeroVector, 500.0f, 3, Results);
    TestEqual(TEXT("默认层 K=3 返回 3 个"), Results.Num(), 3);
    if (Results.Num() == 3) {
        TestEqual(TEXT("第 1 近 Actor2"), Results[0], Actor2);
        TestEqual(TEXT("第 2 近 Actor1"), Results[1], Actor1);
        TestEqual(TEXT("第 3 近 Actor3"), Results[2], Actor3);
    }

    Subsystem->QueryKNearestActors(FVector::ZeroVector, 500.0f, 0, Results);
    TestTrue(TEXT("K<=0 安全返回空"), Results.IsEmpty());

    Subsystem->QueryKNearestActors(FVector::ZeroVector, -50.0f, 100, Results);
    TestTrue(TEXT("负半径 QueryKNearestActors 安全返回空"), Results.IsEmpty());

    Subsystem->QueryKNearestActors(FVector::ZeroVector, 500.0f, 100, Results);
    TestEqual(TEXT("大 K(100) 仅返回实际命中的 4 个 Actor"), Results.Num(), 4);

    int32 StreamRadiusCount = 0;
    Subsystem->ForEachActorInRadius(FVector::ZeroVector, 150.0f, [&StreamRadiusCount](AActor*, const FVector&) { ++StreamRadiusCount; });
    TestEqual(TEXT("ForEachActorInRadius 流式命中 2 个"), StreamRadiusCount, 2);

    int32 StreamConeCount = 0;
    Subsystem->ForEachActorInCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 150.0f, 30.0f, [&StreamConeCount](AActor*) { ++StreamConeCount; });
    TestEqual(TEXT("ForEachActorInCone 流式命中 1 个"), StreamConeCount, 1);

    int32 StreamCylCount = 0;
    Subsystem->ForEachActorInCylinder(FVector::ZeroVector, 150.0f, 400.0f, [&StreamCylCount](AActor*) { ++StreamCylCount; });
    TestEqual(TEXT("ForEachActorInCylinder 流式命中 3 个 (Actor1, Actor2, Actor4)"), StreamCylCount, 3);

    int32 StreamAabbCount = 0;
    Subsystem->ForEachActorInAABB(FBox(FVector(-10.0, -10.0, -10.0), FVector(120.0, 10.0, 10.0)), [&StreamAabbCount](AActor*) { ++StreamAabbCount; });
    TestEqual(TEXT("ForEachActorInAABB 流式命中 1 个"), StreamAabbCount, 1);

    Subsystem->GetActorsInLayer(Results);
    TestEqual(TEXT("GetActorsInLayer 导出默认层全部 4 个存活 Actor"), Results.Num(), 4);

    const FGameplayTag EmptyLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Spatial.Layer.EmptyTest"), TEXT("空隔离层"));
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 1000.0f, Results, EmptyLayer);
    TestTrue(TEXT("未注册图层查询返回空"), Results.IsEmpty());
    TestFalse(TEXT("未注册图层近邻查询返回 false"), Subsystem->QueryNearestActor(FVector::ZeroVector, 1000.0f, NearestActor, EmptyLayer));
    Subsystem->QueryKNearestActors(FVector::ZeroVector, 1000.0f, 3, Results, EmptyLayer);
    TestTrue(TEXT("未注册图层 K 近邻返回空"), Results.IsEmpty());
    Subsystem->GetActorsInLayer(Results, EmptyLayer);
    TestTrue(TEXT("未注册图层 GetActorsInLayer 返回空"), Results.IsEmpty());

    int32 EmptyStreamCalls = 0;
    Subsystem->ForEachActorInRadius(FVector::ZeroVector, 1000.0f, [&EmptyStreamCalls](AActor*) { ++EmptyStreamCalls; }, EmptyLayer);
    Subsystem->ForEachActorInCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 1000.0f, 45.0f, [&EmptyStreamCalls](AActor*) { ++EmptyStreamCalls; }, EmptyLayer);
    Subsystem->ForEachActorInCylinder(FVector::ZeroVector, 1000.0f, 500.0f, [&EmptyStreamCalls](AActor*) { ++EmptyStreamCalls; }, EmptyLayer);
    Subsystem->ForEachActorInAABB(FBox(FVector(-500.0), FVector(500.0)), [&EmptyStreamCalls](AActor*) { ++EmptyStreamCalls; }, EmptyLayer);
    TestEqual(TEXT("未注册图层全部四类 ForEach 流式查询零回调安全早退"), EmptyStreamCalls, 0);

    Subsystem->ClearAll();
    Actor1->Destroy();
    Actor2->Destroy();
    Actor3->Destroy();
    Actor4->Destroy();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameSpatialSubsystemWeakPtrAndPruningTest,
    "Game.Spatial.Subsystem.WeakPtrAndPruning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证已销毁/销毁窗口期 Actor 的全几何排除一致性、EndPlay 反注册放行与 O(Layer)/全局失效清理
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameSpatialSubsystemWeakPtrAndPruningTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = Game::Spatial::TestHelpers::GetTestWorld();
    if (World == nullptr) {
        return false;
    }

    UGameSpatialSubsystem* Subsystem = World->GetSubsystem<UGameSpatialSubsystem>();
    if (Subsystem == nullptr) {
        return false;
    }

    Subsystem->ClearAll();

    const FGameplayTag PruneLayer = UGameplayTagsManager::Get().AddNativeGameplayTag(FName("Spatial.Layer.PruneTest"), TEXT("清理测试层"));

    AActor* LiveActor = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(100.0, 0.0, 0.0));
    AActor* DeadDefault = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(50.0, 0.0, 0.0));
    AActor* DeadOtherLayer = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(30.0, 0.0, 0.0));
    AActor* DeadUnregisterTest = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(20.0, 0.0, 0.0));

    Subsystem->RegisterActor(LiveActor);
    Subsystem->RegisterActor(DeadDefault);
    Subsystem->RegisterActor(DeadUnregisterTest);
    Subsystem->RegisterActor(DeadOtherLayer, PruneLayer);

    TestEqual(TEXT("全员存活时单层清理返回 0"), Subsystem->PruneStaleActors(), 0);
    TestEqual(TEXT("全员存活时全局清理返回 0"), Subsystem->PruneAllStaleActors(), 0);

    DeadDefault->Destroy();
    DeadOtherLayer->Destroy();
    DeadUnregisterTest->Destroy();

    TestFalse(TEXT("已销毁 Actor 的 IsActorRegistered 必须返回 false"), Subsystem->IsActorRegistered(DeadDefault));
    TestFalse(TEXT("已销毁 Actor 的 GetActorLayer 必须返回空标签"), Subsystem->GetActorLayer(DeadDefault).IsValid());
    FVector DeadLoc(1.0, 1.0, 1.0);
    TestFalse(TEXT("已销毁 Actor 的 GetActorLocation 必须返回 false"), Subsystem->GetActorLocation(DeadDefault, DeadLoc));
    TestEqual(TEXT("已销毁 Actor 的 GetActorLocation 输出重置为零"), DeadLoc, FVector::ZeroVector);
    TestFalse(TEXT("已销毁 Actor 禁止再次注册"), Subsystem->RegisterActor(DeadDefault));
    TestFalse(TEXT("已销毁 Actor 禁止位置更新"), Subsystem->UpdateActor(DeadDefault));

    TestTrue(TEXT("已销毁 Actor 允许通过 UnregisterActor 正常反注册（兼容 EndPlay）"), Subsystem->UnregisterActor(DeadUnregisterTest));

    TArray<AActor*> QueryResults;
    Subsystem->QueryActorsInRadius(FVector::ZeroVector, 200.0f, QueryResults);
    TestEqual(TEXT("球形查询自动排除已销毁 Actor，仅返回 LiveActor"), QueryResults.Num(), 1);
    if (!QueryResults.IsEmpty()) {
        TestEqual(TEXT("返回项为 LiveActor"), QueryResults[0], LiveActor);
    }

    Subsystem->QueryActorsInCone(FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 200.0f, 45.0f, QueryResults);
    TestEqual(TEXT("锥体查询自动排除已销毁 Actor，仅返回 LiveActor"), QueryResults.Num(), 1);

    Subsystem->QueryActorsInCylinder(FVector::ZeroVector, 200.0f, 100.0f, QueryResults);
    TestEqual(TEXT("圆柱查询自动排除已销毁 Actor，仅返回 LiveActor"), QueryResults.Num(), 1);

    Subsystem->QueryActorsInAABB(FBox(FVector(-10.0, -10.0, -10.0), FVector(150.0, 10.0, 10.0)), QueryResults);
    TestEqual(TEXT("AABB 查询自动排除已销毁 Actor，仅返回 LiveActor"), QueryResults.Num(), 1);

    AActor* Nearest = nullptr;
    TestTrue(TEXT("近邻查询跳过更近的已销毁 Actor，命中 LiveActor"), Subsystem->QueryNearestActor(FVector::ZeroVector, 200.0f, Nearest));
    TestEqual(TEXT("最近存活目标为 LiveActor"), Nearest, LiveActor);

    Subsystem->QueryKNearestActors(FVector::ZeroVector, 200.0f, 3, QueryResults);
    TestEqual(TEXT("K 近邻仅返回 1 个存活 Actor"), QueryResults.Num(), 1);

    Subsystem->GetActorsInLayer(QueryResults);
    TestEqual(TEXT("GetActorsInLayer 仅返回 1 个存活 Actor"), QueryResults.Num(), 1);

    const int32 DefaultPruned = Subsystem->PruneStaleActors();
    TestEqual(TEXT("单层清理仅清理默认层 1 个失效 Actor"), DefaultPruned, 1);
    TestEqual(TEXT("默认层剩余 1 个存活 Actor"), Subsystem->GetActorCount(), 1);
    TestEqual(TEXT("另一层失效 Actor 未被误清"), Subsystem->GetActorCount(PruneLayer), 1);

    const int32 GlobalPruned = Subsystem->PruneAllStaleActors();
    TestEqual(TEXT("全局清理回收跨层残留 1 个失效 Actor"), GlobalPruned, 1);
    TestEqual(TEXT("跨层全部清空后另一层计数为 0"), Subsystem->GetActorCount(PruneLayer), 0);
    TestEqual(TEXT("全局总数精确剩余 1"), Subsystem->GetTotalActorCount(), 1);

    TestEqual(TEXT("清理不存在图层返回 0"), Subsystem->PruneStaleActors(PruneLayer), 0);

    AActor* DyingActor = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(40.0, 0.0, 0.0));
    TestNotNull(TEXT("DyingActor 创建成功"), DyingActor);
    if (DyingActor == nullptr) {
        Subsystem->ClearAll();
        LiveActor->Destroy();
        return false;
    }

    TestTrue(TEXT("注册 DyingActor 到默认层"), Subsystem->RegisterActor(DyingActor));
    bool bObservedDestroyWindow = false;
    bool bWeakStillValidInWindow = false;
    bool bRadiusExcludedDying = false;
    bool bStreamExcludedDying = false;
    bool bNearestExcludedDying = false;
    bool bKNearestExcludedDying = false;
    bool bLayerListExcludedDying = false;
    int32 PrunedInWindow = 0;

    const FDelegateHandle DestroyedHandle = World->AddOnActorDestroyedHandler(
        FOnActorDestroyed::FDelegate::CreateLambda([&](AActor* DestroyedActor) {
            if (DestroyedActor != DyingActor) {
                return;
            }
            bObservedDestroyWindow = DestroyedActor->IsActorBeingDestroyed();
            bWeakStillValidInWindow = TWeakObjectPtr<AActor>(DestroyedActor).IsValid();

            TArray<AActor*> WindowResults;
            Subsystem->QueryActorsInRadius(FVector::ZeroVector, 200.0f, WindowResults);
            bRadiusExcludedDying = (WindowResults.Num() == 1 && WindowResults[0] == LiveActor);

            int32 StreamHits = 0;
            bool bStreamHitDying = false;
            Subsystem->ForEachActorInRadius(FVector::ZeroVector, 200.0f, [&](AActor* HitActor) {
                ++StreamHits;
                bStreamHitDying = bStreamHitDying || (HitActor == DyingActor);
            });
            bStreamExcludedDying = (StreamHits == 1 && !bStreamHitDying);

            AActor* WindowNearest = nullptr;
            bNearestExcludedDying = Subsystem->QueryNearestActor(FVector::ZeroVector, 200.0f, WindowNearest)
                && (WindowNearest == LiveActor);

            Subsystem->QueryKNearestActors(FVector::ZeroVector, 200.0f, 3, WindowResults);
            bKNearestExcludedDying = (WindowResults.Num() == 1 && WindowResults[0] == LiveActor);

            Subsystem->GetActorsInLayer(WindowResults);
            bLayerListExcludedDying = (WindowResults.Num() == 1 && WindowResults[0] == LiveActor);

            PrunedInWindow = Subsystem->PruneStaleActors();
        })
    );

    DyingActor->Destroy();
#if UE_VERSION_NEWER_THAN(5, 4, 99)
    World->RemoveOnActorDestroyedHandler(DestroyedHandle);
#else
    World->RemoveOnActorDestroyededHandler(DestroyedHandle);
#endif

    TestTrue(TEXT("OnActorDestroyed 窗口期内 IsActorBeingDestroyed 为 true"), bObservedDestroyWindow);
    TestTrue(TEXT("OnActorDestroyed 窗口期内 WeakObjectPtr 尚未 MarkAsGarbage"), bWeakStillValidInWindow);
    TestTrue(TEXT("销毁窗口期内 QueryActorsInRadius 排除正在销毁的 Actor"), bRadiusExcludedDying);
    TestTrue(TEXT("销毁窗口期内 ForEachActorInRadius 排除正在销毁的 Actor"), bStreamExcludedDying);
    TestTrue(TEXT("销毁窗口期内 QueryNearestActor 排除更近的正在销毁 Actor"), bNearestExcludedDying);
    TestTrue(TEXT("销毁窗口期内 QueryKNearestActors 排除正在销毁的 Actor"), bKNearestExcludedDying);
    TestTrue(TEXT("销毁窗口期内 GetActorsInLayer 排除正在销毁的 Actor"), bLayerListExcludedDying);
    TestEqual(TEXT("销毁窗口期内 PruneStaleActors 一致清理 1 个销毁中 Actor"), PrunedInWindow, 1);

    Subsystem->ClearAll();
    LiveActor->Destroy();

    constexpr int32 BatchSize = 32;
    TArray<AActor*> DefaultBatch;
    TArray<AActor*> OtherBatch;
    DefaultBatch.Reserve(BatchSize);
    OtherBatch.Reserve(BatchSize);
    for (int32 Index = 0; Index < BatchSize; ++Index) {
        AActor* DefaultItem = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(10.0, 10.0, 0.0));
        AActor* OtherItem = Game::Spatial::TestHelpers::SpawnTestActor(World, FVector(10.0, 10.0, 0.0));
        DefaultBatch.Add(DefaultItem);
        OtherBatch.Add(OtherItem);
        Subsystem->RegisterActor(DefaultItem);
        Subsystem->RegisterActor(OtherItem, PruneLayer);
    }

    for (AActor* Actor : DefaultBatch) {
        Actor->Destroy();
    }
    for (AActor* Actor : OtherBatch) {
        Actor->Destroy();
    }

    for (int32 Index = 0; Index < BatchSize / 2; ++Index) {
        TestTrue(
            TEXT("大量失效 Actor 哈希桶碰撞场景下按对象身份精准反注册"),
            Subsystem->UnregisterActor(DefaultBatch[Index])
        );
    }
    TestEqual(TEXT("默认层剩余恰好一半未反注册的失效条目"), Subsystem->GetActorCount(), BatchSize / 2);
    TestEqual(TEXT("另一层失效条目数量完全不受哈希碰撞误删影响"), Subsystem->GetActorCount(PruneLayer), BatchSize);
    TestEqual(TEXT("按层清理仅清理默认层剩余失效条目"), Subsystem->PruneStaleActors(), BatchSize / 2);
    TestEqual(TEXT("另一层失效条目依然完整保留待清理"), Subsystem->GetActorCount(PruneLayer), BatchSize);
    TestEqual(TEXT("全局清理精确回收另一层全部失效条目"), Subsystem->PruneAllStaleActors(), BatchSize);
    TestEqual(TEXT("全部清理后总条目归零"), Subsystem->GetTotalActorCount(), 0);

    return true;
}

#endif
