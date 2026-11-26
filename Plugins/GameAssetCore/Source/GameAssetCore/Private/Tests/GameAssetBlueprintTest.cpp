/**
 * @file GameAssetBlueprintTest.cpp
 * @brief UAsyncAction_LoadGameAsset、UAsyncAction_LoadGameAssetsBatch（真实 AddDynamic 引脚绑定）与 UGameAssetBlueprintLibrary 自动化测试
 */

#include "Blueprint/AsyncAction_LoadGameAsset.h"
#include "Blueprint/AsyncAction_LoadGameAssetsBatch.h"
#include "Blueprint/UGameAssetBlueprintLibrary.h"
#include "Core/GameAssetTypes.h"
#include "Pinning/FGameAssetPinner.h"
#include "Tests/UGameAssetTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace {

struct FBpMissingBatchLatentState {
    TStrongObjectPtr<UGameInstance> AliveOwner;
    TStrongObjectPtr<UGameAssetTestReceiver> Receiver;
    int32 FramesWaited = 0;
};

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FWaitForBpMissingBatchFailureCommand,
    FAutomationTestBase&,
    TestRunner,
    TSharedRef<FBpMissingBatchLatentState>,
    SharedState
);

bool FWaitForBpMissingBatchFailureCommand::Update() {
    if (SharedState->Receiver.IsValid() && SharedState->Receiver->FailureCount > 0) {
        TestRunner.TestEqual(
            TEXT("批次包含不存在磁盘资产时在流式回调派发帧触发蓝图 OnFailure"),
            SharedState->Receiver->FailureCount,
            1
        );
        TestRunner.TestTrue(
            TEXT("失败批次触发 OnFailure 前绝不误报 OnProgress(1.0f)"),
            SharedState->Receiver->LastProgress < 1.0f
        );
        SharedState->Receiver.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }

    ++SharedState->FramesWaited;
    if (SharedState->FramesWaited > 300) {
        TestRunner.AddError(TEXT("等待蓝图批量节点缺失资产 OnFailure 回调超时"));
        SharedState->Receiver.Reset();
        SharedState->AliveOwner.Reset();
        return true;
    }
    return false;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBlueprintPriorityAndLibraryTest,
    "Game.Asset.Blueprint.PriorityAndLibrary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证优先级映射常量与 UGameAssetBlueprintLibrary 全接口在空引用、未加载引用、驻留引用及批量常驻操作下的行为
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetBlueprintPriorityAndLibraryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestEqual(TEXT("Low 优先级映射为 -50"), ToAsyncLoadPriority(EGameAssetPriority::Low), -50);
    TestEqual(TEXT("Normal 优先级映射为 0"), ToAsyncLoadPriority(EGameAssetPriority::Normal), 0);
    TestEqual(TEXT("High 优先级映射为 50"), ToAsyncLoadPriority(EGameAssetPriority::High), 50);
    TestEqual(TEXT("Critical 优先级映射为 100"), ToAsyncLoadPriority(EGameAssetPriority::Critical), 100);

    FGameAssetPinner::ClearAllPinnedAssets();
    FGameAssetPinner::Startup();

    const TSoftObjectPtr<UObject> NullSoft;
    TestFalse(TEXT("空软引用 IsAssetPinned 为假"), UGameAssetBlueprintLibrary::IsAssetPinned(NullSoft));
    TestFalse(TEXT("空软引用 PinAsset 返回假"), UGameAssetBlueprintLibrary::PinAsset(NullSoft));
    TestFalse(TEXT("空软引用 UnpinAsset 返回假"), UGameAssetBlueprintLibrary::UnpinAsset(NullSoft));
    TestEqual(TEXT("空软引用 GetAssetPinRefCount 为 0"), UGameAssetBlueprintLibrary::GetAssetPinRefCount(NullSoft), 0);
    TestNull(TEXT("空软引用 GetPinnedAsset 为 nullptr"), UGameAssetBlueprintLibrary::GetPinnedAsset(NullSoft));
    TestFalse(TEXT("空软引用 IsAssetLoaded 为假"), UGameAssetBlueprintLibrary::IsAssetLoaded(NullSoft));
    TestNull(TEXT("空软引用 GetLoadedAsset 为 nullptr"), UGameAssetBlueprintLibrary::GetLoadedAsset(NullSoft));

    const TSoftObjectPtr<UObject> UnloadedSoft(FSoftObjectPath(TEXT("/Game/__UnitTest_BpUnloaded.__UnitTest_BpUnloaded")));
    TestFalse(TEXT("未加载路径 IsAssetLoaded 为假且不触发阻塞加载"), UGameAssetBlueprintLibrary::IsAssetLoaded(UnloadedSoft));
    TestNull(TEXT("未加载路径 GetLoadedAsset 返回 nullptr"), UGameAssetBlueprintLibrary::GetLoadedAsset(UnloadedSoft));

    UGameInstance* Resident1 = NewObject<UGameInstance>();
    UGameInstance* Resident2 = NewObject<UGameInstance>();
    const TSoftObjectPtr<UObject> Soft1(Resident1);
    const TSoftObjectPtr<UObject> Soft2(Resident2);

    TestTrue(TEXT("已驻留对象 IsAssetLoaded 为真"), UGameAssetBlueprintLibrary::IsAssetLoaded(Soft1));
    TestEqual(TEXT("已驻留对象 GetLoadedAsset 返回实例本身"), UGameAssetBlueprintLibrary::GetLoadedAsset(Soft1), static_cast<UObject*>(Resident1));

    const TArray<TSoftObjectPtr<UObject>> BatchAssets = {Soft1, NullSoft, Soft2};
    TestEqual(TEXT("批量 PinAssets 忽略空项并成功锁定 2 项"), UGameAssetBlueprintLibrary::PinAssets(BatchAssets), 2);
    TestEqual(TEXT("蓝图查询总常驻种类数为 2"), UGameAssetBlueprintLibrary::GetTotalPinnedAssetCount(), 2);
    TestEqual(TEXT("蓝图导出常驻路径数为 2"), UGameAssetBlueprintLibrary::GetAllPinnedAssetPaths().Num(), 2);
    TestTrue(TEXT("Soft1 处于常驻状态"), UGameAssetBlueprintLibrary::IsAssetPinned(Soft1));
    TestEqual(TEXT("Soft1 常驻引用计数为 1"), UGameAssetBlueprintLibrary::GetAssetPinRefCount(Soft1), 1);
    TestEqual(TEXT("Soft1 GetPinnedAsset 匹配"), UGameAssetBlueprintLibrary::GetPinnedAsset(Soft1), static_cast<UObject*>(Resident1));

    TestEqual(TEXT("批量 UnpinAssets 成功解除 2 项"), UGameAssetBlueprintLibrary::UnpinAssets(BatchAssets), 2);
    TestEqual(TEXT("全部解除后常驻种类数为 0"), UGameAssetBlueprintLibrary::GetTotalPinnedAssetCount(), 0);

    FGameAssetPinner::ClearAllPinnedAssets();
    Resident1->MarkAsGarbage();
    Resident2->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBlueprintSingleActionTest,
    "Game.Asset.Blueprint.AsyncActionSingleAndClass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 使用 AddDynamic 绑定真实蓝图动态多播引脚，验证 UAsyncAction_LoadGameAsset 的单资产/类加载、非 UClass 拦截与 OnProgress 重入取消保护
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetBlueprintSingleActionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* ResidentObject = NewObject<UGameInstance>();

    UGameAssetTestReceiver* NullReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAsset* NullAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(Owner, TSoftObjectPtr<UObject>());
    NullAction->OnFailure.AddDynamic(NullReceiver, &UGameAssetTestReceiver::HandleAssetFailure);
    NullAction->Activate();
    TestEqual(TEXT("空软引用激活后触发真实蓝图 OnFailure 引脚 1 次"), NullReceiver->FailureCount, 1);

    UGameAssetTestReceiver* SuccessReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAsset* SuccessAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
        Owner,
        TSoftObjectPtr<UObject>(ResidentObject),
        EGameAssetPriority::High
    );
    SuccessAction->OnProgress.AddDynamic(SuccessReceiver, &UGameAssetTestReceiver::HandleAssetProgress);
    SuccessAction->OnSuccess.AddDynamic(SuccessReceiver, &UGameAssetTestReceiver::HandleAssetSuccess);
    SuccessAction->Activate();
    SuccessAction->Activate();
    TestEqual(TEXT("幂等 Activate 仅触发 OnSuccess 1 次"), SuccessReceiver->SuccessCount, 1);
    TestEqual(TEXT("OnProgress 汇报完成进度 1.0"), SuccessReceiver->LastProgress, 1.0f);
    TestEqual(TEXT("OnSuccess 准确交付目标 UObject"), SuccessReceiver->LastLoadedAsset.Get(), static_cast<UObject*>(ResidentObject));

    UGameAssetTestReceiver* ValidClassReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAsset* ValidClassAction = UAsyncAction_LoadGameAsset::LoadGameClassAsync(
        Owner,
        TSoftClassPtr<UObject>(UGameInstance::StaticClass())
    );
    ValidClassAction->OnSuccess.AddDynamic(ValidClassReceiver, &UGameAssetTestReceiver::HandleAssetSuccess);
    ValidClassAction->Activate();
    TestEqual(TEXT("有效软类触发 OnSuccess 1 次"), ValidClassReceiver->SuccessCount, 1);
    TestEqual(TEXT("有效软类交付 UGameInstance::StaticClass()"), ValidClassReceiver->LastLoadedAsset.Get(), static_cast<UObject*>(UGameInstance::StaticClass()));

    UGameAssetTestReceiver* InvalidClassReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAsset* InvalidClassAction = UAsyncAction_LoadGameAsset::LoadGameClassAsync(
        Owner,
        TSoftClassPtr<UObject>{FSoftObjectPath(ResidentObject)}
    );
    InvalidClassAction->OnSuccess.AddDynamic(InvalidClassReceiver, &UGameAssetTestReceiver::HandleAssetSuccess);
    InvalidClassAction->OnFailure.AddDynamic(InvalidClassReceiver, &UGameAssetTestReceiver::HandleAssetFailure);
    InvalidClassAction->Activate();
    TestEqual(TEXT("将普通对象实例传给 LoadGameClassAsync 绝不触发 OnSuccess"), InvalidClassReceiver->SuccessCount, 0);
    TestEqual(TEXT("将普通对象实例传给 LoadGameClassAsync 触发 OnFailure"), InvalidClassReceiver->FailureCount, 1);

    UGameAssetTestReceiver* ReentrantCancelReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAsset* ReentrantCancelAction = UAsyncAction_LoadGameAsset::LoadGameAssetAsync(
        Owner,
        TSoftObjectPtr<UObject>(ResidentObject)
    );
    ReentrantCancelReceiver->SingleActionToCancelOnProgress = ReentrantCancelAction;
    ReentrantCancelAction->OnProgress.AddDynamic(ReentrantCancelReceiver, &UGameAssetTestReceiver::HandleAssetProgress);
    ReentrantCancelAction->OnSuccess.AddDynamic(ReentrantCancelReceiver, &UGameAssetTestReceiver::HandleAssetSuccess);
    ReentrantCancelAction->OnFailure.AddDynamic(ReentrantCancelReceiver, &UGameAssetTestReceiver::HandleAssetFailure);
    ReentrantCancelAction->Activate();
    TestEqual(TEXT("OnProgress 已触发并执行重入 Cancel()"), ReentrantCancelReceiver->ProgressCount, 1);
    TestEqual(TEXT("OnProgress 内重入 Cancel() 后严格阻断后续 OnSuccess 广播"), ReentrantCancelReceiver->SuccessCount, 0);
    TestEqual(TEXT("主动 Cancel() 不误触 OnFailure"), ReentrantCancelReceiver->FailureCount, 0);

    Owner->MarkAsGarbage();
    ResidentObject->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameAssetBlueprintBatchActionTest,
    "Game.Asset.Blueprint.AsyncActionBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 使用 AddDynamic 验证 UAsyncAction_LoadGameAssetsBatch 的空批次/空元素拦截、去重顺序还原、OnProgress 重入取消与磁盘失败回退
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameAssetBlueprintBatchActionTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* Owner = NewObject<UGameInstance>();
    UGameInstance* Resident1 = NewObject<UGameInstance>();
    UGameInstance* Resident2 = NewObject<UGameInstance>();

    UGameAssetTestReceiver* EmptyReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAssetsBatch* EmptyAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(Owner, {});
    EmptyAction->OnFailure.AddDynamic(EmptyReceiver, &UGameAssetTestReceiver::HandleAssetFailure);
    EmptyAction->Activate();
    TestEqual(TEXT("空数组激活触发蓝图 OnFailure"), EmptyReceiver->FailureCount, 1);

    UGameAssetTestReceiver* NullElemReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAssetsBatch* NullElemAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        Owner,
        {TSoftObjectPtr<UObject>(Resident1), TSoftObjectPtr<UObject>()}
    );
    NullElemAction->OnFailure.AddDynamic(NullElemReceiver, &UGameAssetTestReceiver::HandleAssetFailure);
    NullElemAction->Activate();
    TestEqual(TEXT("含空软引用元素触发蓝图 OnFailure"), NullElemReceiver->FailureCount, 1);

    UGameAssetTestReceiver* BatchSuccessReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    const TArray<TSoftObjectPtr<UObject>> DupBatch = {
        TSoftObjectPtr<UObject>(Resident1),
        TSoftObjectPtr<UObject>(Resident2),
        TSoftObjectPtr<UObject>(Resident1)
    };
    UAsyncAction_LoadGameAssetsBatch* BatchSuccessAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        Owner,
        DupBatch,
        EGameAssetPriority::Critical
    );
    BatchSuccessAction->OnProgress.AddDynamic(BatchSuccessReceiver, &UGameAssetTestReceiver::HandleAssetProgress);
    BatchSuccessAction->OnSuccess.AddDynamic(BatchSuccessReceiver, &UGameAssetTestReceiver::HandleBatchSuccess);
    BatchSuccessAction->Activate();
    TestEqual(TEXT("含重复项批次成功触发 OnSuccess 1 次"), BatchSuccessReceiver->BatchSuccessCount, 1);
    TestEqual(TEXT("批次完成进度汇报 1.0"), BatchSuccessReceiver->LastProgress, 1.0f);
    TestEqual(TEXT("批次输出按原输入保留 3 个顺序元素"), BatchSuccessReceiver->LastLoadedBatch.Num(), 3);
    if (BatchSuccessReceiver->LastLoadedBatch.Num() == 3) {
        TestEqual(TEXT("元素 0 匹配 Resident1"), BatchSuccessReceiver->LastLoadedBatch[0].Get(), static_cast<UObject*>(Resident1));
        TestEqual(TEXT("元素 1 匹配 Resident2"), BatchSuccessReceiver->LastLoadedBatch[1].Get(), static_cast<UObject*>(Resident2));
        TestEqual(TEXT("元素 2 匹配 Resident1"), BatchSuccessReceiver->LastLoadedBatch[2].Get(), static_cast<UObject*>(Resident1));
    }

    UGameAssetTestReceiver* ReentrantBatchReceiver = NewObject<UGameAssetTestReceiver>(Owner);
    UAsyncAction_LoadGameAssetsBatch* ReentrantBatchAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        Owner,
        DupBatch
    );
    ReentrantBatchReceiver->BatchActionToCancelOnProgress = ReentrantBatchAction;
    ReentrantBatchAction->OnProgress.AddDynamic(ReentrantBatchReceiver, &UGameAssetTestReceiver::HandleAssetProgress);
    ReentrantBatchAction->OnSuccess.AddDynamic(ReentrantBatchReceiver, &UGameAssetTestReceiver::HandleBatchSuccess);
    ReentrantBatchAction->Activate();
    TestEqual(TEXT("批量节点 OnProgress 内重入 Cancel() 阻断后续 OnSuccess"), ReentrantBatchReceiver->BatchSuccessCount, 0);

    TSharedRef<FBpMissingBatchLatentState> LatentState = MakeShared<FBpMissingBatchLatentState>();
    LatentState->AliveOwner = TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
    LatentState->Receiver = TStrongObjectPtr<UGameAssetTestReceiver>(NewObject<UGameAssetTestReceiver>(LatentState->AliveOwner.Get()));

    UAsyncAction_LoadGameAssetsBatch* MissingBatchAction = UAsyncAction_LoadGameAssetsBatch::LoadGameAssetsBatchAsync(
        LatentState->AliveOwner.Get(),
        {TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/__UnitTest_BpBatchMissing.__UnitTest_BpBatchMissing")))}
    );
    MissingBatchAction->OnProgress.AddDynamic(LatentState->Receiver.Get(), &UGameAssetTestReceiver::HandleAssetProgress);
    MissingBatchAction->OnFailure.AddDynamic(LatentState->Receiver.Get(), &UGameAssetTestReceiver::HandleAssetFailure);
    MissingBatchAction->Activate();
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForBpMissingBatchFailureCommand(*this, LatentState));

    Owner->MarkAsGarbage();
    Resident1->MarkAsGarbage();
    Resident2->MarkAsGarbage();
    return true;
}

#endif
