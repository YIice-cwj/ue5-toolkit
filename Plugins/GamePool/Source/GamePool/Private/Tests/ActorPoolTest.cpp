// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ObjectPoolSubsystem.h"

#include "Tests/GamePoolableTestActor.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

namespace {

/**
 * @brief 获取测试世界指针
 * @return 当前有效的 UWorld 指针，若不存在则返回 nullptr
 */
UWorld* GetTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolAcquireAndReleaseTest,
    "Game.Pool.AcquireAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池借出、休眠激活、复用已有实例及 IGamePoolable 回调
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolAcquireAndReleaseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());

    const FTransform SpawnTransform(FVector(100.0f, 200.0f, 300.0f));
    const TGameStatusResult<AGamePoolableTestActor*> FirstActorRes = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        SpawnTransform
    );
    TestTrue(TEXT("借出成功"), FirstActorRes.IsSuccess());
    AGamePoolableTestActor* FirstActor = FirstActorRes.GetValue();
    TestNotNull(TEXT("借出实例非空"), FirstActor);
    TestFalse(TEXT("借出后在游戏中可见"), FirstActor->IsHidden());
    TestEqual(TEXT("首次借出触发 OnAcquireFromPool 回调"), FirstActor->AcquireCount, 1);
    TestEqual(TEXT("初始位置变换设置正确"), FirstActor->GetActorLocation(), SpawnTransform.GetLocation());

    const FPoolStats Stats1 = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("当前活跃数为 1"), Stats1.ActiveCount, 1);
    TestEqual(TEXT("当前空闲数为 0"), Stats1.IdleCount, 0);

    FirstActor->CustomTimeDilation = 0.01f;

    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FirstActor).IsSuccess());
    TestTrue(TEXT("归还后隐藏"), FirstActor->IsHidden());
    TestEqual(TEXT("归还触发 OnReleaseToPool 回调"), FirstActor->ReleaseCount, 1);

    const FPoolStats Stats2 = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("归还后活跃数为 0"), Stats2.ActiveCount, 0);
    TestEqual(TEXT("归还后空闲数为 1"), Stats2.IdleCount, 1);

    const FTransform SecondTransform(FVector(500.0f, 600.0f, 700.0f));
    const TGameStatusResult<AGamePoolableTestActor*> ReusedActorRes = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        SecondTransform
    );
    TestTrue(TEXT("复用借出成功"), ReusedActorRes.IsSuccess());
    AGamePoolableTestActor* ReusedActor = ReusedActorRes.GetValue();
    TestEqual(TEXT("复用同一个实例指针"), ReusedActor, FirstActor);
    TestFalse(TEXT("复用后重新可见"), ReusedActor->IsHidden());
    TestEqual(TEXT("复用后 CustomTimeDilation 还原为默认值"), ReusedActor->CustomTimeDilation, 1.0f);
    TestEqual(TEXT("再次借出触发 OnAcquireFromPool 回调"), ReusedActor->AcquireCount, 2);
    TestEqual(TEXT("新位置变换更新正确"), ReusedActor->GetActorLocation(), SecondTransform.GetLocation());

    TestTrue(TEXT("再次归还成功"), Pool->ReleaseActor(ReusedActor).IsSuccess());
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPrewarmAndClearTest,
    "Game.Pool.PrewarmAndClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池预热生成与清空清理机制
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolPrewarmAndClearTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    Pool->SetMaxPoolCapacity(AGamePoolableTestActor::StaticClass(), 100);

    Pool->Prewarm(AGamePoolableTestActor::StaticClass(), 3);

    const FPoolStats PrewarmStats = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("预热后空闲数为 3"), PrewarmStats.IdleCount, 3);
    TestEqual(TEXT("预热后活跃数为 0"), PrewarmStats.ActiveCount, 0);

    Pool->SetMaxPoolCapacity(AGamePoolableTestActor::StaticClass(), 1);
    const FPoolStats ClampedStats = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("缩减容量上限后立即裁剪多余空闲实例"), ClampedStats.IdleCount, 1);
    Pool->SetMaxPoolCapacity(AGamePoolableTestActor::StaticClass(), 100);

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    const FPoolStats ClearedStats = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("清空后空闲数为 0"), ClearedStats.IdleCount, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolExternalDestroyAutoHealingTest,
    "Game.Pool.ExternalDestroyAutoHealing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证活跃 Actor 被外部强制 DestroyActor 时，对象池活跃数自动自愈扣减
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolExternalDestroyAutoHealingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());

    const TGameStatusResult<AGamePoolableTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        FTransform::Identity
    );
    TestTrue(TEXT("借出成功"), ActorRes.IsSuccess());
    AGamePoolableTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("借出实例有效"), Actor);

    const FPoolStats StatsBefore = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("借出后活跃数为 1"), StatsBefore.ActiveCount, 1);

    Actor->Destroy();

    const FPoolStats StatsAfter = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("外部销毁后活跃数自愈归零"), StatsAfter.ActiveCount, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolExternalReleaseTest,
    "Game.Pool.ExternalRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证未从池中借出的 Actor 不会被外部归还污染空闲池与统计
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolExternalReleaseTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    AGamePoolableTestActor* ExternalActor = World->SpawnActor<AGamePoolableTestActor>();
    TestNotNull(TEXT("外部 Actor 生成成功"), ExternalActor);
    if (!ExternalActor) {
        return false;
    }

    TestTrue(TEXT("外部归还返回失败"), Pool->ReleaseActor(ExternalActor).IsFailure());
    TestFalse(TEXT("外部归还不休眠非池属 Actor"), ExternalActor->IsHidden());
    TestEqual(TEXT("外部归还不触发生命周期回调"), ExternalActor->ReleaseCount, 0);

    const FPoolStats ExtStats = Pool->GetPoolStats(AGamePoolableTestActor::StaticClass());
    TestEqual(TEXT("外部归还后活跃数为零"), ExtStats.ActiveCount, 0);
    TestEqual(TEXT("外部归还后空闲数为零"), ExtStats.IdleCount, 0);
    TestEqual(TEXT("外部归还后实例总数为零"), ExtStats.TotalCount, 0);

    ExternalActor->Destroy();
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolReleaseOnAcquireTest,
    "Game.Pool.ReleaseOnAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 OnAcquire 内归还自身不会形成活跃与空闲双重归属或伪成功借出
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolReleaseOnAcquireTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableReleaseOnAcquireTestActor::StaticClass();
    Pool->ClearPool(ActorClass);
    const TGameStatusResult<AGamePoolableReleaseOnAcquireTestActor*> ActorRes =
        Pool->AcquireActor<AGamePoolableReleaseOnAcquireTestActor>(ActorClass);
    TestTrue(TEXT("首次借出成功"), ActorRes.IsSuccess());
    AGamePoolableReleaseOnAcquireTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("首次借出用于安装重入回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [Pool](AGamePoolableTestActor* InActor) {
        Pool->ReleaseActor(InActor);
    };
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(Actor).IsSuccess());

    const TGameStatusResult<AGamePoolableReleaseOnAcquireTestActor*> ReenteredActorRes =
        Pool->AcquireActor<AGamePoolableReleaseOnAcquireTestActor>(ActorClass);
    TestTrue(TEXT("回调内已归还导致借出失败"), ReenteredActorRes.IsFailure());

    const FPoolStats ReenteredStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("自归还后活跃数为零"), ReenteredStats.ActiveCount, 0);
    TestEqual(TEXT("自归还后仅保留一个空闲实例"), ReenteredStats.IdleCount, 1);
    TestEqual(TEXT("自归还后实例无双重计数"), ReenteredStats.TotalCount, 1);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolDestroyOnAcquireTest,
    "Game.Pool.DestroyOnAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 OnAcquire 内销毁自身会撤销活跃归属且借出返回空
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolDestroyOnAcquireTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableDestroyOnAcquireTestActor::StaticClass();
    Pool->ClearPool(ActorClass);
    const TGameStatusResult<AGamePoolableDestroyOnAcquireTestActor*> ActorRes =
        Pool->AcquireActor<AGamePoolableDestroyOnAcquireTestActor>(ActorClass);
    TestTrue(TEXT("首次借出成功"), ActorRes.IsSuccess());
    AGamePoolableDestroyOnAcquireTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("首次借出用于安装销毁回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [](AGamePoolableTestActor* InActor) {
        InActor->Destroy();
    };
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(Actor).IsSuccess());

    const TGameStatusResult<AGamePoolableDestroyOnAcquireTestActor*> ReenteredActorRes =
        Pool->AcquireActor<AGamePoolableDestroyOnAcquireTestActor>(ActorClass);
    TestTrue(TEXT("回调内已销毁导致借出失败"), ReenteredActorRes.IsFailure());

    const FPoolStats DestroyedStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("自销毁后活跃数为零"), DestroyedStats.ActiveCount, 0);
    TestEqual(TEXT("自销毁后空闲数为零"), DestroyedStats.IdleCount, 0);
    TestEqual(TEXT("自销毁后实例总数为零"), DestroyedStats.TotalCount, 0);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPayloadAndDetachmentTest,
    "Game.Pool.PayloadAndDetachment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池带参借出注入载荷以及归还时自动解除子 Actor 挂接
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolPayloadAndDetachmentTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());

    const int32 CustomPayload = 999;
    const TGameStatusResult<AGamePoolableTestActor*> ActorWithPayloadRes = Pool->AcquireActorWithPayload<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        FTransform::Identity,
        CustomPayload
    );
    TestTrue(TEXT("借出 Actor 成功"), ActorWithPayloadRes.IsSuccess());
    AGamePoolableTestActor* ActorWithPayload = ActorWithPayloadRes.GetValue();
    TestNotNull(TEXT("借出 Actor 实例非空"), ActorWithPayload);
    TestEqual(TEXT("载荷值成功传递至 Actor"), ActorWithPayload->LastReceivedPayloadValue, 999);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AGamePoolableTestActor* ChildActor = World->SpawnActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        SpawnParams
    );
    TestNotNull(TEXT("子 Actor 生成成功"), ChildActor);

    ChildActor->AttachToActor(ActorWithPayload, FAttachmentTransformRules::KeepWorldTransform);
    TestEqual<AActor*>(TEXT("挂接后子 Actor 的父节点为 ActorWithPayload"), ChildActor->GetAttachParentActor(), ActorWithPayload);

    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(ActorWithPayload).IsSuccess());
    TestNull(TEXT("ReleaseActor 后子 Actor 自动脱钩父节点，Parent 为空"), ChildActor->GetAttachParentActor());

    ChildActor->Destroy();
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
