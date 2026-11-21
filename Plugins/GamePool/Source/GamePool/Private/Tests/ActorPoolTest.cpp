/**
 * @file ActorPoolTest.cpp
 * @brief Actor 对象池核心借还、预热上限、外部销毁自愈、回调重入、载荷类型安全与蓝图外观自动化测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Tests/AGamePoolableTestActor.h"
#include "Tests/UGamePoolableTestObject.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "Logging/FGameLogRegistry.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

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

    UObjectPoolSubsystem* Pool = UObjectPoolSubsystem::GetPtr(World);
    TestNotNull(TEXT("UObjectPoolSubsystem::GetPtr 获取成功"), Pool);
    if (!Pool) {
        return false;
    }
    TestEqual(TEXT("UObjectPoolSubsystem::Get 返回同一单例引用"), &UObjectPoolSubsystem::Get(World), Pool);

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
 * @brief 验证对象池预热生成、MaxIdleCount 上限截断与清空清理机制
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

    Pool->Prewarm(AGamePoolableTestActor::StaticClass(), 5);
    TestEqual(TEXT("同步预热严格受 MaxIdleCount 上限约束"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 1);

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

    AGamePoolableTestActor* IdleVictim = Pool->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass()).GetValue();
    AGamePoolableTestActor* IdleSurvivor = Pool->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass()).GetValue();
    TestNotNull(TEXT("空闲自愈测试实例 1 有效"), IdleVictim);
    TestNotNull(TEXT("空闲自愈测试实例 2 有效"), IdleSurvivor);
    if (IdleVictim != nullptr && IdleSurvivor != nullptr) {
        TestTrue(TEXT("归还实例 1 入空闲池"), Pool->ReleaseActor(IdleVictim).IsSuccess());
        TestTrue(TEXT("归还实例 2 入空闲池"), Pool->ReleaseActor(IdleSurvivor).IsSuccess());
        TestEqual(TEXT("归还后空闲数为 2"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 2);

        IdleVictim->Destroy();
        TestEqual(TEXT("池内空闲 Actor 遭外部销毁后立即自愈扣减空闲数至 1"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 1);

        AGamePoolableTestActor* AcquiredRemaining = Pool->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass()).GetValue();
        TestEqual(TEXT("剩余空闲实例精确复用未受损对象"), AcquiredRemaining, IdleSurvivor);
        (void)Pool->ReleaseActor(AcquiredRemaining);
    }

    Pool->ClearPool(AGamePoolableTestActor::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolExternalReleaseTest,
    "Game.Pool.ExternalRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证未从池中借出的 Actor 不会被外部归还污染空闲池与统计，并返回规范错误码
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

    const TGameStatusResult<void> ReleaseRes = Pool->ReleaseActor(ExternalActor);
    TestTrue(TEXT("外部归还返回失败"), ReleaseRes.IsFailure());
    TestEqual(TEXT("错误归属 ModuleId::Pool"), ReleaseRes.GetError().ModuleId, Game::ModuleId::Pool);
    TestEqual(TEXT("错误码为 NotOwnedActiveInstance"), ReleaseRes.GetError().ErrorCode, Game::PoolErrorCodes::NotOwnedActiveInstance);
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
 * @brief 验证 OnAcquire 与 OnAcquireFromPoolWithPayload 内归还自身均不会形成活跃与空闲双重归属
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

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);
    const TGameStatusResult<AGamePoolableTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("首次借出成功"), ActorRes.IsSuccess());
    AGamePoolableTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("首次借出用于安装重入回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [Pool](AGamePoolableTestActor* InActor) {
        (void)Pool->ReleaseActor(InActor);
    };
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(Actor).IsSuccess());

    const TGameStatusResult<AGamePoolableTestActor*> ReenteredActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("OnAcquire 内自归还导致借出失败"), ReenteredActorRes.IsFailure());
    TestEqual(TEXT("错误码为 InvalidatedDuringAcquire"), ReenteredActorRes.GetError().ErrorCode, Game::PoolErrorCodes::InvalidatedDuringAcquire);

    const FPoolStats ReenteredStats = Pool->GetPoolStats(ActorClass);
    TestEqual(TEXT("自归还后活跃数为零"), ReenteredStats.ActiveCount, 0);
    TestEqual(TEXT("自归还后仅保留一个空闲实例"), ReenteredStats.IdleCount, 1);
    TestEqual(TEXT("自归还后实例无双重计数"), ReenteredStats.TotalCount, 1);

    Actor->AcquireCallback = nullptr;
    Actor->PayloadAcquireCallback = [Pool](AGamePoolableTestActor* InActor) {
        (void)Pool->ReleaseActor(InActor);
    };
    const int32 PayloadVal = 123;
    const TGameStatusResult<AGamePoolableTestActor*> PayloadReenterRes =
        Pool->AcquireActorWithPayload<AGamePoolableTestActor>(ActorClass, FTransform::Identity, PayloadVal);
    TestTrue(TEXT("OnAcquireFromPoolWithPayload 内自归还同样被拦截为失败"), PayloadReenterRes.IsFailure());
    TestEqual(TEXT("载荷回调自归还错误码一致"), PayloadReenterRes.GetError().ErrorCode, Game::PoolErrorCodes::InvalidatedDuringAcquire);
    TestEqual(TEXT("载荷回调自归还后活跃数为零"), Pool->GetPoolStats(ActorClass).ActiveCount, 0);
    TestEqual(TEXT("载荷回调自归还后空闲数为一"), Pool->GetPoolStats(ActorClass).IdleCount, 1);

    Pool->ClearPool(ActorClass);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolDestroyOnAcquireTest,
    "Game.Pool.DestroyOnAcquire",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 OnAcquire 内销毁自身会撤销活跃归属且借出返回规范失败
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

    const TSubclassOf<AActor> ActorClass = AGamePoolableTestActor::StaticClass();
    Pool->ClearPool(ActorClass);
    const TGameStatusResult<AGamePoolableTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("首次借出成功"), ActorRes.IsSuccess());
    AGamePoolableTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("首次借出用于安装销毁回调"), Actor);
    if (!Actor) {
        return false;
    }
    Actor->AcquireCallback = [](AGamePoolableTestActor* InActor) {
        InActor->Destroy();
    };
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(Actor).IsSuccess());

    const TGameStatusResult<AGamePoolableTestActor*> ReenteredActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("回调内已销毁导致借出失败"), ReenteredActorRes.IsFailure());
    TestEqual(TEXT("错误码为 InvalidatedDuringAcquire"), ReenteredActorRes.GetError().ErrorCode, Game::PoolErrorCodes::InvalidatedDuringAcquire);

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
 * @brief 验证带类型指纹的载荷借出、错配载荷拦截与归还时自动解除子 Actor 挂接
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

    const float WrongTypePayload = 3.14f;
    const FPoolAcquirePayload WrongView = FPoolAcquirePayload::From(WrongTypePayload);
    TestTrue(TEXT("类型指纹视图有效"), WrongView.IsValid());
    TestTrue(TEXT("匹配 float 类型指纹"), WrongView.IsType<float>());
    TestFalse(TEXT("拒绝误解包为 int32 类型指纹"), WrongView.IsType<int32>());
    TestNull(TEXT("错配类型安全解包返回 nullptr 防 UB"), WrongView.Extract<int32>());

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolFacadeAndContractsTest,
    "Game.Pool.FacadeAndErrorContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 K2 蓝图门面、类型不匹配前置零泄漏拦截、空池隔离清理与 ModuleId::Pool 错误契约
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolFacadeAndContractsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UWorld* World = GetTestWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (!World) {
        return false;
    }

    UObjectPoolSubsystem* Pool = UObjectPoolSubsystem::GetPtr(World);
    TestNotNull(TEXT("对象池子系统有效"), Pool);
    if (!Pool) {
        return false;
    }

    TestNull(TEXT("空上下文调用 GetPtr 安全返回 nullptr"), UObjectPoolSubsystem::GetPtr(nullptr));

    Pool->ClearAllPools();

    const TGameStatusResult<APawn*> MismatchRes = Pool->AcquireActor<APawn>(AGamePoolableTestActor::StaticClass());
    TestTrue(TEXT("类型不匹配借出返回失败"), MismatchRes.IsFailure());
    TestEqual(TEXT("错误归属 ModuleId::Pool"), MismatchRes.GetError().ModuleId, Game::ModuleId::Pool);
    TestEqual(TEXT("错误码为 TypeMismatch"), MismatchRes.GetError().ErrorCode, Game::PoolErrorCodes::TypeMismatch);
    TestEqual(TEXT("类型不匹配拦截后零活跃实例泄漏"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).ActiveCount, 0);

    const TGameStatusResult<AActor*> NullClassRes = Pool->AcquireActor<AActor>(nullptr);
    TestTrue(TEXT("空 ActorClass 返回失败"), NullClassRes.IsFailure());
    TestEqual(TEXT("空类错误码为 InvalidClass"), NullClassRes.GetError().ErrorCode, Game::PoolErrorCodes::InvalidClass);

    AActor* K2Actor = Pool->K2_AcquireActor(AGamePoolableTestActor::StaticClass(), FTransform::Identity);
    TestNotNull(TEXT("K2_AcquireActor 借出成功"), K2Actor);
    TestEqual(TEXT("K2 借出后活跃数为 1"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).ActiveCount, 1);
    Pool->K2_ReleaseActor(K2Actor);
    TestEqual(TEXT("K2_ReleaseActor 归还后活跃数为 0"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).ActiveCount, 0);
    TestEqual(TEXT("K2_ReleaseActor 归还后空闲数为 1"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 1);

    UObject* K2Object = Pool->K2_AcquireObject(UGamePoolableTestObject::StaticClass());
    TestNotNull(TEXT("K2_AcquireObject 借出成功"), K2Object);
    TestEqual(TEXT("K2 借出纯 UObject 后活跃数为 1"), Pool->GetObjectPoolStats(UGamePoolableTestObject::StaticClass()).ActiveCount, 1);
    Pool->K2_ReleaseObject(K2Object);
    TestEqual(TEXT("K2_ReleaseObject 归还后空闲数为 1"), Pool->GetObjectPoolStats(UGamePoolableTestObject::StaticClass()).IdleCount, 1);

    Pool->ClearAllActorPools();
    TestEqual(TEXT("ClearAllActorPools 清空 Actor 池"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 0);
    TestEqual(TEXT("ClearAllActorPools 不影响纯 UObject 池"), Pool->GetObjectPoolStats(UGamePoolableTestObject::StaticClass()).IdleCount, 1);

    Pool->Prewarm(AGamePoolableTestActor::StaticClass(), 2);
    Pool->ClearAllObjectPools();
    TestEqual(TEXT("ClearAllObjectPools 清空纯 UObject 池"), Pool->GetObjectPoolStats(UGamePoolableTestObject::StaticClass()).IdleCount, 0);
    TestEqual(TEXT("ClearAllObjectPools 不影响 Actor 池"), Pool->GetPoolStats(AGamePoolableTestActor::StaticClass()).IdleCount, 2);

    const FLogCategoryBase& RegisteredLogCategory = Game::Log::FGameLogRegistry::GetCategory("GamePool");
    TestEqual(TEXT("GamePool 模块已向 GameLogging 注册统一分类 LogGamePool"), RegisteredLogCategory.GetCategoryName(), FName(TEXT("LogGamePool")));

    Pool->ClearAllPools();
    AGamePoolableTestActor* ReenterHost = Pool->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass()).GetValue();
    TestNotNull(TEXT("重入扩容测试宿主 Actor 借出成功"), ReenterHost);
    if (ReenterHost != nullptr) {
        AActor* SpawnedDuringAcquire = nullptr;
        AActor* SpawnedDuringRelease = nullptr;
        ReenterHost->AcquireCallback = [Pool, &SpawnedDuringAcquire](AGamePoolableTestActor*) {
            TGameStatusResult<APawn*> NestedPawn = Pool->AcquireActor<APawn>(APawn::StaticClass());
            if (NestedPawn.IsSuccess()) {
                SpawnedDuringAcquire = NestedPawn.GetValue();
            }
        };
        ReenterHost->ReleaseCallback = [Pool, &SpawnedDuringRelease](AGamePoolableTestActor*) {
            TGameStatusResult<AActor*> NestedActor = Pool->AcquireActor<AActor>(AActor::StaticClass());
            if (NestedActor.IsSuccess()) {
                SpawnedDuringRelease = NestedActor.GetValue();
            }
        };
        TestTrue(TEXT("归还宿主时回调内重入新类别不导致 TMap 桶悬空"), Pool->ReleaseActor(ReenterHost).IsSuccess());
        TestNotNull(TEXT("OnReleaseToPool 内重入借出新类别成功"), SpawnedDuringRelease);

        TGameStatusResult<AGamePoolableTestActor*> ReacquiredHost =
            Pool->AcquireActor<AGamePoolableTestActor>(AGamePoolableTestActor::StaticClass());
        TestTrue(TEXT("复用借出宿主时回调内重入新类别不导致 TMap 桶悬空"), ReacquiredHost.IsSuccess());
        TestNotNull(TEXT("OnAcquireFromPool 内重入借出新类别成功"), SpawnedDuringAcquire);

        ReenterHost->AcquireCallback = nullptr;
        ReenterHost->ReleaseCallback = nullptr;
        (void)Pool->ReleaseActor(ReenterHost);
        if (SpawnedDuringAcquire != nullptr) {
            (void)Pool->ReleaseActor(SpawnedDuringAcquire);
        }
        if (SpawnedDuringRelease != nullptr) {
            (void)Pool->ReleaseActor(SpawnedDuringRelease);
        }
    }

    Pool->ClearAllPools();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
