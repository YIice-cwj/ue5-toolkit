/**
 * @file UGameAbilitySystemBootstrapTest.cpp
 * @brief GAS 启动握手与自愈引导组件自动化测试套件
 */

#include "Ability/UGameAbilitySystemBootstrap.h"

#if WITH_AUTOMATION_TESTS

#include "Abilities/GameplayAbility.h"
#include "Ability/UGameAbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapTest,
    "Game.Gameplay.Bootstrap.EnsureInitialized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 EnsureInitialized 初值写入幂等、ASC 切换重置、防御性空指针判断与宿主自愈尝试
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

    APlayerState* DummyPS = NewObject<APlayerState>(GetTransientPackage());
    UGameAbilitySystemBootstrap* PSBootstrap = NewObject<UGameAbilitySystemBootstrap>(DummyPS);
    PSBootstrap->SetResolveAbilitySystemComponentDelegate(FResolveAbilitySystemComponent::CreateLambda([ASC1]() {
        return ASC1;
    }));
    PSBootstrap->TryInitializeFromOwner();
    TestFalse(TEXT("PlayerState 无有效 Pawn 时自愈短路"), PSBootstrap->IsInitialAttributesWritten());

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
    TestWorld->DestroyWorld(false);
    TestWorld->RemoveFromRoot();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAbilitySystemBootstrapDataReadyFirstTest,
    "Game.Gameplay.Bootstrap.DataReadyFirst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证数据未就绪时的门禁拦截、NotifyDataReady 唤醒、默认技能授予及 EndPlay 清理
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameAbilitySystemBootstrapDataReadyFirstTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameAbilitySystemBootstrap* Bootstrap = NewObject<UGameAbilitySystemBootstrap>(GetTransientPackage());
    AActor* Avatar = NewObject<AActor>(GetTransientPackage());
    UGameAbilitySystemComponent* ASC = NewObject<UGameAbilitySystemComponent>(Avatar);
    UGameAbilitySystemComponent* ReplacementASC = NewObject<UGameAbilitySystemComponent>(Avatar);
    UAbilitySystemComponent* CurrentTargetASC = ASC;

    FArrayProperty* DefaultAbilitiesProperty = FindFProperty<FArrayProperty>(
        UGameAbilitySystemBootstrap::StaticClass(),
        TEXT("DefaultAbilities")
    );
    FIntProperty* MaxReplicationRetriesProperty = FindFProperty<FIntProperty>(
        UGameAbilitySystemBootstrap::StaticClass(),
        TEXT("MaxReplicationRetries")
    );
    TestNotNull(TEXT("默认能力属性可通过反射配置"), DefaultAbilitiesProperty);
    TestNotNull(TEXT("复制重试上限属性可通过反射配置"), MaxReplicationRetriesProperty);
    if (DefaultAbilitiesProperty == nullptr || MaxReplicationRetriesProperty == nullptr) {
        return false;
    }

    auto& DefaultAbilities = *reinterpret_cast<TArray<TSubclassOf<UGameplayAbility>>*>(
        DefaultAbilitiesProperty->GetPropertyValuePtr_InContainer(Bootstrap)
    );
    DefaultAbilities.Add(UGameplayAbility::StaticClass());
    *static_cast<int32*>(MaxReplicationRetriesProperty->GetPropertyValuePtr_InContainer(Bootstrap)) = 0;

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
    TestTrue(TEXT("数据就绪后标记能力已授予"), Bootstrap->IsDefaultAbilitiesGranted());
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

    return true;
}

#endif  // WITH_AUTOMATION_TESTS

