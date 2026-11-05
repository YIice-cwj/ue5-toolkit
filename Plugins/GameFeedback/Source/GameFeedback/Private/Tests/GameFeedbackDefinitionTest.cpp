/**
 * @file GameFeedbackDefinitionTest.cpp
 * @brief 反馈定义资产标识、映射版本及 Provider 缓存生命周期自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace {

/**
 * @brief 创建具有稳定资产路径的测试 Definition
 * @param ObjectPath 完整对象路径（Package.Object）
 * @return 测试 Definition
 */
UGameFeedbackDefinition* CreateDefinitionAtPath(const FString& ObjectPath) {
    FString PackageName;
    FString ObjectName;
    ObjectPath.Split(TEXT("."), &PackageName, &ObjectName);
    return NewObject<UGameFeedbackDefinition>(CreatePackage(*PackageName), *ObjectName);
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionIdentityTest,
    "Game.Feedback.DefinitionIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Definition 稳定 ID、映射版本、全部来源索引及 CRC 冲突拒绝
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackDefinitionIdentityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackRegistry* EquivalentRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Default.Default"));
    UGameFeedbackDefinition* ChannelDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Channel.Channel"));
    UGameFeedbackDefinition* TagDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Tag.Tag"));
    UGameFeedbackDefinition* CompositeDef = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Composite.Composite"));
    const FGameplayTag ChannelTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Identity.Channel")));
    const FGameplayTag FeedbackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Identity.Tag")));

    for (UGameFeedbackRegistry* CurrentRegistry : {Registry, EquivalentRegistry}) {
        CurrentRegistry->DefaultFallbackDefinition = DefaultDef;
        CurrentRegistry->ExactChannelMap.Add(ChannelTag, ChannelDef);
        CurrentRegistry->ExactFeedbackTagMap.Add(FeedbackTag, TagDef);
        FGameFeedbackCompositeEntry Entry;
        Entry.Priority = 7;
        Entry.Definition = CompositeDef;
        CurrentRegistry->CompositeRules.Add(Entry);
        CurrentRegistry->RebuildDefinitionIndex();
    }

    const uint32 InitialVersion = Registry->GetDefinitionMappingVersion();
    TestNotEqual(TEXT("有效 Registry 必须具有非零映射版本"), InitialVersion, 0u);
    TestEqual(TEXT("相同映射生成相同版本"), EquivalentRegistry->GetDefinitionMappingVersion(), InitialVersion);

    for (UGameFeedbackDefinition* Definition : {DefaultDef, ChannelDef, TagDef, CompositeDef}) {
        const uint32 DefinitionId = Registry->GetDefinitionId(Definition);
        TestNotEqual(TEXT("已登记 Definition 必须具有非零 ID"), DefinitionId, 0u);
        TestEqual(TEXT("同路径在等价 Registry 中 ID 确定"), EquivalentRegistry->GetDefinitionId(Definition), DefinitionId);
        TestEqual(TEXT("全部 Registry 来源均可按 ID 往返"), Registry->FindDefinitionById(DefinitionId).Get(), Definition);
    }

    Registry->ExactChannelMap.FindChecked(ChannelTag) = CompositeDef;
    Registry->RebuildDefinitionIndex();
    TestNotEqual(TEXT("映射或资产路径变化必须改变版本"), Registry->GetDefinitionMappingVersion(), InitialVersion);

    UGameFeedbackDefinition* Unregistered = CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Unregistered.Unregistered"));
    TestEqual(TEXT("未登记 Override 不可生成网络 ID"), Registry->GetDefinitionId(Unregistered), 0u);
    TestTrue(TEXT("未知 ID 不可解析"), Registry->FindDefinitionById(0xFFFFFFFFu).IsNull());

    UGameFeedbackRegistry* CollisionRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CollisionA = CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_8733.Def_8733"));
    UGameFeedbackDefinition* CollisionB = CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_54027.Def_54027"));
    const FGameplayTag CollisionTagA = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Collision.A")));
    const FGameplayTag CollisionTagB = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Feedback.Test.Collision.B")));
    CollisionRegistry->ExactFeedbackTagMap.Add(CollisionTagA, CollisionA);
    CollisionRegistry->ExactFeedbackTagMap.Add(CollisionTagB, CollisionB);
    CollisionRegistry->RebuildDefinitionIndex();
    const uint32 CollisionIdA = UGameFeedbackRegistry::MakeDefinitionId(FSoftObjectPath(CollisionA));
    const uint32 CollisionIdB = UGameFeedbackRegistry::MakeDefinitionId(FSoftObjectPath(CollisionB));
    TestNotEqual(TEXT("CRC 冲突夹具必须生成有效 ID"), CollisionIdA, 0u);
    TestEqual(TEXT("CRC 冲突夹具必须有效"), CollisionIdA, CollisionIdB);
    TestEqual(TEXT("CRC 冲突项 A 必须不可解析"), CollisionRegistry->GetDefinitionId(CollisionA), 0u);
    TestEqual(TEXT("CRC 冲突项 B 必须不可解析"), CollisionRegistry->GetDefinitionId(CollisionB), 0u);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionProviderLifetimeTest,
    "Game.Feedback.DefinitionProviderLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Definition 强缓存、同路径异步句柄去重、加载完成后再解析缓存及注销释放
 * @param Parameters 自动化测试参数
 * @return 测试是否通过
 */
bool FGameFeedbackDefinitionProviderLifetimeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CachedDefinition = CreateDefinitionAtPath(TEXT("/Game/Feedback/Lifetime/Cached.Cached"));
    const FGameplayTag CachedChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.Cached")));
    Registry->ExactChannelMap.Add(CachedChannel, CachedDefinition);
    Registry->RebuildDefinitionIndex();

    UGameFeedbackSettings* Settings = GetMutableDefault<UGameFeedbackSettings>();
    const TSoftObjectPtr<UGameFeedbackRegistry> PreviousRegistry = Settings->GlobalRegistry;
    Settings->GlobalRegistry = Registry;
    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackDefinitionProviderLifetimeTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    UGameFeedbackDefinitionProvider* Provider = World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
    TestNotNull(TEXT("Provider 生命周期测试世界有效"), World);
    TestNotNull(TEXT("Provider 生命周期服务有效"), Provider);
    if (!World || !Provider) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }

    FGameFeedbackSignal CachedSignal;
    CachedSignal.PrimaryChannel = CachedChannel;
    TestTrue(TEXT("首次 Resolve 必须返回已加载 Definition"), Provider->Resolve(CachedSignal) == CachedDefinition);
    TWeakObjectPtr<UGameFeedbackDefinition> CachedWeak = CachedDefinition;
    CachedDefinition = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("已解析 Definition 必须由 Provider 强缓存"), CachedWeak.IsValid());

    const FString AsyncPackageName = TEXT("/Game/GameFeedbackProviderLifetimeAsync");
    const FString AsyncFileName = FPackageName::LongPackageNameToFilename(AsyncPackageName, FPackageName::GetAssetPackageExtension());
    const bool bAsyncAssetAlreadyExists = IFileManager::Get().FileExists(*AsyncFileName);
    TestFalse(TEXT("异步测试不得覆盖已有资产"), bAsyncAssetAlreadyExists);
    if (bAsyncAssetAlreadyExists) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }

    UGameFeedbackDefinition* AsyncDefinition = CreateDefinitionAtPath(AsyncPackageName + TEXT(".GameFeedbackProviderLifetimeAsync"));
    AsyncDefinition->SetFlags(RF_Public | RF_Standalone);
    UPackage* AsyncPackage = AsyncDefinition->GetOutermost();
    AsyncPackage->MarkAsFullyLoaded();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bAsyncAssetSaved = UPackage::SavePackage(AsyncPackage, AsyncDefinition, *AsyncFileName, SaveArgs);
    TestTrue(TEXT("异步测试资产必须真实保存"), bAsyncAssetSaved);
    if (!bAsyncAssetSaved) {
        Settings->GlobalRegistry = PreviousRegistry;
        GameInstance->Shutdown();
        return false;
    }
    ON_SCOPE_EXIT {
        IFileManager::Get().Delete(*AsyncFileName);
    };

    const FSoftObjectPath AsyncPath = AsyncDefinition->GetPathName();
    const TSoftObjectPtr<UGameFeedbackDefinition> AsyncSoft(AsyncPath);
    const FGameplayTag AsyncChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.Async")));
    Registry->ExactChannelMap.Add(AsyncChannel, AsyncSoft);
    Registry->RebuildDefinitionIndex();
    FGameFeedbackSignal AsyncSignal;
    AsyncSignal.PrimaryChannel = AsyncChannel;

    TWeakObjectPtr<UGameFeedbackDefinition> AsyncWeak = AsyncDefinition;
    AsyncDefinition->ClearFlags(RF_Standalone);
    AsyncPackage->ClearFlags(RF_Standalone);
    AsyncDefinition = nullptr;
    AsyncPackage = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("异步测试资产必须在 Resolve 前真实卸载"), AsyncWeak.IsValid());

    Provider->PreloadFeedbackBundle({AsyncSoft, TSoftObjectPtr<UGameFeedbackDefinition>()});
    Provider->Resolve(AsyncSignal);
    Provider->Resolve(AsyncSignal);

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    TArray<TSharedRef<FStreamableHandle>> Handles;
    StreamableManager.GetActiveHandles(AsyncPath, Handles);
    TestEqual(TEXT("同一路径连续 Resolve 只能保留一个异步句柄"), Handles.Num(), 1);
    if (!Handles.IsEmpty()) {
        Handles[0]->WaitUntilComplete();
        TestTrue(TEXT("异步 Definition 加载必须真实完成"), Handles[0]->HasLoadCompleted());
        AsyncWeak = Handles[0]->GetLoadedAsset<UGameFeedbackDefinition>();
        TestTrue(
            TEXT("加载完成后再次 Resolve 必须返回同一已加载 Definition"),
            AsyncWeak.IsValid() && Provider->Resolve(AsyncSignal) == AsyncWeak.Get());
        Handles[0]->ReleaseHandle();
    }

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("加载完成后 Resolve 必须写入独立保活的强缓存"), AsyncWeak.IsValid());

    Settings->GlobalRegistry = PreviousRegistry;
    World->DestroyWorld(true);
    GameInstance->Shutdown();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Deinitialize 必须释放同步解析缓存"), CachedWeak.IsValid());
    TestFalse(TEXT("Deinitialize 必须释放异步完成缓存"), AsyncWeak.IsValid());
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
