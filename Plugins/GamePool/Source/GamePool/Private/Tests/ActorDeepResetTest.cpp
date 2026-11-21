/**
 * @file ActorDeepResetTest.cpp
 * @brief 池化 Actor 全部 7 项 EPoolResetFlags 深度重置与 1-pass 单次遍历组件清洗自动化测试
 */

#include "Subsystem/UObjectPoolSubsystem.h"

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Tests/AGamePoolableTestActor.h"
#include "TimerManager.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Tests/GamePoolTestUtils.h"

using Game::Pool::TestUtils::GetTestWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolDeepResetTest,
    "Game.Pool.DeepReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Timers、Movement、Audio、Animation 与 PawnAndAI 引擎组件深度重置策略
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolDeepResetTest::RunTest([[maybe_unused]] const FString& Parameters) {
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
    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);

    const TGameStatusResult<AGamePoolableTestActor*> ActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出 Actor 成功"), ActorRes.IsSuccess());
    AGamePoolableTestActor* Actor = ActorRes.GetValue();
    TestNotNull(TEXT("Actor 有效"), Actor);
    if (!Actor) {
        return false;
    }

    FTimerHandle TestTimerHandle;
    World->GetTimerManager().SetTimer(
        TestTimerHandle,
        FTimerDelegate::CreateWeakLambda(Actor, []() {}),
        60.0f,
        false
    );
    TestTrue(TEXT("归还前挂接在 Actor 上的定时器处于活跃态"), World->GetTimerManager().IsTimerActive(TestTimerHandle));

    UProjectileMovementComponent* MoveComp = NewObject<UProjectileMovementComponent>(Actor);
    MoveComp->SetUpdatedComponent(Actor->GetRootComponent());
    MoveComp->RegisterComponent();
    MoveComp->Velocity = FVector(1200.0, 0.0, 0.0);
    TestFalse(TEXT("归还前移动组件存在非零速度"), MoveComp->Velocity.IsNearlyZero());

    UAudioComponent* AudioComp = NewObject<UAudioComponent>(Actor);
    AudioComp->SetupAttachment(Actor->GetRootComponent());
    AudioComp->RegisterComponent();
    TestTrue(TEXT("音频组件注册成功"), AudioComp->IsRegistered());

    USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(Actor);
    SkelComp->SetupAttachment(Actor->GetRootComponent());
    SkelComp->RegisterComponent();
    TestTrue(TEXT("骨骼网格组件注册成功"), SkelComp->IsRegistered());

    TestTrue(TEXT("归还携带多类组件的 Actor 成功"), Pool->ReleaseActor(Actor).IsSuccess());
    TestTrue(TEXT("EPoolResetFlags::Movement 归还时立即清零移动速度"), MoveComp->Velocity.IsNearlyZero());
    TestFalse(TEXT("EPoolResetFlags::Audio 归还后音频不处于播放态"), AudioComp->IsPlaying());

    FTimerHandle ObjectBoundTimer;
    World->GetTimerManager().SetTimer(
        ObjectBoundTimer,
        Actor,
        &AGamePoolableTestActor::K2_DestroyActor,
        60.0f,
        false
    );
    TestTrue(TEXT("以 UObject 成员绑定的定时器初始活跃"), World->GetTimerManager().IsTimerActive(ObjectBoundTimer));

    const TGameStatusResult<AGamePoolableTestActor*> ReacquiredRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("重新借出成功"), ReacquiredRes.IsSuccess());
    TestEqual(TEXT("复用到同一 Actor"), ReacquiredRes.GetValue(), Actor);
    TestTrue(TEXT("再次归还触发 Timers 清理"), Pool->ReleaseActor(Actor).IsSuccess());
    TestFalse(TEXT("EPoolResetFlags::Timers 归还时清理绑定至 Actor 的定时器"), World->GetTimerManager().IsTimerActive(ObjectBoundTimer));

    World->GetTimerManager().ClearTimer(TestTimerHandle);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AController* TestController = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("测试 Controller 生成成功"), TestController);

    if (TestController != nullptr) {
        Pool->SetPoolResetFlags(APawn::StaticClass(), EPoolResetFlags::All);
        const TGameStatusResult<APawn*> PooledPawnRes = Pool->AcquireActor<APawn>(APawn::StaticClass());
        TestTrue(TEXT("借出池化 Pawn 成功"), PooledPawnRes.IsSuccess());
        APawn* CastedPawn = PooledPawnRes.GetValue();
        TestNotNull(TEXT("借出 APawn 有效"), CastedPawn);

        if (CastedPawn != nullptr) {
            TestController->Possess(CastedPawn);
            TestEqual(TEXT("池化 Pawn 附身成功"), CastedPawn->GetController(), TestController);

            TestTrue(TEXT("归还池化 Pawn 成功"), Pool->ReleaseActor(CastedPawn).IsSuccess());
            TestNull(TEXT("EPoolResetFlags::PawnAndAI 归还时自动解除 Controller 附身"), CastedPawn->GetController());
        }

        TestController->Destroy();
    }

    Pool->ClearPool(ActorClass);
    Pool->ClearPool(APawn::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameObjectPoolResetFlagsTest,
    "Game.Pool.ResetFlags",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证按位组件重置掩码（All、按位组合与 None 零遍历加速）
 * @param Parameters 自动化测试参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameObjectPoolResetFlagsTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    UProjectileMovementComponent* MoveComp = NewObject<UProjectileMovementComponent>(FXActor);
    MoveComp->SetUpdatedComponent(FXActor->GetRootComponent());
    MoveComp->RegisterComponent();
    MoveComp->Velocity = FVector(500.0, 0.0, 0.0);

    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FXActor).IsSuccess());
    TestFalse(TEXT("EPoolResetFlags::All 归还时自动停用 UFXSystemComponent"), FXComp->IsActive());
    TestTrue(TEXT("EPoolResetFlags::All 归还时清零移动速度"), MoveComp->Velocity.IsNearlyZero());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::Movement);
    const TGameStatusResult<AGamePoolableTestActor*> PartialActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("按位掩码借出成功"), PartialActorRes.IsSuccess());
    FXComp->SetActiveFlag(true);
    MoveComp->Velocity = FVector(800.0, 0.0, 0.0);
    TestTrue(TEXT("仅 Movement 掩码归还成功"), Pool->ReleaseActor(PartialActorRes.GetValue()).IsSuccess());
    TestTrue(TEXT("仅 Movement 掩码清零移动速度"), MoveComp->Velocity.IsNearlyZero());
    TestTrue(TEXT("仅 Movement 掩码跳过 FX 重置"), FXComp->IsActive());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::None);
    TestEqual(TEXT("ResetFlags 更新为 None"), Pool->GetPoolResetFlags(ActorClass), EPoolResetFlags::None);

    const TGameStatusResult<AGamePoolableTestActor*> FastActorRes = Pool->AcquireActor<AGamePoolableTestActor>(ActorClass);
    TestTrue(TEXT("借出成功"), FastActorRes.IsSuccess());
    AGamePoolableTestActor* FastActor = FastActorRes.GetValue();
    TestEqual(TEXT("复用同一 Actor 实例"), FastActor, FXActor);
    FXComp->SetActiveFlag(true);
    MoveComp->Velocity = FVector(300.0, 0.0, 0.0);
    TestTrue(TEXT("归还成功"), Pool->ReleaseActor(FastActor).IsSuccess());
    TestTrue(TEXT("EPoolResetFlags::None 跳过组件遍历保留 FX 激活标志"), FXComp->IsActive());
    TestFalse(TEXT("EPoolResetFlags::None 跳过组件遍历保留移动速度"), MoveComp->Velocity.IsNearlyZero());

    Pool->SetPoolResetFlags(ActorClass, EPoolResetFlags::All);
    Pool->ClearPool(ActorClass);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
