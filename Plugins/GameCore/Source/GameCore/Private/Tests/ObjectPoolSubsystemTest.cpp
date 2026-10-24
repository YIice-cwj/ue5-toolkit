// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pool/ObjectPoolSubsystem.h"
#include "Time/TimeDilationSubsystem.h"
#include "Tests/GamePoolableTestActor.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

namespace {

UWorld* GetTestWorld() {
    for (const FWorldContext& Context : GEngine->GetWorldContexts()) {
        if (Context.World()) {
            return Context.World();
        }
    }
    return nullptr;
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolAcquireAndReleaseTest, "Game.Core.Pool.AcquireAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证对象池借出、休眠激活、复用已有实例及 IGamePoolable 回调
 *
 * 测试步骤：
 * 1. 首次借出：池中为空时自动 SpawnActor，校验可见性与初始变换，校验 OnAcquireFromPool 回调；
 * 2. 归还至池：校验隐藏休眠状态与 OnReleaseToPool 回调，以及统计数量变动；
 * 3. 再次借出：校验复用原实例指针且不产生新 Spawn，位置更新并再次触发激活回调。
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
    AGamePoolableTestActor* FirstActor = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        SpawnTransform
    );
    TestNotNull(TEXT("借出实例非空"), FirstActor);
    TestFalse(TEXT("借出后在游戏中可见"), FirstActor->IsHidden());
    TestEqual(TEXT("首次借出触发 OnAcquireFromPool 回调"), FirstActor->AcquireCount, 1);
    TestEqual(TEXT("初始位置变换设置正确"), FirstActor->GetActorLocation(), SpawnTransform.GetLocation());

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("当前活跃数为 1"), Active, 1);
    TestEqual(TEXT("当前空闲数为 0"), Idle, 0);

    UTimeDilationSubsystem* TimeSubsystem = World->GetSubsystem<UTimeDilationSubsystem>();
    if (TimeSubsystem != nullptr) {
        TimeSubsystem->RequestHitStop(FirstActor, 5.0f, 0.01f);
        TestTrue(TEXT("归还前 Actor 处于顿帧状态"), TimeSubsystem->IsActorInHitStop(FirstActor));
    }

    Pool->ReleaseActor(FirstActor);
    TestTrue(TEXT("归还后隐藏"), FirstActor->IsHidden());
    TestEqual(TEXT("归还触发 OnReleaseToPool 回调"), FirstActor->ReleaseCount, 1);
    if (TimeSubsystem != nullptr) {
        TestFalse(TEXT("归还入池时自动清理活跃顿帧"), TimeSubsystem->IsActorInHitStop(FirstActor));
    }

    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("归还后活跃数为 0"), Active, 0);
    TestEqual(TEXT("归还后空闲数为 1"), Idle, 1);

    const FTransform SecondTransform(FVector(500.0f, 600.0f, 700.0f));
    AGamePoolableTestActor* ReusedActor = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        SecondTransform
    );
    TestEqual(TEXT("复用同一个实例指针"), ReusedActor, FirstActor);
    TestFalse(TEXT("复用后重新可见"), ReusedActor->IsHidden());
    TestEqual(TEXT("复用后 CustomTimeDilation 还原为默认值"), ReusedActor->CustomTimeDilation, 1.0f);
    TestEqual(TEXT("再次借出触发 OnAcquireFromPool 回调"), ReusedActor->AcquireCount, 2);
    TestEqual(TEXT("新位置变换更新正确"), ReusedActor->GetActorLocation(), SecondTransform.GetLocation());

    Pool->ReleaseActor(ReusedActor);
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolPrewarmAndClearTest, "Game.Core.Pool.PrewarmAndClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证对象池预热生成与清空清理机制
 *
 * 测试步骤：
 * 1. 预热生成指定数量空闲实例并核对统计数量；
 * 2. 清空并销毁全部池内实例，核对统计归零。
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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("预热后空闲数为 3"), Idle, 3);
    TestEqual(TEXT("预热后活跃数为 0"), Active, 0);

    Pool->SetMaxPoolCapacity(AGamePoolableTestActor::StaticClass(), 1);
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("缩减容量上限后立即裁剪多余空闲实例"), Idle, 1);
    Pool->SetMaxPoolCapacity(AGamePoolableTestActor::StaticClass(), 100);

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("清空后空闲数为 0"), Idle, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolExternalDestroyAutoHealingTest, "Game.Core.Pool.ExternalDestroyAutoHealing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证活跃 Actor 被外部强制 DestroyActor 时，对象池活跃数自动自愈扣减
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

    AGamePoolableTestActor* Actor = Pool->AcquireActor<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        FTransform::Identity
    );
    TestNotNull(TEXT("借出实例有效"), Actor);

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("借出后活跃数为 1"), Active, 1);

    Actor->Destroy();

    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("外部销毁后活跃数自愈归零"), Active, 0);

    return true;
}

#endif // WITH_AUTOMATION_TESTS


