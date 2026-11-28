/**
 * @file GameFeedbackDefinitionTest.cpp
 * @brief 反馈定义资产标识、映射版本、软资产去重预热及 Provider 缓存生命周期自动化测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Asset/GameFeedbackDefinition.h"
#include "Asset/GameFeedbackRegistry.h"
#include "Config/GameFeedbackSettings.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "System/GameFeedbackDefinitionProvider.h"
#include "Tests/GameFeedbackTestHelpers.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameFeedbackDefinitionIdentityTest,
    "Game.Feedback.DefinitionIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 Definition 稳定 ID、映射版本、全部来源索引及 CRC 冲突拒绝
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackDefinitionIdentityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackRegistry* EquivalentRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* DefaultDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Default.Default"));
    UGameFeedbackDefinition* ChannelDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Channel.Channel"));
    UGameFeedbackDefinition* TagDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Tag.Tag"));
    UGameFeedbackDefinition* CompositeDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Composite.Composite"));
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

    UGameFeedbackDefinition* Unregistered = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Identity/Unregistered.Unregistered"));
    TestEqual(TEXT("未登记 Override 不可生成网络 ID"), Registry->GetDefinitionId(Unregistered), 0u);
    TestTrue(TEXT("未知 ID 不可解析"), Registry->FindDefinitionById(0xFFFFFFFFu).IsNull());

    UGameFeedbackRegistry* CollisionRegistry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CollisionA = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_8733.Def_8733"));
    UGameFeedbackDefinition* CollisionB = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Collision/Def_54027.Def_54027"));
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
 * @brief 验证 Definition 强缓存、同路径异步句柄去重、版本缓存失效刷新与注销释放
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameFeedbackDefinitionProviderLifetimeTest::RunTest([[maybe_unused]] const FString& Parameters) {
    GameFeedbackTestHelpers::FScopedFeedbackSettingsGuard SettingsGuard;
    UGameFeedbackSettings* Settings = SettingsGuard.Get();

    UGameFeedbackRegistry* Registry = NewObject<UGameFeedbackRegistry>();
    UGameFeedbackDefinition* CachedDefinition = GameFeedbackTestHelpers::CreateDefinitionAtPath(TEXT("/Game/Feedback/Lifetime/Cached.Cached"));
    const FGameplayTag CachedChannel = UGameplayTagsManager::Get().AddNativeGameplayTag(FName(TEXT("Damage.Test.Lifetime.Cached")));
    Registry->ExactChannelMap.Add(CachedChannel, CachedDefinition);
    Registry->RebuildDefinitionIndex();
    Settings->GlobalRegistry = Registry;

    UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
    GameInstance->InitializeStandalone(FName(TEXT("GameFeedbackDefinitionProviderLifetimeTestWorld")));
    UWorld* World = GameInstance->GetWorld();
    UGameFeedbackDefinitionProvider* Provider = World ? World->GetSubsystem<UGameFeedbackDefinitionProvider>() : nullptr;
    TestNotNull(TEXT("Provider 生命周期测试世界有效"), World);
    TestNotNull(TEXT("Provider 生命周期服务有效"), Provider);
    if (!World || !Provider) {
        GameInstance->Shutdown();
        return false;
    }

    FGameFeedbackSignal CachedSignal;
    CachedSignal.PrimaryChannel = CachedChannel;
    TestTrue(TEXT("首次 Resolve 必须返回已加载 Definition"), Provider->Resolve(CachedSignal) == CachedDefinition);
    const uint32 CachedDefId = Provider->GetDefinitionId(CachedDefinition);
    TestEqual(TEXT("重复 GetDefinitionId 命中缓存且结果一致"), Provider->GetDefinitionId(CachedDefinition), CachedDefId);

    TWeakObjectPtr<UGameFeedbackDefinition> CachedWeak = CachedDefinition;
    CachedDefinition = nullptr;
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("已解析 Definition 必须由 Provider 强缓存"), CachedWeak.IsValid());

    const FString AsyncPackageName = TEXT("/Game/GameFeedbackProviderLifetimeAsync");
    const FString AsyncFileName = FPackageName::LongPackageNameToFilename(AsyncPackageName, FPackageName::GetAssetPackageExtension());
    const bool bAsyncAssetAlreadyExists = IFileManager::Get().FileExists(*AsyncFileName);
    TestFalse(TEXT("异步测试不得覆盖已有资产"), bAsyncAssetAlreadyExists);
    if (bAsyncAssetAlreadyExists) {
        GameInstance->Shutdown();
        return false;
    }

    UGameFeedbackDefinition* AsyncDefinition = GameFeedbackTestHelpers::CreateDefinitionAtPath(AsyncPackageName + TEXT(".GameFeedbackProviderLifetimeAsync"));
    AsyncDefinition->SetFlags(RF_Public | RF_Standalone);
    UPackage* AsyncPackage = AsyncDefinition->GetOutermost();
    AsyncPackage->MarkAsFullyLoaded();
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bAsyncAssetSaved = UPackage::SavePackage(AsyncPackage, AsyncDefinition, *AsyncFileName, SaveArgs);
    TestTrue(TEXT("异步测试资产必须真实保存"), bAsyncAssetSaved);
    if (!bAsyncAssetSaved) {
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

    IConsoleVariable* DelegateDelayCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("s.StreamableDelegateDelayFrames"));
    const int32 PrevDelegateDelayFrames = DelegateDelayCVar ? DelegateDelayCVar->GetInt() : 1;
    if (DelegateDelayCVar) {
        DelegateDelayCVar->Set(0, ECVF_SetByCode);
    }
    ON_SCOPE_EXIT {
        if (DelegateDelayCVar) {
            DelegateDelayCVar->Set(PrevDelegateDelayFrames, ECVF_SetByCode);
        }
    };

    Provider->PreloadFeedbackBundle({AsyncSoft, TSoftObjectPtr<UGameFeedbackDefinition>()});
    Registry->DefaultFallbackDefinition = CachedWeak.Get();
    TestNull(
        TEXT("未驻留 Definition 不得回退 DefaultFallbackDefinition"),
        Provider->Resolve(AsyncSignal)
    );
    TestNull(TEXT("加载中重复 Resolve 仍返回空"), Provider->Resolve(AsyncSignal));

    FStreamableManager& StreamableManager = UAssetManager::GetStreamableManager();
    TArray<TSharedRef<FStreamableHandle>> Handles;
    StreamableManager.GetActiveHandles(AsyncPath, Handles);
    TestEqual(TEXT("同一路径连续 Resolve 只能保留一个异步句柄"), Handles.Num(), 1);
    TestEqual(TEXT("Provider 统计的在飞 Definition 预热句柄数必须为 1"), Provider->GetActivePreloadHandleCount(), 1);
    if (!Handles.IsEmpty()) {
        Handles[0]->WaitUntilComplete();
        TestTrue(TEXT("异步 Definition 加载必须真实完成"), Handles[0]->HasLoadCompleted());
        AsyncWeak = Handles[0]->GetLoadedAsset<UGameFeedbackDefinition>();
        Handles.Reset();
        TestTrue(
            TEXT("加载完成后再次 Resolve 必须返回同一已加载 Definition"),
            AsyncWeak.IsValid() && Provider->Resolve(AsyncSignal) == AsyncWeak.Get());
    }
    TestEqual(TEXT("Definition 完成回调执行后在飞句柄计数归零"), Provider->GetActivePreloadHandleCount(), 0);

    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("加载完成后 Resolve 必须写入独立保活的强缓存"), AsyncWeak.IsValid());

    const FString AsyncMediaPackageName = TEXT("/Game/GameFeedbackProviderMediaAsync");
    const FString AsyncMediaFileName = FPackageName::LongPackageNameToFilename(AsyncMediaPackageName, FPackageName::GetAssetPackageExtension());
    const bool bMediaAssetExists = IFileManager::Get().FileExists(*AsyncMediaFileName);
    TestFalse(TEXT("媒体异步测试不得覆盖已有资产"), bMediaAssetExists);
    if (bMediaAssetExists) {
        World->DestroyWorld(true);
        GameInstance->Shutdown();
        return false;
    }

    UGameFeedbackDefinition* MediaAsset = GameFeedbackTestHelpers::CreateDefinitionAtPath(
        AsyncMediaPackageName + TEXT(".GameFeedbackProviderMediaAsync"));
    MediaAsset->SetFlags(RF_Public | RF_Standalone);
    UPackage* MediaPackage = MediaAsset->GetOutermost();
    MediaPackage->MarkAsFullyLoaded();
    const bool bMediaAssetSaved = UPackage::SavePackage(MediaPackage, MediaAsset, *AsyncMediaFileName, SaveArgs);
    TestTrue(TEXT("媒体异步测试资产必须真实保存"), bMediaAssetSaved);
    if (!bMediaAssetSaved) {
        World->DestroyWorld(true);
        GameInstance->Shutdown();
        return false;
    }
    ON_SCOPE_EXIT {
        IFileManager::Get().Delete(*AsyncMediaFileName);
    };

    const FSoftObjectPath MediaPath = MediaAsset->GetPathName();
    TWeakObjectPtr<UObject> MediaWeak = MediaAsset;
    MediaAsset->ClearFlags(RF_Standalone);
    MediaPackage->ClearFlags(RF_Standalone);
    MediaAsset = nullptr;
    MediaPackage = nullptr;
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("媒体测试资产必须在 PreloadSoftAsset 前真实卸载"), MediaWeak.IsValid());

    Provider->PreloadSoftAsset(MediaPath);
    Provider->PreloadSoftAsset(MediaPath);
    TArray<TSharedRef<FStreamableHandle>> MediaHandles;
    StreamableManager.GetActiveHandles(MediaPath, MediaHandles);
    TestEqual(TEXT("PreloadSoftAsset 同路径重复调用只能保留唯一异步句柄"), MediaHandles.Num(), 1);
    TestEqual(TEXT("Provider 统计的在飞媒体预热句柄数必须为 1"), Provider->GetActivePreloadHandleCount(), 1);
    if (!MediaHandles.IsEmpty()) {
        MediaHandles[0]->WaitUntilComplete();
        TestTrue(TEXT("PreloadSoftAsset 异步加载必须真实完成"), MediaHandles[0]->HasLoadCompleted());
        MediaWeak = MediaPath.ResolveObject();
        MediaHandles.Reset();
    }
    CollectGarbage(RF_NoFlags);
    TestTrue(TEXT("PreloadSoftAsset 完成后必须由 Provider 强缓存保活"), MediaWeak.IsValid());
    TestEqual(TEXT("媒体加载完成后在飞句柄计数归零"), Provider->GetActivePreloadHandleCount(), 0);

    CachedWeak->MarkAsGarbage();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("手动标废后旧缓存对象应已回收"), CachedWeak.IsValid());
    UGameFeedbackDefinition* RebuiltCachedDef = GameFeedbackTestHelpers::CreateDefinitionAtPath(
        TEXT("/Game/Feedback/Lifetime/Cached.Cached"));
    Registry->ExactChannelMap.FindChecked(CachedChannel) = RebuiltCachedDef;
    TestEqual(
        TEXT("缓存槽位失效后不得空值中毒，再次 Resolve 必须自愈并命中新资产"),
        Provider->Resolve(CachedSignal),
        static_cast<const UGameFeedbackDefinition*>(RebuiltCachedDef));
    CachedWeak = RebuiltCachedDef;
    RebuiltCachedDef = nullptr;

    World->DestroyWorld(true);
    GameInstance->Shutdown();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Deinitialize 必须释放同步解析缓存"), CachedWeak.IsValid());
    TestFalse(TEXT("Deinitialize 必须释放异步完成缓存"), AsyncWeak.IsValid());
    TestFalse(TEXT("Deinitialize 必须释放 PreloadSoftAsset 媒体保活缓存"), MediaWeak.IsValid());
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
