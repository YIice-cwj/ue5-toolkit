/**
 * @file AsyncAction_LoadGameAssetTest.cpp
 * @brief UAsyncAction_LoadGameAsset 蓝图异步加载动作节点与优先级映射自动化测试
 */

#include "Asset/AsyncAction_LoadGameAsset.h"
#include "Asset/AsyncAction_LoadGameAssetsBatch.h"
#include "Asset/GameAssetTypes.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetAsyncActionPriorityTest,
    "Game.Asset.AsyncAction.PriorityMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 EGameAssetPriority 业务语义优先级到底层数值的精确转换契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetAsyncActionPriorityTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(TEXT("Low 优先级映射为 -50"), ToAsyncLoadPriority(EGameAssetPriority::Low), -50);
    TestEqual(TEXT("Normal 优先级映射为默认 0"), ToAsyncLoadPriority(EGameAssetPriority::Normal), 0);
    TestEqual(TEXT("High 优先级映射为 50"), ToAsyncLoadPriority(EGameAssetPriority::High), 50);
    TestEqual(TEXT("Critical 优先级映射为 100"), ToAsyncLoadPriority(EGameAssetPriority::Critical), 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetAsyncActionObjectLoadingTest,
    "Game.Asset.AsyncAction.ObjectLoading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UAsyncAction_LoadGameAsset 对资产软引用的空拦截、驻留同步成功派发与主动取消
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetAsyncActionObjectLoadingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget = NewObject<UGameInstance>();

    bool bNullActionFailed = false;
    UAsyncAction_LoadGameAsset* NullAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
        DummyOwner,
        TSoftObjectPtr<UObject>()
    );
    TestNotNull(TEXT("空软引用创建 AsyncAction 实例有效"), NullAction);
    if (NullAction) {
        NullAction->OnFailureNative.AddLambda([&bNullActionFailed]() {
            bNullActionFailed = true;
        });
        NullAction->Activate();
        TestTrue(TEXT("空软引用激活后立即触发 OnFailure"), bNullActionFailed);
    }

    bool bResidentActionSuccess = false;
    float ResidentProgress = 0.0f;
    UObject* LoadedResident = nullptr;
    const TSoftObjectPtr<UObject> ResidentSoft(ResidentTarget);
    UAsyncAction_LoadGameAsset* ResidentAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
        DummyOwner,
        ResidentSoft,
        EGameAssetPriority::High
    );
    TestNotNull(TEXT("驻留软引用创建 AsyncAction 实例有效"), ResidentAction);
    if (ResidentAction) {
        ResidentAction->OnSuccessNative.AddLambda([&bResidentActionSuccess, &LoadedResident](UObject* Asset) {
            bResidentActionSuccess = true;
            LoadedResident = Asset;
        });
        ResidentAction->OnProgressNative.AddLambda([&ResidentProgress](float Progress) {
            ResidentProgress = Progress;
        });
        ResidentAction->Activate();
        TestTrue(TEXT("驻留软引用激活后立即触发 OnSuccess"), bResidentActionSuccess);
        TestEqual(TEXT("驻留软引用回调解析出正确目标对象"), LoadedResident, static_cast<UObject*>(ResidentTarget));
        TestEqual(TEXT("驻留软引用汇报进度为 1.0"), ResidentProgress, 1.0f);
    }

    UAsyncAction_LoadGameAsset* CancelAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
        DummyOwner,
        TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/TestUnloaded.TestUnloaded")))
    );
    if (CancelAction) {
        CancelAction->Activate();
        CancelAction->Cancel();
    }

    DummyOwner->MarkAsGarbage();
    ResidentTarget->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetAsyncActionClassLoadingTest,
    "Game.Asset.AsyncAction.ClassLoading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UAsyncAction_LoadGameAsset 对类软引用的加载与 UClass 回调
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetAsyncActionClassLoadingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();

    bool bResidentClassActionSuccess = false;
    UObject* LoadedResidentClass = nullptr;
    const TSoftClassPtr<UObject> ResidentClassSoft(UGameInstance::StaticClass());
    UAsyncAction_LoadGameAsset* ClassAction = UAsyncAction_LoadGameAsset::LoadGameClassAsync(
        DummyOwner,
        ResidentClassSoft
    );
    TestNotNull(TEXT("驻留软类引用创建 AsyncAction 实例有效"), ClassAction);
    if (ClassAction) {
        ClassAction->OnSuccessNative.AddLambda([&bResidentClassActionSuccess, &LoadedResidentClass](UObject* Asset) {
            bResidentClassActionSuccess = true;
            LoadedResidentClass = Asset;
        });
        ClassAction->Activate();
        TestTrue(TEXT("驻留软类引用激活后立即触发 OnSuccess"), bResidentClassActionSuccess);
        TestEqual(TEXT("驻留软类引用解析出正确 UClass"), LoadedResidentClass, static_cast<UObject*>(UGameInstance::StaticClass()));
    }

    DummyOwner->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetAsyncActionBatchLoadingTest,
    "Game.Asset.AsyncAction.BatchLoading",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 UAsyncAction_LoadGameAssetsBatch 批量软引用异步加载节点的错误拦截、全驻留直达与主动取消
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetAsyncActionBatchLoadingTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* DummyOwner = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget1 = NewObject<UGameInstance>();
    UGameInstance* ResidentTarget2 = NewObject<UGameInstance>();

    bool bEmptyActionFailed = false;
    UAsyncAction_LoadGameAssetsBatch* EmptyAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        DummyOwner,
        {}
    );
    TestNotNull(TEXT("空数组创建 BatchAction 实例有效"), EmptyAction);
    if (EmptyAction) {
        EmptyAction->OnFailureNative.AddLambda([&bEmptyActionFailed]() {
            bEmptyActionFailed = true;
        });
        EmptyAction->Activate();
        TestTrue(TEXT("空数组激活后立即触发 OnFailure"), bEmptyActionFailed);
    }

    bool bNullElementFailed = false;
    UAsyncAction_LoadGameAssetsBatch* NullElementAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        DummyOwner,
        {TSoftObjectPtr<UObject>(ResidentTarget1), TSoftObjectPtr<UObject>()}
    );
    TestNotNull(TEXT("含空软引用的 BatchAction 实例有效"), NullElementAction);
    if (NullElementAction) {
        NullElementAction->OnFailureNative.AddLambda([&bNullElementFailed]() {
            bNullElementFailed = true;
        });
        NullElementAction->Activate();
        TestTrue(TEXT("含空软引用激活后立即触发 OnFailure"), bNullElementFailed);
    }

    bool bResidentSuccess = false;
    float ResidentProgress = 0.0f;
    TArray<UObject*> LoadedAssets;
    const TArray<TSoftObjectPtr<UObject>> Targets = {
        TSoftObjectPtr<UObject>(ResidentTarget1),
        TSoftObjectPtr<UObject>(ResidentTarget2)
    };

    UAsyncAction_LoadGameAssetsBatch* ResidentAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        DummyOwner,
        Targets,
        EGameAssetPriority::High
    );
    TestNotNull(TEXT("驻留批次创建 BatchAction 实例有效"), ResidentAction);
    if (ResidentAction) {
        ResidentAction->OnSuccessNative.AddLambda([&bResidentSuccess, &LoadedAssets](const TArray<UObject*>& InAssets) {
            bResidentSuccess = true;
            LoadedAssets = InAssets;
        });
        ResidentAction->OnProgressNative.AddLambda([&ResidentProgress](float Progress) {
            ResidentProgress = Progress;
        });
        ResidentAction->Activate();
        TestTrue(TEXT("全驻留批次激活后立即触发 OnSuccess"), bResidentSuccess);
        TestEqual(TEXT("全驻留批次汇报进度为 1.0"), ResidentProgress, 1.0f);
        TestEqual(TEXT("全驻留批次返回元素数量为 2"), LoadedAssets.Num(), 2);
        if (LoadedAssets.Num() == 2) {
            TestEqual(TEXT("第一个返回元素与目标1严格匹配"), LoadedAssets[0], static_cast<UObject*>(ResidentTarget1));
            TestEqual(TEXT("第二个返回元素与目标2严格匹配"), LoadedAssets[1], static_cast<UObject*>(ResidentTarget2));
        }
    }

    UAsyncAction_LoadGameAssetsBatch* CancelAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        DummyOwner,
        {TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/TestUnloaded1.TestUnloaded1")))}
    );
    if (CancelAction) {
        CancelAction->Activate();
        CancelAction->Cancel();
    }

    DummyOwner->MarkAsGarbage();
    ResidentTarget1->MarkAsGarbage();
    ResidentTarget2->MarkAsGarbage();
    return true;
}

#endif
