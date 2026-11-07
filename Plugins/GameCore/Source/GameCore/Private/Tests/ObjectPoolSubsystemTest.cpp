// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pool/ObjectPoolSubsystem.h"

#include "Particles/ParticleSystemComponent.h"
#include "Tasks/UTimeSlicedTaskSubsystem.h"
#include "Tests/GamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"
#include "Time/TimeDilationSubsystem.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolAcquireAndReleaseTest, "Game.Core.Pool.AcquireAndRelease",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolExternalDestroyAutoHealingTest, "Game.Core.Pool.ExternalDestroyAutoHealing",
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolExternalReleaseTest, "Game.Core.Pool.ExternalRelease",
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

    Pool->ReleaseActor(ExternalActor);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolReleaseOnAcquireTest, "Game.Core.Pool.ReleaseOnAcquire",
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
    AGamePoolableReleaseOnAcquireTestActor* Actor = Pool->AcquireActor<AGamePoolableReleaseOnAcquireTestActor>(ActorClass);
    TestNotNull(TEXT("首次借出用于安装重入回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [Pool](AGamePoolableTestActor* InActor) {
        Pool->ReleaseActor(InActor);
    };
    Pool->ReleaseActor(Actor);

    AGamePoolableReleaseOnAcquireTestActor* ReenteredActor =
        Pool->AcquireActor<AGamePoolableReleaseOnAcquireTestActor>(ActorClass);
    TestNull(TEXT("回调内已归还的 Actor 不作为本次借出结果返回"), ReenteredActor);

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("自归还后活跃数为零"), Active, 0);
    TestEqual(TEXT("自归还后仅保留一个空闲实例"), Idle, 1);
    TestEqual(TEXT("自归还后实例无双重计数"), Total, 1);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolDestroyOnAcquireTest, "Game.Core.Pool.DestroyOnAcquire",
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
    AGamePoolableDestroyOnAcquireTestActor* Actor = Pool->AcquireActor<AGamePoolableDestroyOnAcquireTestActor>(ActorClass);
    TestNotNull(TEXT("首次借出用于安装销毁回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [](AGamePoolableTestActor* InActor) {
        InActor->Destroy();
    };
    Pool->ReleaseActor(Actor);

    AGamePoolableDestroyOnAcquireTestActor* ReenteredActor =
        Pool->AcquireActor<AGamePoolableDestroyOnAcquireTestActor>(ActorClass);
    TestNull(TEXT("回调内已销毁的 Actor 不作为本次借出结果返回"), ReenteredActor);

    int32 Total = 0, Active = 0, Idle = 0;
    Pool->GetPoolStats(ActorClass, Total, Active, Idle);
    TestEqual(TEXT("自销毁后活跃数为零"), Active, 0);
    TestEqual(TEXT("自销毁后空闲数为零"), Idle, 0);
    TestEqual(TEXT("自销毁后实例总数为零"), Total, 0);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameObjectPoolResetFlagsAndTimeSliceTest, "Game.Core.Pool.ResetFlagsAndTimeSlice",
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

    AGamePoolableTestActor* FXActor = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestNotNull(TEXT("借出携带 FX 组件的 Actor 有效"), FXActor);
    if (!FXActor) {
        return false;
    }

    UParticleSystemComponent* FXComp = NewObject<UParticleSystemComponent>(FXActor);
    FXComp->SetupAttachment(FXActor->GetRootComponent());
    FXComp->RegisterComponent();
    FXComp->SetActiveFlag(true);
    TestTrue(TEXT("归还前 FX 组件处于激活态"), FXComp->IsActive());

    Pool->ReleaseActor(FXActor);
    TestFalse(TEXT("EPoolResetFlags::All 归还时自动停用 UFXSystemComponent"), FXComp->IsActive());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::None);
    TestEqual(TEXT("ResetFlags 更新为 None"), Pool->GetPoolResetFlags(ActorClass), EPoolResetFlags::None);

    AGamePoolableTestActor* FastActor = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestEqual(TEXT("复用同一 Actor 实例"), FastActor, FXActor);
    FXComp->SetActiveFlag(true);
    Pool->ReleaseActor(FastActor);
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

    TaskSubsystem->SetFrameBudgetMilliseconds(UTimeSlicedTaskSubsystem::DEFAULT_FRAME_BUDGET_MS);
    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameNonActorUObjectPoolTest, "Game.Core.Pool.UObjectPoolLifecycle",
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

    TestNull(TEXT("拒绝使用 AcquireObject 借出 AActor 派生类"),
        Pool->AcquireObject(AGamePoolableTestActor::StaticClass()));
    TestNull(TEXT("空类型借出返回 nullptr"), Pool->AcquireObject(nullptr));

    Pool->PrewarmObjectPool(ObjClass, 3);
    FPoolStats Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("UObject 预热后空闲数为 3"), Stats.IdleCount, 3);
    TestEqual(TEXT("UObject 预热后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("UObject 预热后总数为 3"), Stats.TotalCount, 3);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 1);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("收缩 UObject 池容量上限立即裁剪空闲实例至 1"), Stats.IdleCount, 1);

    UGamePoolableTestObject* ObjA = Pool->AcquireObject<UGamePoolableTestObject>();
    TestNotNull(TEXT("首次借出纯 UObject 实例有效"), ObjA);
    if (!ObjA) {
        return false;
    }
    TestEqual(TEXT("借出触发纯 UObject 的 OnAcquireFromPool 回调"), ObjA->AcquireCount, 1);
    ObjA->PayloadValue = 42;

    UGamePoolableTestObject* ObjB = Pool->AcquireObject<UGamePoolableTestObject>();
    TestNotNull(TEXT("空闲耗尽时自动 NewObject 扩容借出第二个实例"), ObjB);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("两个活跃实例计数为 2"), Stats.ActiveCount, 2);
    TestEqual(TEXT("空闲数为 0"), Stats.IdleCount, 0);

    Pool->ReleaseObject(ObjA);
    TestEqual(TEXT("归还触发纯 UObject 的 OnReleaseToPool 回调"), ObjA->ReleaseCount, 1);
    Pool->ReleaseObject(ObjB);

    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("受容量上限 1 约束仅保留 1 个空闲实例"), Stats.IdleCount, 1);
    TestEqual(TEXT("全部归还后活跃数为 0"), Stats.ActiveCount, 0);

    UGamePoolableTestObject* ReusedObj = Pool->AcquireObject<UGamePoolableTestObject>();
    TestEqual(TEXT("再次借出精确复用池内缓存的同一 UObject 指针"), ReusedObj, ObjA);
    TestEqual(TEXT("复用实例累计借出计数为 2"), ReusedObj->AcquireCount, 2);
    Pool->ReleaseObject(ReusedObj);

    UGamePoolableTestObject* ExternalObj = NewObject<UGamePoolableTestObject>(World);
    Pool->ReleaseObject(ExternalObj);
    TestEqual(TEXT("非池属外部 UObject 归还被忽略不触发回调"), ExternalObj->ReleaseCount, 0);

    Pool->SetMaxObjectPoolCapacity(ObjClass, 10);
    ReusedObj->AcquireCallback = [Pool](UGamePoolableTestObject* Self) {
        Pool->ReleaseObject(Self);
    };
    UGamePoolableTestObject* SelfReleased = Pool->AcquireObject<UGamePoolableTestObject>();
    TestNull(TEXT("OnAcquire 内自归还的 UObject 不作为有效借出返回"), SelfReleased);
    Stats = Pool->GetObjectPoolStats(ObjClass);
    TestEqual(TEXT("自归还后活跃数为 0"), Stats.ActiveCount, 0);
    TestEqual(TEXT("自归还后空闲数为 1"), Stats.IdleCount, 1);

    ReusedObj->AcquireCallback = [](UGamePoolableTestObject* Self) {
        Self->MarkAsGarbage();
    };
    UGamePoolableTestObject* SelfDestroyed = Pool->AcquireObject<UGamePoolableTestObject>();
    TestNull(TEXT("OnAcquire 内被标记回收的 UObject 返回 nullptr 并自愈活跃数"), SelfDestroyed);
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
    "Game.Core.Pool.PayloadAndDetachment",
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
    AGamePoolableTestActor* ActorWithPayload = Pool->AcquireActorWithPayload<AGamePoolableTestActor>(
        AGamePoolableTestActor::StaticClass(),
        FTransform::Identity,
        CustomPayload
    );
    TestNotNull(TEXT("借出 Actor 成功"), ActorWithPayload);
    TestEqual(TEXT("载荷值成功传递至 Actor"), ActorWithPayload->LastReceivedPayloadValue, 999);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AGamePoolableTestActor* ChildActor = World->SpawnActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass(), SpawnParams);
    TestNotNull(TEXT("子 Actor 生成成功"), ChildActor);

    ChildActor->AttachToActor(ActorWithPayload, FAttachmentTransformRules::KeepWorldTransform);
    TestEqual<AActor*>(TEXT("挂接后子 Actor 的父节点为 ActorWithPayload"), ChildActor->GetAttachParentActor(), ActorWithPayload);

    Pool->ReleaseActor(ActorWithPayload);
    TestNull(TEXT("ReleaseActor 后子 Actor 自动脱钩父节点，Parent 为空"), ChildActor->GetAttachParentActor());

    ChildActor->Destroy();
    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
