// Copyright Epic Games, Inc. All Rights Reserved.

#include "ObjectPool/ObjectPoolSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Particles/ParticleSystemComponent.h"
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
    FGameObjectPoolDeepResetTest,
    "Game.Pool.DeepReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证骨骼动画与 Pawn 控制器解绑深层引擎重置策略
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

    USkeletalMeshComponent* SkelComp = NewObject<USkeletalMeshComponent>(Actor);
    SkelComp->SetupAttachment(Actor->GetRootComponent());
    SkelComp->RegisterComponent();
    TestTrue(TEXT("骨骼网格组件注册成功"), SkelComp->IsRegistered());

    TestTrue(TEXT("归还带骨骼组件 Actor 成功"), Pool->ReleaseActor(Actor).IsSuccess());

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    APawn* TestPawn = World->SpawnActor<APawn>(APawn::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("测试 Pawn 生成成功"), TestPawn);

    AController* TestController = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FTransform::Identity, SpawnParams);
    TestNotNull(TEXT("测试 Controller 生成成功"), TestController);

    if (TestPawn && TestController) {
        TestController->Possess(TestPawn);
        TestEqual(TEXT("Pawn 被 Controller 附身"), TestPawn->GetController(), TestController);

        Pool->SetPoolResetFlags(APawn::StaticClass(), EPoolResetFlags::All);
        const TGameStatusResult<AActor*> PooledPawnRes = Pool->AcquireActor(APawn::StaticClass());
        TestTrue(TEXT("借出池化 Pawn 成功"), PooledPawnRes.IsSuccess());
        AActor* PooledPawn = PooledPawnRes.GetValue();
        APawn* CastedPawn = Cast<APawn>(PooledPawn);
        TestNotNull(TEXT("池化 Actor 成功转为 APawn"), CastedPawn);

        if (CastedPawn) {
            TestController->Possess(CastedPawn);
            TestEqual(TEXT("池化 Pawn 附身成功"), CastedPawn->GetController(), TestController);

            TestTrue(TEXT("归还池化 Pawn 成功"), Pool->ReleaseActor(CastedPawn).IsSuccess());
            TestNull(TEXT("EPoolResetFlags::PawnAndAI 归还时自动解除 Controller 附身"), CastedPawn->GetController());
        }

        TestController->Destroy();
        TestPawn->Destroy();
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
 * @brief 验证按位组件重置掩码（EPoolResetFlags::All 自动停用特效组件与 EPoolResetFlags::None 零遍历加速）
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
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
