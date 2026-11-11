/**
 * @file ULoadingSubsystemTest.cpp
 * @brief ULoadingSubsystem 与 FLoadingBarrierToken 自动化测试（状态流转、异步就绪屏障投票、RAII 释放与跨实例隔离）
 */

#include "CoreMinimal.h"
#include "Async/FGameCoroAwaiters.h"
#include "Async/TGameCoroTask.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/LoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

namespace {

/**
 * @brief 验证非加载态立即返回的测试协程
 * @param ContextObject 世界上下文对象
 * @return 布尔结果协程
 */
TGameCoroTask<bool> RunImmediateLoadingCoro(const UObject* ContextObject) {
    const bool bResult = co_await GameFlow::WaitLoadingComplete(ContextObject);
    co_return bResult;
}

/**
 * @brief 验证等待加载屏障解除的测试协程
 * @param ContextObject 世界上下文对象
 * @param OutFinished 是否执行完成标志引用
 * @return 异步协程
 */
TGameCoroTask<bool> RunWaitLoadingCompleteCoro(const UObject* ContextObject, bool& OutFinished) {
    const bool bOk = co_await GameFlow::WaitLoadingComplete(ContextObject);
    if (bOk) {
        OutFinished = true;
    }
    co_return bOk;
}

/**
 * @brief 验证生命周期守卫熔断的测试协程
 * @param ContextObject 世界上下文对象
 * @param HostObject 绑定宿主对象
 * @param OutPostAwaitExecuted 挂起点后执行标志引用
 * @return 异步协程
 */
TGameCoroTask<void> RunGuardedWaitLoadingCoro(
    const UObject* ContextObject,
    UObject* HostObject,
    bool& OutPostAwaitExecuted
) {
    co_await Game::Coro::BindContext(HostObject);
    co_await GameFlow::WaitLoadingComplete(ContextObject);
    OutPostAwaitExecuted = true;
}

/**
 * @brief 构造独立测试用的 GameInstance 与关卡流转子系统
 * @param OutGameInstance 输出创建的 GameInstance 实例引用
 * @return 初始化的 ULoadingSubsystem 指针
 */
ULoadingSubsystem* CreateTestSubsystem(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("LoadingSubsystemTestWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemStateFlowTest,
    "Game.Flow.System.LoadingSubsystemStateFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证地图加载生命周期委托挂钩、状态流转、动态组播广播与跨 GameInstance 隔离
 * @param Parameters 自动化框架参数（未使用）
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
    FGameLoadingBarrierTokenTest,
    "Game.Flow.System.LoadingBarrierToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLoadingBarrierToken 异步就绪屏障投票、同名多票独立计数、RAII 自动释放与提前释放契约
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBarrierTokenTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingStateChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    TestFalse(TEXT("初始无活跃屏障"), LoadingSubsystem->HasActiveBarriers());
    TestEqual(TEXT("初始活跃屏障数为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    const FString LevelOne = TEXT("/Game/Maps/BarrierLevel_One");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(LevelOne);
    TestEqual(TEXT("进入加载开始广播计数为 1"), Receiver->LoadingChangeCount, 1);

    TSharedPtr<FLoadingBarrierToken> EarlyToken = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("EarlyPreload")));
    TestTrue(TEXT("申请屏障后存在活跃屏障"), LoadingSubsystem->HasActiveBarriers());
    TestEqual(TEXT("屏障名称一致"), EarlyToken->GetBarrierName(), FName(TEXT("EarlyPreload")));
    TestTrue(TEXT("屏障 ID 大于 0"), EarlyToken->GetBarrierId() > 0ULL);

    EarlyToken->Release();
    EarlyToken->Release();
    TestTrue(TEXT("显式释放后令牌状态标记为已释放"), EarlyToken->IsReleased());
    TestTrue(TEXT("PostLoadMap 尚未到达前释放屏障不会提前关闭加载幕布"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("未提前触发结束广播"), Receiver->LoadingChangeCount, 1);

    TSharedPtr<FLoadingBarrierToken> DupTokenA = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("PoolWarmup")));
    {
        TSharedRef<FLoadingBarrierToken> DupTokenB = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("PoolWarmup")));
        TestNotEqual(TEXT("同名屏障分配独立递增 ID"), DupTokenA->GetBarrierId(), DupTokenB->GetBarrierId());
        TestEqual(TEXT("同名双屏障独立计数为 2"), LoadingSubsystem->GetActiveBarrierCount(), 2);

        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
        TestTrue(TEXT("PostLoadMap 已完成但屏障未清空时保持加载幕布开启"), LoadingSubsystem->IsLoading());
        TestEqual(TEXT("屏障挂起期间不广播加载完成"), Receiver->LoadingChangeCount, 1);
    }

    TestEqual(TEXT("DupTokenB 离开作用域析构后剩余 1 个活跃屏障"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    TestTrue(TEXT("尚余 1 票屏障时仍保持加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("尚余 1 票屏障时不广播完成"), Receiver->LoadingChangeCount, 1);

    DupTokenA->Release();
    TestFalse(TEXT("最后一票屏障释放后立即退出加载态"), LoadingSubsystem->IsLoading());
    TestFalse(TEXT("全部屏障清空"), LoadingSubsystem->HasActiveBarriers());
    TestEqual(TEXT("最后一票释放触发加载结束广播"), Receiver->LoadingChangeCount, 2);
    TestFalse(TEXT("结束广播标志为 false"), Receiver->bLastIsLoading);
    TestEqual(TEXT("结束广播携带原目标地图名"), Receiver->LastTargetLevel, FName(*LevelOne));

    TSharedPtr<FLoadingBarrierToken> SurvivingToken = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("SurvivingAfterShutdown")));
    GameInstance->Shutdown();
    SurvivingToken.Reset();

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingSubsystemCoroAwaiterTest,
    "Game.Flow.System.LoadingSubsystemCoroAwaiter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 GameFlow::WaitLoadingComplete C++20 协程挂起等待原语、非加载态直通、屏障解除唤醒与宿主生命周期自毁熔断
 * @param Parameters 自动化框架参数（未使用）
 * @return 全部断言通过返回 true
 */
bool FGameLoadingSubsystemCoroAwaiterTest::RunTest([[maybe_unused]] const FString& Parameters) {
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

    TGameCoroTask<bool> ImmediateTask = RunImmediateLoadingCoro(World);
    TestTrue(TEXT("非加载态下 WaitLoadingComplete 直接同步完成"), ImmediateTask.IsDone());
    TestTrue(TEXT("非加载态下结果状态成功"), ImmediateTask.GetResult().IsSuccess());
    TestTrue(TEXT("非加载态下返回 true"), ImmediateTask.GetResult().GetValue());

    TGameCoroTask<bool> NullContextTask = RunImmediateLoadingCoro(nullptr);
    TestTrue(TEXT("空上下文直接同步完成"), NullContextTask.IsDone());
    TestTrue(TEXT("空上下文结果状态成功"), NullContextTask.GetResult().IsSuccess());
    TestFalse(TEXT("空上下文返回 false"), NullContextTask.GetResult().GetValue());

    const FString LevelName = TEXT("/Game/Maps/CoroLevel");
    FCoreUObjectDelegates::PreLoadMap.Broadcast(LevelName);
    TSharedPtr<FLoadingBarrierToken> Barrier = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("AsyncBarrier")));

    bool bCoroFinished = false;
    TGameCoroTask<bool> WaitTask = RunWaitLoadingCompleteCoro(World, bCoroFinished);
    TestFalse(TEXT("加载与屏障进行中协程处于挂起未完成态"), WaitTask.IsDone());
    TestFalse(TEXT("协程尚未执行结束赋值"), bCoroFinished);

    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    TestFalse(TEXT("屏障未释放前协程依然挂起"), WaitTask.IsDone());

    Barrier->Release();
    TestTrue(TEXT("屏障释放加载完成后协程立即唤醒完成"), WaitTask.IsDone());
    TestTrue(TEXT("协程执行状态正常"), WaitTask.GetResult().IsSuccess());
    TestTrue(TEXT("协程返回值结果为 true"), WaitTask.GetResult().GetValue());
    TestTrue(TEXT("协程成功执行挂起点后逻辑"), bCoroFinished);

    FCoreUObjectDelegates::PreLoadMap.Broadcast(LevelName);
    TSharedPtr<FLoadingBarrierToken> Barrier2 = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("Barrier2")));

    bool bGuardedPostAwaitRan = false;
    UGameInstance* TempHost = NewObject<UGameInstance>();
    TGameCoroTask<void> GuardedCoro = RunGuardedWaitLoadingCoro(World, TempHost, bGuardedPostAwaitRan);
    TestFalse(TEXT("守卫协程处于挂起态"), GuardedCoro.IsDone());

    TempHost->MarkAsGarbage();
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World);
    Barrier2->Release();

    TestFalse(TEXT("宿主销毁后挂起点后的代码绝不执行"), bGuardedPostAwaitRan);
    TestTrue(TEXT("宿主销毁后协程处于已取消终态"), GuardedCoro.IsCancelled());

    GameInstance->Shutdown();
    return true;
}

#endif  // WITH_AUTOMATION_TESTS
