/**
 * @file UGameAbilitySystemBootstrapTest.cpp
 * @brief GAS 启动握手、重试闭环、非权威拦截与对象池复用自动化测试套件
 */

#include "Ability/UGameAbilitySystemBootstrap.h"

#if WITH_AUTOMATION_TESTS

#include "Abilities/GameplayAbility.h"
#include "Ability/UGameAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace {

/**
 * @brief 逐帧推进测试世界定时器管理器（递增 GFrameCounter 避免同帧 HasBeenTickedThisFrame 短路）
 * @param World 测试世界
 * @param FrameCount 推进帧数
 * @param DeltaSeconds 每帧时间步长（秒）
 */
void AdvanceWorldTimerFrames(UWorld* World, int32 FrameCount, float DeltaSeconds) {
    for (int32 Index = 0; Index < FrameCount; ++Index) {
        ++GFrameCounter;
        World->GetTimerManager().Tick(DeltaSeconds);
    }
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapTest,
    "Game.Gameplay.Bootstrap.EnsureInitialized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 EnsureInitialized 幂等、ASC 切换重置、TryInitializeFromOwner 早于 BeginPlay 的定时器重试闭环与非权威拦截
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemBootstrapTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC1 = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC2 = NewObject<UGameAbilitySystemComponent>(GetTransientPackage());

    UAbilitySystemComponent* CurrentTargetASC = ASC1;
    Bootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([&CurrentTargetASC]() {
        return CurrentTargetASC;
    }));

    int32 InitialAttributesWriteCount = 0;
    Bootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&InitialAttributesWriteCount](UAbilitySystemComponent*) {
        ++InitialAttributesWriteCount;
    }));

    AActor* DummyAvatar1 = NewObject<AActor>(GetTransientPackage());
    AActor* DummyAvatar2 = NewObject<AActor>(GetTransientPackage());

    Bootstrap->EnsureInitialized(DummyAvatar1);
    TestTrue(TEXT("初次引导写入初值"), Bootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("初值写入回调触发一次"), InitialAttributesWriteCount, 1);

    Bootstrap->EnsureInitialized(DummyAvatar1);
    TestEqual(TEXT("同 Avatar 重复调用幂等保持"), InitialAttributesWriteCount, 1);

    CurrentTargetASC = ASC2;
    Bootstrap->EnsureInitialized(DummyAvatar2);
    TestTrue(TEXT("换 ASC 重新引导写入初值"), Bootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("新 ASC 重置状态并再次写入初值"), InitialAttributesWriteCount, 2);

    Bootstrap->EnsureInitialized(nullptr);
    TestEqual(TEXT("Avatar 为空时忽略引导"), InitialAttributesWriteCount, 2);

    UGameAbilitySystemBootstrap* UnboundBootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    UnboundBootstrap->EnsureInitialized(DummyAvatar1);
    TestFalse(TEXT("委托未绑定时拒绝引导"), UnboundBootstrap->IsInitialAttributesWritten());

    UGameAbilitySystemBootstrap* NullAscBootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    NullAscBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([]() -> UAbilitySystemComponent* {
        return nullptr;
    }));
    NullAscBootstrap->EnsureInitialized(DummyAvatar1);
    TestFalse(TEXT("委托返回空 ASC 时拒绝引导"), NullAscBootstrap->IsInitialAttributesWritten());

    UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
    TestNotNull(TEXT("测试世界创建成功"), TestWorld);
    if (TestWorld == nullptr) {
        return false;
    }
    TestWorld->AddToRoot();
    ON_SCOPE_EXIT {
        TestWorld->DestroyWorld(false);
        TestWorld->RemoveFromRoot();
    };

    APawn* DummyPawn = TestWorld->SpawnActor<APawn>();
    TestNotNull(TEXT("Pawn 宿主创建成功"), DummyPawn);
    if (DummyPawn == nullptr) {
        return false;
    }
    UGameAbilitySystemBootstrap* PawnBootstrap = NewObject<UGameAbilitySystemBootstrap>(DummyPawn);
    DummyPawn->AddInstanceComponent(PawnBootstrap);
    PawnBootstrap->RegisterComponent();
    PawnBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([ASC1]() {
        return ASC1;
    }));
    PawnBootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&InitialAttributesWriteCount](UAbilitySystemComponent*) {
        ++InitialAttributesWriteCount;
    }));
    PawnBootstrap->TryInitializeFromOwner();
    TestTrue(TEXT("Pawn 宿主成功自愈引导初值"), PawnBootstrap->IsInitialAttributesWritten());

    APawn* LateAscPawn = TestWorld->SpawnActor<APawn>();
    UGameAbilitySystemBootstrap* LateAscBootstrap = NewObject<UGameAbilitySystemBootstrap>(LateAscPawn);
    LateAscPawn->AddInstanceComponent(LateAscBootstrap);
    LateAscBootstrap->RegisterComponent();
    LateAscBootstrap->ConfigureRetryPolicy(0.02f, 3, 2);
    UAbilitySystemComponent* DelayedASC = nullptr;
    LateAscBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([&DelayedASC]() {
        return DelayedASC;
    }));
    int32 DelayedAscWriteCount = 0;
    LateAscBootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&DelayedAscWriteCount](UAbilitySystemComponent*) {
        ++DelayedAscWriteCount;
    }));
    LateAscBootstrap->TryInitializeFromOwner();
    TestFalse(TEXT("Pawn 先到但 ASC 暂空时挂起等待定时器重试"), LateAscBootstrap->IsInitialAttributesWritten());
    AdvanceWorldTimerFrames(TestWorld, 1, 0.03f);
    TestFalse(TEXT("第 1 次定时重试时 ASC 仍空继续等待"), LateAscBootstrap->IsInitialAttributesWritten());
    DelayedASC = NewObject<UGameAbilitySystemComponent>(LateAscPawn);
    AdvanceWorldTimerFrames(TestWorld, 2, 0.03f);
    TestTrue(TEXT("第 2 次定时重试感知延迟到达的 ASC 并完成自愈初始化"), LateAscBootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("延迟 ASC 自愈成功后仅写入初值 1 次"), DelayedAscWriteCount, 1);

    APlayerState* WorldPS = TestWorld->SpawnActor<APlayerState>();
    UGameAbilitySystemBootstrap* PSBootstrap = NewObject<UGameAbilitySystemBootstrap>(WorldPS);
    WorldPS->AddInstanceComponent(PSBootstrap);
    PSBootstrap->RegisterComponent();
    PSBootstrap->ConfigureRetryPolicy(0.02f, 3, 2);
    UGameAbilitySystemComponent* RetryASC = NewObject<UGameAbilitySystemComponent>(WorldPS);
    PSBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([RetryASC]() {
        return RetryASC;
    }));
    int32 RetryWriteCount = 0;
    PSBootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&RetryWriteCount](UAbilitySystemComponent*) {
        ++RetryWriteCount;
    }));
    PSBootstrap->TryInitializeFromOwner();
    TestFalse(TEXT("早于 BeginPlay 调用自愈在无 Pawn 时挂起等待定时器重试"), PSBootstrap->IsInitialAttributesWritten());
    AdvanceWorldTimerFrames(TestWorld, 1, 0.03f);
    TestFalse(TEXT("第 1 次定时重试时仍无 Pawn 继续等待"), PSBootstrap->IsInitialAttributesWritten());
    APawn* LateSpawnedPawn = TestWorld->SpawnActor<APawn>();
    LateSpawnedPawn->SetPlayerState(WorldPS);
    AdvanceWorldTimerFrames(TestWorld, 2, 0.03f);
    TestTrue(TEXT("第 2 次定时重试感知延迟到达的 Pawn 并自愈完成初始化（证明早于 BeginPlay 重试上限已生效）"), PSBootstrap->IsInitialAttributesWritten());
    TestEqual(TEXT("自愈成功后写入初值 1 次"), RetryWriteCount, 1);

    APlayerState* TimeoutPS = TestWorld->SpawnActor<APlayerState>();
    UGameAbilitySystemBootstrap* TimeoutBootstrap = NewObject<UGameAbilitySystemBootstrap>(TimeoutPS);
    TimeoutPS->AddInstanceComponent(TimeoutBootstrap);
    TimeoutBootstrap->RegisterComponent();
    TimeoutBootstrap->ConfigureRetryPolicy(0.02f, 1, 1);
    UGameAbilitySystemComponent* TimeoutASC = NewObject<UGameAbilitySystemComponent>(TimeoutPS);
    TimeoutBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([TimeoutASC]() {
        return TimeoutASC;
    }));
    TimeoutBootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([](UAbilitySystemComponent*) {}));
    TimeoutBootstrap->TryInitializeFromOwner();
    AdvanceWorldTimerFrames(TestWorld, 3, 0.03f);
    APawn* TooLatePawn = TestWorld->SpawnActor<APawn>();
    TooLatePawn->SetPlayerState(TimeoutPS);
    AdvanceWorldTimerFrames(TestWorld, 3, 0.03f);
    TestFalse(TEXT("自愈重试达到上限后中止，后续延迟到达的 Pawn 不再由旧定时器触发"), TimeoutBootstrap->IsInitialAttributesWritten());

    AActor* ProxyActor = TestWorld->SpawnActor<AActor>();
    FByteProperty* RoleProp = FindFProperty<FByteProperty>(AActor::StaticClass(), TEXT("Role"));
    TestNotNull(TEXT("找到 Actor Role 反射属性"), RoleProp);
    if (RoleProp != nullptr) {
        RoleProp->SetPropertyValue_InContainer(ProxyActor, static_cast<uint8>(ROLE_SimulatedProxy));
    }
    UGameAbilitySystemComponent* ProxyASC = NewObject<UGameAbilitySystemComponent>(ProxyActor);
    UGameAbilitySystemBootstrap* ProxyBootstrap = NewObject<UGameAbilitySystemBootstrap>(ProxyActor);
    ProxyActor->AddInstanceComponent(ProxyBootstrap);
    ProxyBootstrap->RegisterComponent();
    ProxyBootstrap->SetDefaultAbilities({ UGameplayAbility::StaticClass() });
    ProxyBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([ProxyASC]() {
        return ProxyASC;
    }));
    int32 ProxyWriteCount = 0;
    ProxyBootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&ProxyWriteCount](UAbilitySystemComponent*) {
        ++ProxyWriteCount;
    }));
    ProxyBootstrap->EnsureInitialized(ProxyActor);
    TestEqual(TEXT("非权威模拟端不执行初值写入"), ProxyWriteCount, 0);
    TestFalse(TEXT("非权威模拟端不授予默认能力"), ProxyBootstrap->IsDefaultAbilitiesGranted());
    TestNull(TEXT("非权威模拟端 ASC 无默认能力规格"), ProxyASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    UGameAbilitySystemBootstrap* EmptyDelegateBootstrap = NewObject<UGameAbilitySystemBootstrap>(DummyPawn);
    EmptyDelegateBootstrap->TryInitializeFromOwner();
    TestFalse(TEXT("委托未绑定时自愈直接返回"), EmptyDelegateBootstrap->IsInitialAttributesWritten());

    AActor* WorldActor = TestWorld->SpawnActor<AActor>();
    UGameAbilitySystemBootstrap* WorldBootstrap = NewObject<UGameAbilitySystemBootstrap>(WorldActor);
    WorldActor->AddInstanceComponent(WorldBootstrap);
    WorldBootstrap->RegisterComponent();
    WorldBootstrap->BeginPlay();
    TestTrue(TEXT("世界 ActorComponent 成功 BeginPlay"), WorldBootstrap->HasBegunPlay());
    WorldBootstrap->EndPlay(EEndPlayReason::Destroyed);
    TestFalse(TEXT("EndPlay 后 HasBegunPlay 为 false"), WorldBootstrap->HasBegunPlay());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapDataReadyFirstTest,
    "Game.Gameplay.Bootstrap.DataReadyFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证数据门禁、无 World 不卡死直授、未绑写初值委托短路、真实复制重试超限强授及 EndPlay 清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemBootstrapDataReadyFirstTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    AActor* Avatar = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(Avatar);
    UGameAbilitySystemComponent* ReplacementASC = NewObject<UGameAbilitySystemComponent>(Avatar);
    UAbilitySystemComponent* CurrentTargetASC = ASC;

    Bootstrap->SetDefaultAbilities({ UGameplayAbility::StaticClass() });
    Bootstrap->ConfigureRetryPolicy(0.05f, 10, 8);

    Bootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([&CurrentTargetASC]() {
        return CurrentTargetASC;
    }));

    int32 InitialAttributesWriteCount = 0;
    Bootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&InitialAttributesWriteCount](UAbilitySystemComponent*) {
        ++InitialAttributesWriteCount;
    }));

    bool bDataReady = false;
    Bootstrap->SetDataReadyQueryDelegate(FQueryDataReady::CreateLambda([&bDataReady]() {
        return bDataReady;
    }));

    Bootstrap->NotifyDataReady();
    TestEqual(TEXT("ActorInfo 初始化前不写初值"), InitialAttributesWriteCount, 0);
    TestFalse(TEXT("ActorInfo 初始化前不标记能力已授予"), Bootstrap->IsDefaultAbilitiesGranted());
    TestNull(TEXT("ActorInfo 初始化前不授予能力"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    Bootstrap->EnsureInitialized(Avatar);
    TestEqual(TEXT("数据就绪前不写初值"), InitialAttributesWriteCount, 0);
    TestFalse(TEXT("数据就绪前不标记能力已授予"), Bootstrap->IsDefaultAbilitiesGranted());

    bDataReady = true;
    Bootstrap->NotifyDataReady();
    TestEqual(TEXT("数据就绪后唤醒写入初值"), InitialAttributesWriteCount, 1);
    TestTrue(TEXT("无 World 即使 MaxReplicationRetries=8 也不卡死并直接授予默认能力"), Bootstrap->IsDefaultAbilitiesGranted());
    TestNotNull(TEXT("正常握手授予默认能力"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    Bootstrap->EnsureInitialized(Avatar);
    TestEqual(TEXT("重复握手不重复写初值"), InitialAttributesWriteCount, 1);
    TestEqual(TEXT("重复握手不重复授予能力"), ASC->GetActivatableAbilities().Num(), 1);

    CurrentTargetASC = ReplacementASC;
    Bootstrap->EnsureInitialized(Avatar);
    TestNull(TEXT("切换 ASC 清理旧 ASC 上已授予的能力"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));
    TestNotNull(TEXT("切换 ASC 后重新授予默认能力"), ReplacementASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    Bootstrap->EndPlay(EEndPlayReason::Destroyed);
    TestNull(TEXT("EndPlay 清理已授予的能力"), ReplacementASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    UGameAbilitySystemBootstrap* NoAttrWriterBootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    UGameAbilitySystemComponent* NoAttrASC = NewObject<UGameAbilitySystemComponent>(Avatar);
    NoAttrWriterBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([NoAttrASC]() {
        return NoAttrASC;
    }));
    int32 DataQueryCount = 0;
    NoAttrWriterBootstrap->SetDataReadyQueryDelegate(FQueryDataReady::CreateLambda([&DataQueryCount]() {
        ++DataQueryCount;
        return true;
    }));
    NoAttrWriterBootstrap->EnsureInitialized(Avatar);
    TestEqual(TEXT("首次握手求值一次数据就绪委托"), DataQueryCount, 1);
    NoAttrWriterBootstrap->NotifyDataReady();
    TestEqual(TEXT("未绑写初值委托且能力授予完毕后 NotifyDataReady 直接短路不再重复求值"), DataQueryCount, 1);

    UWorld* TestWorld = UWorld::CreateWorld(EWorldType::Game, false);
    if (TestWorld != nullptr) {
        ON_SCOPE_EXIT {
            TestWorld->DestroyWorld(false);
        };
        AActor* RepHost = TestWorld->SpawnActor<AActor>();
        UGameAbilitySystemComponent* RepASC = NewObject<UGameAbilitySystemComponent>(RepHost);
        UGameAbilitySystemBootstrap* RepBootstrap = NewObject<UGameAbilitySystemBootstrap>(RepHost);
        RepHost->AddInstanceComponent(RepBootstrap);
        RepBootstrap->RegisterComponent();
        RepBootstrap->SetDefaultAbilities({ UGameplayAbility::StaticClass() });
        RepBootstrap->ConfigureRetryPolicy(0.02f, 2, 2);
        RepBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([RepASC]() {
            return RepASC;
        }));

        RepBootstrap->EnsureInitialized(RepHost);
        if (!RepASC->IsReadyForReplication()) {
            TestFalse(TEXT("复制未就绪时先延迟定时重试"), RepBootstrap->IsDefaultAbilitiesGranted());
            AdvanceWorldTimerFrames(TestWorld, 6, 0.03f);
        }
        TestTrue(TEXT("复制重试超限后强制授予默认能力成功"), RepBootstrap->IsDefaultAbilitiesGranted());
        TestNotNull(TEXT("强制授予后 ASC 持有默认能力规格"), RepASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));
        RepBootstrap->EndPlay(EEndPlayReason::Destroyed);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapPoolableTest,
    "Game.Gameplay.Bootstrap.PoolableLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Bootstrap 接入 IGamePoolable 后的出入池重置与自愈复用生命周期
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemBootstrapPoolableTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    AActor* Avatar = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(Avatar);

    Bootstrap->SetDefaultAbilities({ UGameplayAbility::StaticClass() });
    Bootstrap->ConfigureRetryPolicy(0.05f, 5, 0);

    Bootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([ASC]() {
        return ASC;
    }));

    int32 WriteCount = 0;
    Bootstrap->SetWriteInitialAttributesDelegate(FWriteInitialAttributes::CreateLambda([&WriteCount](UAbilitySystemComponent*) {
        ++WriteCount;
    }));

    Bootstrap->EnsureInitialized(Avatar);
    TestEqual(TEXT("首次握手写入初值"), WriteCount, 1);
    TestTrue(TEXT("首次握手标记能力已授予"), Bootstrap->IsDefaultAbilitiesGranted());
    TestNotNull(TEXT("首次握手成功授予能力"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

    IGamePoolable* Poolable = Cast<IGamePoolable>(Bootstrap);
    TestNotNull(TEXT("Bootstrap 原生实现 IGamePoolable 接口"), Poolable);
    if (Poolable != nullptr) {
        IGamePoolable::Execute_OnReleaseToPool(Bootstrap);
        TestNull(TEXT("入池释放时清理已授予能力"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));
        TestFalse(TEXT("入池释放时重置能力授予标记"), Bootstrap->IsDefaultAbilitiesGranted());
        TestFalse(TEXT("入池释放时重置属性写入标记"), Bootstrap->IsInitialAttributesWritten());

        IGamePoolable::Execute_OnAcquireFromPool(Bootstrap);
        Bootstrap->EnsureInitialized(Avatar);
        TestEqual(TEXT("从池中复用时再次成功写初值"), WriteCount, 2);
        TestTrue(TEXT("从池中复用时再次授予能力"), Bootstrap->IsDefaultAbilitiesGranted());
        TestNotNull(TEXT("从池中复用时成功重新授予能力规格"), ASC->FindAbilitySpecFromClass(UGameplayAbility::StaticClass()));

        IGamePoolable::Execute_OnReleaseToPool(ASC);
        IGamePoolable::Execute_OnReleaseToPool(Bootstrap);
        TestFalse(TEXT("ASC 先于 Bootstrap 归还对象池时 Bootstrap 安全清理且零非权威报错"), Bootstrap->IsDefaultAbilitiesGranted());
        TestFalse(TEXT("反序归还后属性写入状态已复位"), Bootstrap->IsInitialAttributesWritten());
    }

    Bootstrap->EndPlay(EEndPlayReason::Destroyed);
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
