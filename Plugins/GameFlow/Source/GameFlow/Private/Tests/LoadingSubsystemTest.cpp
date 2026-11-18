/**
 * @file LoadingSubsystemTest.cpp
 * @brief ULoadingSubsystem 状态流转、蓝图屏障接口与异步动作节点自动化测试
 */

#include "Async/AsyncAction_WaitForLoadingComplete.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/LoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

ULoadingSubsystem* CreateTestSubsystem(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("LoadingSubsystemTestWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemStateFlowTest,
    "Game.Flow.System.LoadingSubsystemStateFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证地图加载生命周期委托挂钩、状态流转、动态组播广播与跨 GameInstance 隔离
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemStateFlowTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (LoadingSubsystem == nullptr || GameInstance == nullptr) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("测试世界有效"), World);
    if (World == nullptr) {
        GameInstance->Shutdown();
        return false;
    }

    TestEqual(TEXT("从 GameInstance 获取子系统一致"), ULoadingSubsystem::Get(GameInstance), LoadingSubsystem);
    TestEqual(TEXT("从 World 获取子系统一致"), ULoadingSubsystem::Get(World), LoadingSubsystem);
    TestNull(TEXT("空上下文获取子系统返回 nullptr"), ULoadingSubsystem::Get(nullptr));

    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingStateChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    TestFalse(TEXT("初始状态未处于加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("初始目标关卡应为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);

    const FString TestLevelName = TEXT("/Game/Maps/UnitTestLevel_Alpha");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(TestLevelName);

    TestTrue(TEXT("PreLoadMap 触发后进入加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("目标关卡名称更新为当前加载地图"), LoadingSubsystem->GetTargetLevelName(), FName(*TestLevelName));
    TestEqual(TEXT("收到加载开始委托广播"), Receiver->LoadingChangeCount, 1);
    TestTrue(TEXT("广播中加载状态为 true"), Receiver->bLastIsLoading);
    TestEqual(TEXT("广播中目标关卡一致"), Receiver->LastTargetLevel, FName(*TestLevelName));

    LoadingSubsystem->OpenLevelWithLoading(FName(TEXT("/Game/Maps/UnitTestLevel_Beta")));
    TestEqual(TEXT("加载中重复请求不改写当前目标关卡"), LoadingSubsystem->GetTargetLevelName(), FName(*TestLevelName));
    TestEqual(TEXT("重复请求不产生额外广播"), Receiver->LoadingChangeCount, 1);

    UGameInstance* OtherGameInstance = nullptr;
    [[maybe_unused]] ULoadingSubsystem* OtherSubsystem = CreateTestSubsystem(OtherGameInstance);
    if (OtherGameInstance != nullptr && OtherGameInstance->GetWorld() != nullptr) {
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(OtherGameInstance->GetWorld());
        TestTrue(TEXT("异源世界加载完成不误清本子系统的加载态"), LoadingSubsystem->IsLoading());
        OtherGameInstance->Shutdown();
    }

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestFalse(TEXT("本实例世界加载完成后退出加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("加载完成后目标关卡复位为 None"), LoadingSubsystem->GetTargetLevelName(), NAME_None);
    TestEqual(TEXT("收到加载结束委托广播"), Receiver->LoadingChangeCount, 2);
    TestFalse(TEXT("广播中加载状态为 false"), Receiver->bLastIsLoading);
    TestEqual(TEXT("结束广播中附带完成的关卡名称"), Receiver->LastTargetLevel, FName(*TestLevelName));

    LoadingSubsystem->OpenLevelWithLoading(NAME_None);
    TestFalse(TEXT("空关卡名不进入加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("空关卡名不产生广播"), Receiver->LoadingChangeCount, 2);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingBlueprintActionAndBpBarrierTest,
    "Game.Flow.System.LoadingBlueprintActionAndBpBarrier",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证蓝图屏障申请/释放接口与 UAsyncAction_WaitForLoadingComplete 动作节点
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBlueprintActionAndBpBarrierTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (!LoadingSubsystem || !GameInstance) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    const int64 BarrierId = LoadingSubsystem->AcquireLoadingBarrierBp(FName(TEXT("BpBarrier1")));
    TestTrue(TEXT("蓝图屏障句柄有效且大于0"), BarrierId > 0);
    TestEqual(TEXT("活跃屏障数为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);

    TestFalse(TEXT("释放不存在的负 ID 屏障返回 false"), LoadingSubsystem->ReleaseLoadingBarrierBp(-1));
    TestFalse(TEXT("释放不存在的超大 ID 屏障返回 false"), LoadingSubsystem->ReleaseLoadingBarrierBp(99999));
    TestTrue(TEXT("释放有效蓝图屏障返回 true"), LoadingSubsystem->ReleaseLoadingBarrierBp(BarrierId));
    TestEqual(TEXT("释放后活跃屏障数为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    bool bActionDirectCompleted = false;
    UAsyncAction_WaitForLoadingComplete* DirectAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    TestNotNull(TEXT("异步等待动作节点创建有效"), DirectAction);
    if (DirectAction) {
        DirectAction->OnCompletedNative.AddLambda([&bActionDirectCompleted]() {
            bActionDirectCompleted = true;
        });
        DirectAction->Activate();
        TestTrue(TEXT("非加载态下异步等待动作直接同步完成"), bActionDirectCompleted);
    }

    const FString TestLevel = TEXT("/Game/Maps/AsyncActionLevel");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(TestLevel);
    const int64 HoldBarrier = LoadingSubsystem->AcquireLoadingBarrierBp(FName(TEXT("HoldingForAction")));

    bool bAsyncActionCompleted = false;
    UAsyncAction_WaitForLoadingComplete* WaitingAction = UAsyncAction_WaitForLoadingComplete::WaitForLoadingComplete(World);
    if (WaitingAction) {
        WaitingAction->OnCompletedNative.AddLambda([&bAsyncActionCompleted]() {
            bAsyncActionCompleted = true;
        });
        WaitingAction->Activate();
    }

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestTrue(TEXT("屏障未释放前仍然处于加载中"), LoadingSubsystem->IsLoading());
    TestFalse(TEXT("屏障未释放前异步动作未触发完成"), bAsyncActionCompleted);

    LoadingSubsystem->ReleaseLoadingBarrierBp(HoldBarrier);
    TestFalse(TEXT("屏障释放后加载完成"), LoadingSubsystem->IsLoading());
    TestTrue(TEXT("屏障释放后异步动作触发完成回调"), bAsyncActionCompleted);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemProgressAndTelemetryTest,
    "Game.Flow.System.LoadingProgressAndTelemetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证加权加载进度换算、步骤说明广播与单次关卡遥测报告生成
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemProgressAndTelemetryTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (!LoadingSubsystem || !GameInstance) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    float LastProgress = -1.0f;
    FString LastDescription;
    LoadingSubsystem->OnLoadingProgressChangedNative.AddLambda([&LastProgress, &LastDescription](float Progress, const FText& Desc) {
        LastProgress = Progress;
        LastDescription = Desc.ToString();
    });

    FLoadingTelemetryReport CapturedReport;
    bool bReportReceived = false;
    LoadingSubsystem->OnLoadingTelemetryReportNative.AddLambda([&CapturedReport, &bReportReceived](const FLoadingTelemetryReport& Report) {
        CapturedReport = Report;
        bReportReceived = true;
    });

    const FString TestLevel = TEXT("/Game/Maps/ProgressLevel");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(TestLevel);

    TestTrue(TEXT("加载开始进度为 0.0f"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 0.0f, 0.01f));

    FLoadingBarrierToken TokenA = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("BarrierAlpha")), 2.0f);
    FLoadingBarrierToken TokenB = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("BarrierBeta")), 2.0f);

    TokenA.ReportProgress(0.5f, FText::FromString(TEXT("预热网格体")));
    TestTrue(TEXT("单屏障进度上报推动全局进度"), LoadingSubsystem->GetLoadingProgress() > 0.0f);
    TestEqual(TEXT("步骤说明正确广播"), LastDescription, TEXT("预热网格体"));

    const float PreReleaseProgress = LoadingSubsystem->GetLoadingProgress();
    TokenA.Release();
    TestTrue(TEXT("高权重屏障释放后进度严格单调不减（权重守恒）"), LoadingSubsystem->GetLoadingProgress() >= PreReleaseProgress);

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestTrue(TEXT("地图加载完成后进度进一步推进"), LoadingSubsystem->GetLoadingProgress() >= 0.5f);

    TokenB.Release();
    TestFalse(TEXT("全部屏障释放后加载完成"), LoadingSubsystem->IsLoading());
    TestTrue(TEXT("全部屏障释放后全局进度达到 1.0f"), FMath::IsNearlyEqual(LoadingSubsystem->GetLoadingProgress(), 1.0f, 0.01f));
    TestTrue(TEXT("成功派发遥测报告"), bReportReceived);
    TestEqual(TEXT("遥测报告关卡名正确"), CapturedReport.LevelName, FName(*TestLevel));
    TestEqual(TEXT("遥测报告包含 2 个屏障记录"), CapturedReport.Barriers.Num(), 2);
    TestEqual(TEXT("遥测报告中屏障记录保留了申请权重"), CapturedReport.Barriers[0].Weight, 2.0f);
    TestTrue(TEXT("遥测总耗时大于等于 0"), CapturedReport.TotalDurationMs >= 0.0f);

    GameInstance->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemWatchdogTimeoutTest,
    "Game.Flow.System.LoadingWatchdogTimeout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证加载看门狗超时检测机制：在外部屏障死锁未释放时强制熔断并揭开幕布
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemWatchdogTimeoutTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystem(GameInstance);
    TestNotNull(TEXT("加载子系统创建成功"), LoadingSubsystem);
    if (!LoadingSubsystem || !GameInstance) {
        return false;
    }

    UWorld* World = GameInstance->GetWorld();
    TestNotNull(TEXT("世界有效"), World);
    if (!World) {
        GameInstance->Shutdown();
        return false;
    }

    LoadingSubsystem->SetMaxLoadingTimeoutSeconds(300.0f);

    const FString TestLevel = TEXT("/Game/Maps/WatchdogLevel");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(TestLevel);
    TestTrue(TEXT("进入加载状态"), LoadingSubsystem->IsLoading());

    FLoadingBarrierToken ZombieToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("ZombieBarrier")), 1.0f, 5.0f);
    FLoadingBarrierToken SafeToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("SafeBarrier")), 1.0f, 0.0f);
    TestEqual(TEXT("当前活跃屏障为 2"), LoadingSubsystem->GetActiveBarrierCount(), 2);

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestTrue(TEXT("地图加载完成但有2个屏障因此保持加载态"), LoadingSubsystem->IsLoading());

    AddExpectedError(TEXT("【加载看门狗】"), EAutomationExpectedErrorFlags::Contains, 1);
    const double PartialTimeoutTime = FPlatformTime::Seconds() + 10.0;
    LoadingSubsystem->CheckWatchdogTimeout(PartialTimeoutTime);

    TestTrue(TEXT("单屏障超时后因仍有安全屏障故依然处于加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("僵尸屏障被精准熔断活跃屏障剩余 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);

    SafeToken.Release();
    TestFalse(TEXT("全部屏障清理后正常退出加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("最终活跃屏障归零"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    GameInstance->Shutdown();
    return true;
}

#endif
