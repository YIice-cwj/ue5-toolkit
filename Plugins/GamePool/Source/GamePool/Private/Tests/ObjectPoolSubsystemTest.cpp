// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pool/ObjectPoolSubsystem.h"

#include "Particles/ParticleSystemComponent.h"
#include "Async/UTimeSlicedTaskSubsystem.h"
#include "Tests/GamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

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

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolAcquireAndReleaseTest, "Game.Pool.AcquireAndRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("当前活跃数为 1"), Active, 1);
    TestEqual(TEXT("当前空闲数为 0"), Idle, 0);

    FirstActor->CustomTimeDilation = 0.01f;

    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FirstActor).IsSuccess());
    TestTrue(TEXT("归还后隐藏"), FirstActor->IsHidden());
    TestEqual(TEXT("归还触发 OnReleaseToPool 回调"), FirstActor->ReleaseCount, 1);

    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("归还后活跃数为 0"), Active, 0);
    TestEqual(TEXT("归还后空闲数为 1"), Idle, 1);

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolPrewarmAndClearTest, "Game.Pool.PrewarmAndClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolExternalDestroyAutoHealingTest, "Game.Pool.ExternalDestroyAutoHealing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("借出后活跃数为 1"), Active, 1);

    Actor->Destroy();

    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("外部销毁后活跃数自愈归零"), Active, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolExternalReleaseTest, "Game.Pool.ExternalRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(AGamePoolableTestActor::StaticClass(), Total, Active, Idle);
    TestEqual(TEXT("外部归还后活跃数为零"), Active, 0);
    TestEqual(TEXT("外部归还后空闲数为零"), Idle, 0);
    TestEqual(TEXT("外部归还后实例总数为零"), Total, 0);

    ExternalActor->Destroy();
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolReleaseOnAcquireTest, "Game.Pool.ReleaseOnAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
    const TGameStatusResult<AGamePoolableReleaseOnAcquireTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableReleaseOnAcquireTestActor>(ActorClass);
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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("自归还后活跃数为零"), Active, 0);
    TestEqual(TEXT("自归还后仅保留一个空闲实例"), Idle, 1);
    TestEqual(TEXT("自归还后实例无双重计数"), Total, 1);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolDestroyOnAcquireTest, "Game.Pool.DestroyOnAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

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
    const TGameStatusResult<AGamePoolableDestroyOnAcquireTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableDestroyOnAcquireTestActor>(ActorClass);
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

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("自销毁后活跃数为零"), Active, 0);
    TestEqual(TEXT("自销毁后空闲数为零"), Idle, 0);
    TestEqual(TEXT("自销毁后实例总数为零"), Total, 0);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolResetFlagsAndTimeSliceTest, "Game.Pool.ResetFlagsAndTimeSlice",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证按位组件重置掩码（含 UFXSystemComponent 立即停用与 None 零遍历）及分帧渐进预热
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolResetFlagsAndTimeSliceTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = World->GetSubsystem<UObjectPoolSubsystem>();
    UTimeSlicedTaskSubsystem* TaskSubsystem = World->GetSubsystem<UTimeSlicedTaskSubsystem>();
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    TestNotNull(TEXT("时间切片子系统有效"), TaskSubsystem);
    if (!Pool || !TaskSubsystem) {
        return false;
    }

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);
    Pool->SetMaxPoolCapacity(ActorClass, 100);
    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);
    TestEqual(TEXT("默认或显式设置 ResetFlags 为 All"), Pool->GetPoolResetFlags(ActorClass), EPoolResetFlags::All);

    const TGameStatusResult<AGamePoolableTestActor*> FXActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出成功"), FXActorRes.IsSuccess());
    AGamePoolableTestActor* FXActor = FXActorRes.GetValue();
    TestNotNull(TEXT("借出携带 FX 组件的 Actor 有效"), FXActor);
    if (!FXActor) {
        return false;
    }

    UParticleSystemComponent* FXComp = NewObject<UParticleSystemComponent>(FXActor);
    FXComp->SetupAttachment(FXActor->GetRootComponent());
    FXComp->RegisterComponent();
    FXComp->SetActiveFlag(true);
    TestTrue(TEXT("归还前 FX 组件处于激活态"), FXComp->IsActive());

    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FXActor).IsSuccess());
    TestFalse(TEXT("EPoolResetFlags::All 归还时自动停用 UFXSystemComponent"), FXComp->IsActive());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::None);
    TestEqual(TEXT("ResetFlags 更新为 None"), Pool->GetPoolResetFlags(ActorClass), EPoolResetFlags::None);

    const TGameStatusResult<AGamePoolableTestActor*> FastActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出成功"), FastActorRes.IsSuccess());
    AGamePoolableTestActor* FastActor = FastActorRes.GetValue();
    TestEqual(TEXT("复用同一 Actor 实例"), FastActor, FXActor);
    FXComp->SetActiveFlag(true);
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FastActor).IsSuccess());
    TestTrue(TEXT("EPoolResetFlags::None 跳过组件遍历保留 FX 激活标志"), FXComp->IsActive());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);
    Pool->ClearPool(ActorClass);

    TaskSubsystem->SetFrameBudgetMilliseconds(50.0);
    FTimeSlicedTaskHandle InvalidPrewarm = Pool->PrewarmTimeSliced(nullptr, 5, 2);
    TestFalse(TEXT("空类分帧预热返回无效句柄"), InvalidPrewarm.IsActive());

    FTimeSlicedTaskHandle PrewarmHandle = Pool->PrewarmTimeSliced(ActorClass, 5, 2);
    TestTrue(TEXT("分帧预热任务句柄已激活"), PrewarmHandle.IsActive());

    int32 Total = 0, Active = 0, Idle = 0;
    int32 SafetyTicks = 0;
    while (PrewarmHandle.IsActive() && SafetyTicks < 10) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("分帧预热完成全部 5 个空闲实例"), Idle, 5);
    TestFalse(TEXT("分帧预热完成后句柄失活"), PrewarmHandle.IsActive());

    Pool->SetMaxPoolCapacity(ActorClass, 7);
    FTimeSlicedTaskHandle CapClampedHandle = Pool->PrewarmTimeSliced(ActorClass, 20, 2);
    SafetyTicks = 0;
    while (CapClampedHandle.IsActive() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("分帧预热触达最大空闲容量上限时自动短路停机"), Idle, 7);
    Pool->SetMaxPoolCapacity(ActorClass, 100);

    FTimeSlicedTaskHandle CanceledPrewarm = Pool->PrewarmTimeSliced(ActorClass, 4, 2);
    CanceledPrewarm.Cancel();
    TaskSubsystem->Tick(0.016f);
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("取消分帧预热后不再新增实例"), Idle, 7);

    Pool->ClearPool(ActorClass);
    TGameCoroTask<int32> InvalidAsyncPrewarm = Pool->PrewarmAsync(nullptr, 5, 2);
    TestTrue(TEXT("无效类参数协程预热立即完成"), InvalidAsyncPrewarm.IsDone());
    TestTrue(TEXT("无效类参数协程结果状态正常"), InvalidAsyncPrewarm.GetResult().IsSuccess());
    TestEqual(TEXT("无效类参数协程预热返回 0"), InvalidAsyncPrewarm.GetResult().GetValue(), 0);

    TGameCoroTask<int32> AsyncPrewarmTask = Pool->PrewarmAsync(ActorClass, 6, 2);
    SafetyTicks = 0;
    while (!AsyncPrewarmTask.IsDone() && SafetyTicks < 20) {
        TaskSubsystem->Tick(0.016f);
        ++SafetyTicks;
    }
    TestTrue(TEXT("分帧渐进预热协程在多帧步进后完成"), AsyncPrewarmTask.IsDone());
    TestTrue(TEXT("分帧渐进预热协程结果状态成功"), AsyncPrewarmTask.GetResult().IsSuccess());
    TestEqual(TEXT("分帧渐进预热协程返回成功生成实例数 6"), AsyncPrewarmTask.GetResult().GetValue(), 6);
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("对象池内实际包含 6 个空闲实例"), Idle, 6);

    TaskSubsystem->SetFrameBudgetMilliseconds(FTimeSliceScheduler::DEFAULT_FRAME_BUDGET_MS);
    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameNonActorUObjectPoolTest, "Game.Pool.UObjectPoolLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * @brief 验证非 Actor 纯 UObject 对象池预热、借出、复用、容量上限裁剪、回调重入防护及 AActor 类型隔离
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameNonActorUObjectPoolTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    const TSubclassOf<UObject> ObjClass = UGamePoolableTestObject::StaticClass();
    Pool->ClearObjectPool(ObjClass);
    Pool->SetMaxObjectPoolCapacity(ObjClass, 100);

    TestTrue(TEXT("拒绝使用 AcquireObject 借出 AActor 派生类"),
        Pool->AcquireObject(AGamePoolableTestActor::StaticClass()).IsFailure());
    TestTrue(TEXT("空类型借出返回失败"), Pool->AcquireObject(nullptr).IsFailure());

    Pool->PrewarmObjectPool(ObjClass, 3);
    FPoolStats Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("UObject 预热后空闲数为 3"), Stats.IdleCount, 3);
    TestEqual(TEXT("UObject 预热后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("UObject 预热后总数为 3"), Stats.TotalCount, 3);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 1);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("收缩 UObject 池容量上限立即裁剪空闲实例至 1"), Stats.IdleCount, 1);

    const TGameStatusResult<UGamePoolableTestObject*> ObjARes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("首次借出纯 UObject 成功"), ObjARes.IsSuccess());
    UGamePoolableTestObject* ObjA = ObjARes.GetValue();
    TestNotNull(TEXT("首次借出纯 UObject 实例有效"), ObjA);
    if (!ObjA) {
        return false;
    }
    TestEqual(TEXT("借出触发纯 UObject 的 OnAcquireFromPool 回调"), ObjA->AcquireCount, 1);
    ObjA->PayloadValue = 42;

    const TGameStatusResult<UGamePoolableTestObject*> ObjBRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("借出第二个实例成功"), ObjBRes.IsSuccess());
    UGamePoolableTestObject* ObjB = ObjBRes.GetValue();
    TestNotNull(TEXT("空闲耗尽时自动 NewObject 扩容借出第二个实例"), ObjB);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("两个活跃实例计数为 2"), Stats.ActiveCount, 2);
    TestEqual(TEXT("空闲数为 0"), Stats.IdleCount, 0);

    TestTrue(TEXT("归还 ObjA 成功"), Pool->ReleaseObject(ObjA).IsSuccess());
    TestEqual(TEXT("归还触发纯 UObject 的 OnReleaseToPool 回调"), ObjA->ReleaseCount, 1);
    TestTrue(TEXT("归还 ObjB 成功"), Pool->ReleaseObject(ObjB).IsSuccess());

    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("受容量上限 1 约束仅保留 1 个空闲实例"), Stats.IdleCount, 1);
    TestEqual(TEXT("全部归还后活跃数为 0"), Stats.ActiveCount, 0);

    const TGameStatusResult<UGamePoolableTestObject*> ReusedObjRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("复用借出成功"), ReusedObjRes.IsSuccess());
    UGamePoolableTestObject* ReusedObj = ReusedObjRes.GetValue();
    TestEqual(TEXT("再次借出精确复用池内缓存的同一 UObject 指针"), ReusedObj, ObjA);
    TestEqual(TEXT("复用实例累计借出计数为 2"), ReusedObj->AcquireCount, 2);
    TestTrue(TEXT("归还复用对象成功"), Pool->ReleaseObject(ReusedObj).IsSuccess());

    UGamePoolableTestObject* ExternalObj = NewObject<UGamePoolableTestObject>(World);
    TestTrue(TEXT("非池属外部 UObject 归还返回失败"), Pool->ReleaseObject(ExternalObj).IsFailure());
    TestEqual(TEXT("非池属外部 UObject 归还不触发回调"), ExternalObj->ReleaseCount, 0);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 10);
    ReusedObj->AcquireCallback = [Pool](UGamePoolableTestObject* Self) {
        Pool->ReleaseObject(Self);
    };
    const TGameStatusResult<UGamePoolableTestObject*> SelfReleasedRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("OnAcquire 内自归还的 UObject 返回失败"), SelfReleasedRes.IsFailure());
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("自归还后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("自归还后空闲数为 1"), Stats.IdleCount, 1);

    ReusedObj->AcquireCallback = [](UGamePoolableTestObject* Self) {
        Self->MarkAsGarbage();
    };
    const TGameStatusResult<UGamePoolableTestObject*> SelfDestroyedRes = Pool->AcquireObject<UGamePoolableTestObject>();
    TestTrue(TEXT("OnAcquire 内被标记回收的 UObject 返回失败"), SelfDestroyedRes.IsFailure());
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("自回收后活跃数归零"), Stats.ActiveCount, 0);
    TestEqual(TEXT("自回收后空闲数归零"), Stats.IdleCount, 0);

    Pool->PrewarmObjectPool(ObjClass, 2);
    Pool->ClearAllPools();
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("ClearAllPools 同步清空所有 UObject 空闲池"), Stats.IdleCount, 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolPayloadAndDetachmentTest,
    "Game.Pool.PayloadAndDetachment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证对象池带参借出注入载荷以及归还时自动解除子 Actor 挂接
 * @param Parameters 自动化测试参数
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
    AGamePoolableTestActor* ChildActor = World->SpawnActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass(), SpawnParams);
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
