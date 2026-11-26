/**
 * @file LoadingSubsystemTest.cpp
 * @brief ULoadingSubsystem 状态机、多 PIE 隔离、遥测进度、看门狗与蓝图异步动作节点全分支自动化测试
 */

#include "Blueprint/AsyncAction_WaitForLoadingComplete.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/GameFlowTestUtils.h"
#include "Tests/ULoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include <limits>

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

using Game::Flow::Test::FScopedTestLoadingWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemStateFlowTest,
    "Game.Flow.System.LoadingSubsystemStateFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证上下文解析、状态阶段流转、多 PIE/预览世界隔离、失败兜底与 Deinitialize 中止语义
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemStateFlowTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingSubsystemStateFlowWorld")));
    TestTrue(TEXT("测试世界与加载子系统初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    UGameInstance* GameInstance = TestWorld.GetGameInstance();
    UWorld* World = TestWorld.GetWorld();

    UObject* OuterOwnedObj = NewObject<ULoadingTestReceiver>(GameInstance);
    ULoadingTestReceiver* GarbageObj = NewObject<ULoadingTestReceiver>(GameInstance);
    GarbageObj->MarkAsGarbage();

    TestEqual(TEXT("从 GameInstance 获取子系统一致"), ULoadingSubsystem::Get(GameInstance), LoadingSubsystem);
    TestEqual(TEXT("从 World 获取子系统一致"), ULoadingSubsystem::Get(World), LoadingSubsystem);
    TestEqual(TEXT("从子系统自身获取子系统一致"), ULoadingSubsystem::Get(LoadingSubsystem), LoadingSubsystem);
    TestEqual(TEXT("从 GameInstance Outer 链对象获取子系统一致"), ULoadingSubsystem::Get(OuterOwnedObj), LoadingSubsystem);
    TestNull(TEXT("空上下文获取子系统返回 nullptr"), ULoadingSubsystem::Get(nullptr));
    TestNull(TEXT("已标记垃圾回收的上下文对象获取子系统返回 nullptr"), ULoadingSubsystem::Get(GarbageObj));

    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingStateChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    TestFalse(TEXT("初始状态未处于加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("初始阶段为 Idle"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::Idle);
    TestEqual(TEXT("初始目标关卡应为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);

    {
        FScopedTestLoadingWorld OtherWorld(FName(TEXT("OtherPieIsolationWorld")));
        TestTrue(TEXT("对照 PIE 实例初始化成功"), OtherWorld.IsValid());
        if (OtherWorld.IsValid()) {
            TestTrue(TEXT("对照 PIE 广播 PreLoadMapWithContext 成功"), OtherWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/OtherPieMap")));
            TestFalse(TEXT("异源 PIE PreLoadMapWithContext 绝不污染本子系统进入加载态"), LoadingSubsystem->IsLoading());
            TestEqual(TEXT("异源 PIE 切图后本子系统仍为 Idle"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::Idle);
            TestTrue(TEXT("对照 PIE 自身正常进入加载态"), OtherWorld.GetSubsystem()->IsLoading());
            OtherWorld.BroadcastPostLoadMap();
            TestFalse(TEXT("对照 PIE 自身正常完成加载"), OtherWorld.GetSubsystem()->IsLoading());
        }
    }

    const FString TestLevelName = TEXT("/Game/Maps/UnitTestLevel_Alpha");
    TestTrue(TEXT("广播本实例 PreLoadMapWithContext 成功"), TestWorld.BroadcastPreLoadMap(TestLevelName));

    TestTrue(TEXT("PreLoadMapWithContext 触发后进入加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("加载阶段进入 LoadingMap"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::LoadingMap);
    TestEqual(TEXT("目标关卡名称更新为当前加载地图"), LoadingSubsystem->GetTargetLevelName(), FName(*TestLevelName));
    TestEqual(TEXT("收到加载开始委托广播"), Receiver->LoadingChangeCount, 1);
    TestTrue(TEXT("广播中加载状态为 true"), Receiver->bLastIsLoading);
    TestEqual(TEXT("广播中目标关卡一致"), Receiver->LastTargetLevel, FName(*TestLevelName));

    LoadingSubsystem->OpenLevelWithLoading(FName(TEXT("/Game/Maps/UnitTestLevel_Beta")));
    TestEqual(TEXT("加载中重复请求不改写当前目标关卡"), LoadingSubsystem->GetTargetLevelName(), FName(*TestLevelName));
    TestEqual(TEXT("重复请求不产生额外广播"), Receiver->LoadingChangeCount, 1);

    {
        FScopedTestLoadingWorld OtherWorld(FName(TEXT("OtherPostLoadWorld")));
        if (OtherWorld.IsValid()) {
            FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(OtherWorld.GetWorld());
            TestTrue(TEXT("异源世界加载完成不误清本子系统的加载态"), LoadingSubsystem->IsLoading());
        }
    }

    if (UWorld* PreviewWorld = UWorld::CreateWorld(EWorldType::EditorPreview, false, FName(TEXT("GameFlowPreviewWorld")))) {
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(PreviewWorld);
        TestTrue(TEXT("无 GameInstance 的编辑器预览世界加载完成绝不误清本子系统加载态"), LoadingSubsystem->IsLoading());
        PreviewWorld->DestroyWorld(false);
    }

    TestWorld.BroadcastPostLoadMap();
    TestFalse(TEXT("本实例世界加载完成后退出加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("加载完成后阶段回归 Idle"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::Idle);
    TestEqual(TEXT("加载完成后目标关卡复位为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);
    TestEqual(TEXT("收到加载结束委托广播"), Receiver->LoadingChangeCount, 2);
    TestFalse(TEXT("广播中加载状态为 false"), Receiver->bLastIsLoading);
    TestEqual(TEXT("结束广播中附带完成的关卡名称"), Receiver->LastTargetLevel, FName(*TestLevelName));

    LoadingSubsystem->OpenLevelWithLoading(NAME_None);
    TestFalse(TEXT("空关卡名不进入加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("空关卡名不产生广播"), Receiver->LoadingChangeCount, 2);

    TestTrue(TEXT("再次进入加载以验证地图失败空指针兜底"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/FailedMap")));
    TestTrue(TEXT("进入加载态"), LoadingSubsystem->IsLoading());
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(nullptr);
    TestFalse(TEXT("PostLoadMapWithWorld(nullptr) 地图加载失败兜底退出加载态防永久卡屏"), LoadingSubsystem->IsLoading());

    int32 IdleShutdownStateCount = 0;
    int32 IdleShutdownAbortCount = 0;
    LoadingSubsystem->OnLoadingStateChangedNative.AddLambda([&IdleShutdownStateCount](bool, FName) {
        ++IdleShutdownStateCount;
    });
    LoadingSubsystem->OnLoadingAbortedNative.AddLambda([&IdleShutdownAbortCount]() {
        ++IdleShutdownAbortCount;
    });
    TestWorld.ShutdownNow();
    TestEqual(TEXT("空闲态 Deinitialize 不发送幽灵加载状态变更事件"), IdleShutdownStateCount, 0);
    TestEqual(TEXT("空闲态 Deinitialize 不触发加载中止事件"), IdleShutdownAbortCount, 0);

    {
        FScopedTestLoadingWorld AbortWorld(FName(TEXT("DeinitAbortWorld")));
        TestTrue(TEXT("中止测试世界初始化成功"), AbortWorld.IsValid());
        if (AbortWorld.IsValid()) {
            int32 ActiveShutdownStateCount = 0;
            int32 ActiveShutdownAbortCount = 0;
            AbortWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/AbortDuringLoad"));
            AbortWorld.GetSubsystem()->OnLoadingStateChangedNative.AddLambda([&ActiveShutdownStateCount](bool, FName) {
                ++ActiveShutdownStateCount;
            });
            AbortWorld.GetSubsystem()->OnLoadingAbortedNative.AddLambda([&ActiveShutdownAbortCount]() {
                ++ActiveShutdownAbortCount;
            });
            AbortWorld.ShutdownNow();
            TestEqual(TEXT("加载期 Deinitialize 不误发正常完成事件"), ActiveShutdownStateCount, 0);
            TestEqual(TEXT("加载期 Deinitialize 精准派发一次加载中止事件"), ActiveShutdownAbortCount, 1);
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingBlueprintActionAndBpBarrierTest,
    "Game.Flow.System.LoadingBlueprintActionAndBpBarrier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证蓝图屏障全套接口（含进度上报与边界校验）及 UAsyncAction_WaitForLoadingComplete 真实动态引脚与生命周期
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBlueprintActionAndBpBarrierTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingBpActionTestWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    UGameInstance* GameInstance = TestWorld.GetGameInstance();
    UWorld* World = TestWorld.GetWorld();

    const int64 BarrierId = LoadingSubsystem->AcquireLoadingBarrierBp(FName(TEXT("BpBarrier1")), 2.0f);
    TestTrue(TEXT("蓝图屏障句柄非零"), BarrierId != 0);
    TestEqual(TEXT("活跃屏障数为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);

    TestFalse(TEXT("空句柄上报进度返回 false"), LoadingSubsystem->ReportBarrierProgressBp(0, 0.5f, FText::GetEmpty()));
    TestFalse(TEXT("非法句柄上报进度返回 false"), LoadingSubsystem->ReportBarrierProgressBp(-1, 0.5f, FText::GetEmpty()));
    TestFalse(TEXT("NaN 进度上报被拒绝返回 false"), LoadingSubsystem->ReportBarrierProgressBp(BarrierId, std::numeric_limits<float>::quiet_NaN(), FText::GetEmpty()));
    TestFalse(TEXT("Inf 进度上报被拒绝返回 false"), LoadingSubsystem->ReportBarrierProgressBp(BarrierId, std::numeric_limits<float>::infinity(), FText::GetEmpty()));
    TestTrue(TEXT("合法进度上报返回 true"), LoadingSubsystem->ReportBarrierProgressBp(BarrierId, 0.5f, FText::FromString(TEXT("蓝图进度"))));

    TestFalse(TEXT("释放空 ID 屏障返回 false"), LoadingSubsystem->ReleaseLoadingBarrierBp(0));
    TestFalse(TEXT("释放不存在的负 ID 屏障返回 false"), LoadingSubsystem->ReleaseLoadingBarrierBp(-1));
    TestFalse(TEXT("释放不存在的伪造 ID 屏障返回 false"), LoadingSubsystem->ReleaseLoadingBarrierBp(99999));
    TestTrue(TEXT("释放有效蓝图屏障返回 true"), LoadingSubsystem->ReleaseLoadingBarrierBp(BarrierId));
    TestFalse(TEXT("重复释放同一蓝图屏障返回 false（代际防重放）"), LoadingSubsystem->ReleaseLoadingBarrierBp(BarrierId));
    TestFalse(TEXT("已释放句柄再上报进度返回 false"), LoadingSubsystem->ReportBarrierProgressBp(BarrierId, 0.8f, FText::GetEmpty()));
    TestEqual(TEXT("释放后活跃屏障数为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    ULoadingTestReceiver* DirectReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    UAsyncAction_WaitForLoadingComplete* DirectAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(GameInstance);
    TestNotNull(TEXT("从 GameInstance 创建异步等待节点有效"), DirectAction);
    if (DirectAction != nullptr) {
        DirectAction->OnCompleted.AddDynamic(DirectReceiver, &ULoadingTestReceiver::OnActionCompleted);
        DirectAction->OnCancelled.AddDynamic(DirectReceiver, &ULoadingTestReceiver::OnActionCancelled);
        DirectAction->Activate();
        DirectAction->Activate();
        TestEqual(TEXT("非加载态下同步完成且重复 Activate 幂等只触发一次"), DirectReceiver->ActionCompletedCount, 1);
        TestEqual(TEXT("非加载态完成不触发 OnCancelled"), DirectReceiver->ActionCancelledCount, 0);
    }

    ULoadingTestReceiver* NullCtxReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    UAsyncAction_WaitForLoadingComplete* NullCtxAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(nullptr);
    TestNotNull(TEXT("空上下文节点创建成功"), NullCtxAction);
    if (NullCtxAction != nullptr) {
        NullCtxAction->OnCompleted.AddDynamic(NullCtxReceiver, &ULoadingTestReceiver::OnActionCompleted);
        NullCtxAction->OnCancelled.AddDynamic(NullCtxReceiver, &ULoadingTestReceiver::OnActionCancelled);
        NullCtxAction->Activate();
        TestEqual(TEXT("空上下文绝不误触发 OnCompleted"), NullCtxReceiver->ActionCompletedCount, 0);
        TestEqual(TEXT("空上下文精准触发一次 OnCancelled"), NullCtxReceiver->ActionCancelledCount, 1);
    }

    ULoadingTestReceiver* PreCancelReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    UAsyncAction_WaitForLoadingComplete* PreCancelAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    if (PreCancelAction != nullptr) {
        PreCancelAction->OnCompleted.AddDynamic(PreCancelReceiver, &ULoadingTestReceiver::OnActionCompleted);
        PreCancelAction->OnCancelled.AddDynamic(PreCancelReceiver, &ULoadingTestReceiver::OnActionCancelled);
        PreCancelAction->Cancel();
        PreCancelAction->Activate();
        TestEqual(TEXT("先 Cancel 再 Activate 不触发 OnCompleted"), PreCancelReceiver->ActionCompletedCount, 0);
        TestEqual(TEXT("先 Cancel 再 Activate 仅触发一次 OnCancelled"), PreCancelReceiver->ActionCancelledCount, 1);
    }

    const FString TestLevel = TEXT("/Game/Maps/AsyncActionLevel");
    TestTrue(TEXT("进入加载态"), TestWorld.BroadcastPreLoadMap(TestLevel));
    const int64 HoldBarrier = LoadingSubsystem->AcquireLoadingBarrierBp(FName(TEXT("HoldingForAction")), 2.0f);

    ULoadingTestReceiver* ProgressReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingProgressChanged.AddDynamic(ProgressReceiver, &ULoadingTestReceiver::OnLoadingProgressChanged);
    TestTrue(TEXT("加载态蓝图进度上报成功"), LoadingSubsystem->ReportBarrierProgressBp(HoldBarrier, 0.5f, FText::FromString(TEXT("预热资源"))));
    TestEqual(TEXT("蓝图进度说明同步广播"), ProgressReceiver->LastStepDescription, TEXT("预热资源"));
    TestTrue(TEXT("蓝图进度推动全局进度"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 0.25f, 0.01f));

    ULoadingTestReceiver* WaitReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    UAsyncAction_WaitForLoadingComplete* WaitingAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    if (WaitingAction != nullptr) {
        WaitingAction->OnCompleted.AddDynamic(WaitReceiver, &ULoadingTestReceiver::OnActionCompleted);
        WaitingAction->OnCancelled.AddDynamic(WaitReceiver, &ULoadingTestReceiver::OnActionCancelled);
        WaitReceiver->OnActionCompletedHook = [WaitingAction]() {
            WaitingAction->Cancel();
        };
        WaitingAction->Activate();
        WaitingAction->Activate();
    }

    ULoadingTestReceiver* MidCancelReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    UAsyncAction_WaitForLoadingComplete* MidCancelAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    if (MidCancelAction != nullptr) {
        MidCancelAction->OnCompleted.AddDynamic(MidCancelReceiver, &ULoadingTestReceiver::OnActionCompleted);
        MidCancelAction->OnCancelled.AddDynamic(MidCancelReceiver, &ULoadingTestReceiver::OnActionCancelled);
        MidCancelAction->Activate();
        MidCancelAction->Cancel();
        TestEqual(TEXT("挂起期 Cancel 立即触发一次 OnCancelled"), MidCancelReceiver->ActionCancelledCount, 1);
    }

    TestWorld.BroadcastPostLoadMap();
    TestTrue(TEXT("屏障未释放前仍然处于加载中"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("屏障未释放前阶段为 WaitingBarriers"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::WaitingBarriers);
    TestEqual(TEXT("屏障未释放前异步动作未触发完成"), WaitReceiver->ActionCompletedCount, 0);

    LoadingSubsystem->ReleaseLoadingBarrierBp(HoldBarrier);
    TestFalse(TEXT("屏障释放后加载完成"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("屏障释放后异步动作仅触发一次 OnCompleted（含重入防抖）"), WaitReceiver->ActionCompletedCount, 1);
    TestEqual(TEXT("完成回调内重入 Cancel 不再触发 OnCancelled"), WaitReceiver->ActionCancelledCount, 0);
    TestEqual(TEXT("已取消节点在加载结束后绝不触发 OnCompleted"), MidCancelReceiver->ActionCompletedCount, 0);

    ULoadingTestReceiver* DeinitReceiver = NewObject<ULoadingTestReceiver>(GameInstance);
    TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/DeinitNodeLevel"));
    UAsyncAction_WaitForLoadingComplete* DeinitAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    if (DeinitAction != nullptr) {
        DeinitAction->OnCompleted.AddDynamic(DeinitReceiver, &ULoadingTestReceiver::OnActionCompleted);
        DeinitAction->OnCancelled.AddDynamic(DeinitReceiver, &ULoadingTestReceiver::OnActionCancelled);
        DeinitAction->Activate();
    }
    TestWorld.ShutdownNow();
    TestEqual(TEXT("子系统销毁中止挂起节点不误触发 OnCompleted"), DeinitReceiver->ActionCompletedCount, 0);
    TestEqual(TEXT("子系统销毁中止挂起节点精准触发 OnCancelled"), DeinitReceiver->ActionCancelledCount, 1);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemProgressAndTelemetryTest,
    "Game.Flow.System.LoadingProgressAndTelemetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证加权进度精确换算、非有限参数净化、非加载态零遥测泄漏、动态/原生遥测双轨广播与多轮隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemProgressAndTelemetryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingProgressTelemetryWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    UGameInstance* GameInstance = TestWorld.GetGameInstance();

    {
        FLoadingBarrierToken IdleToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("IdleNonLoadingBarrier")), 3.0f);
        IdleToken.Release();
    }

    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingProgressChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingProgressChanged);
    LoadingSubsystem->OnLoadingTelemetryReport.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingTelemetryReport);

    float LastNativeProgress = -1.0f;
    FString LastNativeDescription;
    int32 FullProgressBroadcastCount = 0;
    LoadingSubsystem->OnLoadingProgressChangedNative.AddLambda(
        [&LastNativeProgress, &LastNativeDescription, &FullProgressBroadcastCount](float Progress, const FText& Desc) {
            LastNativeProgress = Progress;
            LastNativeDescription = Desc.ToString();
            if (FMath::IsNearlyEqual(Progress, 1.0f, 0.0001f)) {
                ++FullProgressBroadcastCount;
            }
        }
    );

    FLoadingTelemetryReport CapturedNativeReport;
    int32 NativeReportCount = 0;
    LoadingSubsystem->OnLoadingTelemetryReportNative.AddLambda([&CapturedNativeReport, &NativeReportCount](const FLoadingTelemetryReport& Report) {
        CapturedNativeReport = Report;
        ++NativeReportCount;
    });

    const FString TestLevel = TEXT("/Game/Maps/ProgressLevel");
    TestTrue(TEXT("进入第一轮加载"), TestWorld.BroadcastPreLoadMap(TestLevel));
    TestTrue(TEXT("加载开始进度为 0.0f"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 0.0f, 0.001f));

    FLoadingBarrierToken TokenA = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("BarrierAlpha")), 2.0f);
    FLoadingBarrierToken TokenB = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("BarrierBeta")), 2.0f);

    TokenA.ReportProgress(0.5f, FText::FromString(TEXT("预热网格体")));
    TestTrue(TEXT("精确加权进度为 1/6"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 1.0f / 6.0f, 0.005f));
    TestTrue(TEXT("蓝图与原生进度一致"), FMath::IsNearlyEqual(Receiver->LastProgress, LastNativeProgress, 0.001f));
    TestEqual(TEXT("原生步骤说明正确广播"), LastNativeDescription, TEXT("预热网格体"));
    TestEqual(TEXT("蓝图步骤说明正确广播"), Receiver->LastStepDescription, TEXT("预热网格体"));

    TokenA.Release();
    TestTrue(TEXT("TokenA 释放后精确加权进度为 2/6（权重守恒不回退）"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 2.0f / 6.0f, 0.005f));

    TestWorld.BroadcastPostLoadMap();
    TestTrue(TEXT("地图加载完成后精确加权进度为 4/6"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 4.0f / 6.0f, 0.005f));

    TokenB.Release();
    TestFalse(TEXT("全部屏障释放后加载完成"), LoadingSubsystem->IsLoading());
    TestTrue(TEXT("全局进度达到 1.0f"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 1.0f, 0.001f));
    TestEqual(TEXT("最后一票屏障释放结束时 1.0f 进度仅精确广播 1 次（绝不双播）"), FullProgressBroadcastCount, 1);
    TestEqual(TEXT("原生遥测报告派发 1 次"), NativeReportCount, 1);
    TestEqual(TEXT("蓝图遥测报告派发 1 次"), Receiver->TelemetryReportCount, 1);
    TestEqual(TEXT("遥测报告关卡名正确"), CapturedNativeReport.LevelName, FName(*TestLevel));
    TestFalse(TEXT("正常完成遥测 bTimedOut 为 false"), CapturedNativeReport.bTimedOut);
    TestEqual(TEXT("遥测报告仅包含本轮 2 个屏障（不含非加载态泄漏记录）"), CapturedNativeReport.Barriers.Num(), 2);
    TestEqual(TEXT("遥测报告中屏障记录保留权重"), CapturedNativeReport.Barriers[0].Weight, 2.0f);
    TestEqual(TEXT("GetLastTelemetryReport 与广播报告一致"), LoadingSubsystem->GetLastTelemetryReport().LevelName, FName(*TestLevel));
    TestEqual(TEXT("蓝图遥测报告与原生一致"), Receiver->LastTelemetryReport.Barriers.Num(), 2);

    TestTrue(TEXT("进入第二轮加载验证状态干净重置"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/ProgressLevel_2")));
    FLoadingBarrierToken SanitizedToken = LoadingSubsystem->AcquireBarrierToken(
        FName(TEXT("SanitizedWeightBarrier")),
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
    );
    FLoadingBarrierToken MinWeightToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("MinWeightBarrier")), -5.0f);
    SanitizedToken.Release();
    MinWeightToken.Release();
    TestWorld.BroadcastPostLoadMap();

    const FLoadingTelemetryReport& SecondReport = LoadingSubsystem->GetLastTelemetryReport();
    TestEqual(TEXT("第二轮无屏障 PostLoadMap 结束时 1.0f 进度累计精确为 2 次（零双播）"), FullProgressBroadcastCount, 2);
    TestEqual(TEXT("第二轮遥测仅含本轮 2 个屏障"), SecondReport.Barriers.Num(), 2);
    TestTrue(TEXT("NaN 权重回落为默认 1.0f"), FMath::IsNearlyEqual(SecondReport.Barriers[0].Weight, 1.0f, 0.001f));
    TestTrue(TEXT("负权重截断为下限 0.1f"), FMath::IsNearlyEqual(SecondReport.Barriers[1].Weight, 0.1f, 0.001f));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemWatchdogTimeoutTest,
    "Game.Flow.System.LoadingWatchdogTimeout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单屏障超时（含全局关闭独立生效）、单屏障收尾揭幕与全局看门狗强制总熔断遥测
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemWatchdogTimeoutTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingWatchdogTestWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();

    LoadingSubsystem->CheckWatchdogTimeout();
    TestFalse(TEXT("空闲态巡检保持非加载"), LoadingSubsystem->IsLoading());

    LoadingSubsystem->SetMaxLoadingTimeoutSeconds(std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("非有限全局超时被安全置零关闭"), LoadingSubsystem->GetMaxLoadingTimeoutSeconds(), 0.0f);

    TestTrue(TEXT("全局关闭状态下进入加载"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/PerBarrierWatchdog")));

    FLoadingBarrierToken ZombieToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("ZombieBarrier")), 1.0f, 5.0f);
    FLoadingBarrierToken SecondZombie = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("SecondZombie")), 1.0f, 12.0f);
    TestEqual(TEXT("当前活跃屏障为 2"), LoadingSubsystem->GetActiveBarrierCount(), 2);

    TestWorld.BroadcastPostLoadMap();
    TestEqual(TEXT("地图完成后进入 WaitingBarriers 阶段"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::WaitingBarriers);

    AddExpectedError(TEXT("【加载看门狗】"), EAutomationExpectedErrorFlags::Contains, 1);
    LoadingSubsystem->CheckWatchdogTimeoutAt(FPlatformTime::Seconds() + 8.0);

    TestTrue(TEXT("仅首屏障超时后仍处于加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("僵尸屏障被精准熔断后剩余 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    TestFalse(TEXT("被看门狗熔断的令牌 IsValid 立即反映失效"), ZombieToken.IsValid());
    TestFalse(TEXT("被看门狗熔断的令牌布尔转换立即为 false"), static_cast<bool>(ZombieToken));
    ZombieToken.ReportProgress(0.9f);
    TestTrue(TEXT("被熔断令牌继续 ReportProgress 不再篡改本地进度"), FMath::IsNearlyEqual(ZombieToken.GetProgress(), 0.0f, 0.001f));
    TestTrue(TEXT("未超时的第二个令牌仍有效"), SecondZombie.IsValid());

    AddExpectedError(TEXT("【加载看门狗】"), EAutomationExpectedErrorFlags::Contains, 1);
    LoadingSubsystem->CheckWatchdogTimeoutAt(FPlatformTime::Seconds() + 15.0);
    TestFalse(TEXT("最后一个屏障单屏障超时后直接完成加载并揭开幕布"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("最终活跃屏障归零"), LoadingSubsystem->GetActiveBarrierCount(), 0);
    TestFalse(TEXT("非全局超时结案时 Report.bTimedOut 为 false"), LoadingSubsystem->GetLastTelemetryReport().bTimedOut);
    TestEqual(TEXT("遥测包含 2 条超时屏障记录"), LoadingSubsystem->GetLastTelemetryReport().Barriers.Num(), 2);
    TestTrue(TEXT("首屏障遥测标记 bTimedOut"), LoadingSubsystem->GetLastTelemetryReport().Barriers[0].bTimedOut);
    TestTrue(TEXT("次屏障遥测标记 bTimedOut"), LoadingSubsystem->GetLastTelemetryReport().Barriers[1].bTimedOut);

    LoadingSubsystem->SetMaxLoadingTimeoutSeconds(10.0f);
    TestEqual(TEXT("动态配置全局超时为 10 秒"), LoadingSubsystem->GetMaxLoadingTimeoutSeconds(), 10.0f);
    TestTrue(TEXT("进入全局超时测试关卡"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/GlobalWatchdogLevel")));

    FLoadingBarrierToken StuckA = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("StuckA")), 1.0f, 0.0f);
    FLoadingBarrierToken StuckB = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("StuckB")), 2.0f, 0.0f);

    AddExpectedError(TEXT("【加载看门狗】"), EAutomationExpectedErrorFlags::Contains, 1);
    LoadingSubsystem->CheckWatchdogTimeoutAt(FPlatformTime::Seconds() + 15.0);

    TestFalse(TEXT("全局看门狗触发后立即强制退出加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("全局看门狗强制清空所有活跃屏障"), LoadingSubsystem->GetActiveBarrierCount(), 0);
    TestFalse(TEXT("全局熔断后原令牌 StuckA 立即失效"), StuckA.IsValid());
    TestFalse(TEXT("全局熔断后原令牌 StuckB 立即失效"), StuckB.IsValid());

    const FLoadingTelemetryReport& GlobalReport = LoadingSubsystem->GetLastTelemetryReport();
    TestTrue(TEXT("全局超时遥测报告明确标记 bTimedOut=true"), GlobalReport.bTimedOut);
    TestTrue(TEXT("地图阶段全局超时 MapLoadDurationMs 等于总耗时且大于 0"), GlobalReport.MapLoadDurationMs > 0.0f && FMath::IsNearlyEqual(GlobalReport.MapLoadDurationMs, GlobalReport.TotalDurationMs, 0.1f));
    TestEqual(TEXT("全局超时遥测完整记录 2 个被熔断屏障"), GlobalReport.Barriers.Num(), 2);
    TestTrue(TEXT("StuckA 遥测标记超时"), GlobalReport.Barriers[0].bTimedOut);
    TestTrue(TEXT("StuckB 遥测标记超时"), GlobalReport.Barriers[1].bTimedOut);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingReentrancyAndForcedTravelTest,
    "Game.Flow.System.LoadingReentrancyAndForcedTravel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证收尾广播重入链式 OpenLevelWithLoading 严格顺序拉平、LoadingMap 幂等保名及 WaitingBarriers 强切新图重置
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingReentrancyAndForcedTravelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingReentrancyTestWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    TArray<FString> EventLog;
    bool bTriggeredChainedLoad = false;

    LoadingSubsystem->OnLoadingProgressChangedNative.AddLambda(
        [&EventLog](float Progress, const FText&) {
            if (FMath::IsNearlyEqual(Progress, 1.0f, 0.0001f)) {
                EventLog.Add(TEXT("Progress:1.0"));
            }
        }
    );

    LoadingSubsystem->OnLoadingStateChangedNative.AddLambda(
        [LoadingSubsystem, &EventLog, &bTriggeredChainedLoad](bool bIsLoading, FName LevelName) {
            EventLog.Add(FString::Printf(TEXT("State:%d:%s"), bIsLoading ? 1 : 0, *LevelName.ToString()));
            if (!bIsLoading && !bTriggeredChainedLoad && LevelName == FName(TEXT("/Game/Maps/ChainA"))) {
                bTriggeredChainedLoad = true;
                LoadingSubsystem->OpenLevelWithLoading(FName(TEXT("/Game/Maps/ChainB")));
                LoadingSubsystem->OpenLevelWithLoading(FName(TEXT("/Game/Maps/ChainIgnored")));
            }
        }
    );

    LoadingSubsystem->OnLoadingTelemetryReportNative.AddLambda(
        [&EventLog](const FLoadingTelemetryReport& Report) {
            EventLog.Add(FString::Printf(TEXT("Telemetry:%s"), *Report.LevelName.ToString()));
        }
    );

    TestTrue(TEXT("进入 ChainA 加载"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/ChainA")));
    TestTrue(TEXT("LoadingMap 阶段重复收到 PreLoadMapWithContext 幂等保持原目标名"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/ChainA_LongPath")));
    TestEqual(TEXT("LoadingMap 阶段不篡改原 TargetLevelName"), LoadingSubsystem->GetTargetLevelName(), FName(TEXT("/Game/Maps/ChainA")));

    TestWorld.BroadcastPostLoadMap();
    TestTrue(TEXT("ChainA 收尾后延迟拉平启动 ChainB 加载"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("当前目标关卡为 ChainB（第二次排队被忽略）"), LoadingSubsystem->GetTargetLevelName(), FName(TEXT("/Game/Maps/ChainB")));

    TestEqual(TEXT("事件序列长度精确为 5"), EventLog.Num(), 5);
    if (EventLog.Num() == 5) {
        TestEqual(TEXT("第 0 步：ChainA 开始"), EventLog[0], TEXT("State:1:/Game/Maps/ChainA"));
        TestEqual(TEXT("第 1 步：ChainA 进度 1.0"), EventLog[1], TEXT("Progress:1.0"));
        TestEqual(TEXT("第 2 步：ChainA 结束"), EventLog[2], TEXT("State:0:/Game/Maps/ChainA"));
        TestEqual(TEXT("第 3 步：ChainA 遥测先于 ChainB 开始发出"), EventLog[3], TEXT("Telemetry:/Game/Maps/ChainA"));
        TestEqual(TEXT("第 4 步：ChainB 严格在 ChainA 全部收尾后才开始"), EventLog[4], TEXT("State:1:/Game/Maps/ChainB"));
    }

    FLoadingBarrierToken PendingBarrier = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("OldMapBarrier")));
    TestWorld.BroadcastPostLoadMap();
    TestEqual(TEXT("ChainB 进入 WaitingBarriers 阶段"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::WaitingBarriers);

    TestTrue(TEXT("WaitingBarriers 阶段服务端强切至 ForcedMapC"), TestWorld.BroadcastPreLoadMap(TEXT("/Game/Maps/ForcedMapC")));
    TestEqual(TEXT("强切新图后阶段重置为 LoadingMap"), LoadingSubsystem->GetLoadingPhase(), ELoadingTransitionPhase::LoadingMap);
    TestEqual(TEXT("强切新图后目标关卡更新为 ForcedMapC"), LoadingSubsystem->GetTargetLevelName(), FName(TEXT("/Game/Maps/ForcedMapC")));
    TestTrue(TEXT("强切新图触发 State:1:/Game/Maps/ForcedMapC 广播"), EventLog.Last() == TEXT("State:1:/Game/Maps/ForcedMapC"));

    PendingBarrier.Release();
    TestWorld.BroadcastPostLoadMap();
    TestFalse(TEXT("ForcedMapC 加载完毕正常退出加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("最终遥测报告关卡为 ForcedMapC"), LoadingSubsystem->GetLastTelemetryReport().LevelName, FName(TEXT("/Game/Maps/ForcedMapC")));

    return true;
}

#endif
