/**
 * @file LoadingBarrierTokenTest.cpp
 * @brief FLoadingBarrierToken 零堆分配 RAII 令牌、代际句柄与移动语义自动化测试
 */

#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/GameFlowTestUtils.h"
#include "Tests/ULoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

using Game::Flow::Test::FScopedTestLoadingWorld;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingBarrierTokenTest,
    "Game.Flow.System.LoadingBarrierToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLoadingBarrierToken 异步就绪屏障投票、同名多票独立代际句柄、进度净化、RAII 自动释放与跨销毁安全
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBarrierTokenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingBarrierTokenTestWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();
    UGameInstance* GameInstance = TestWorld.GetGameInstance();

    ULoadingTestReceiver* Receiver = NewObject<ULoadingTestReceiver>(GameInstance);
    LoadingSubsystem->OnLoadingStateChanged.AddDynamic(Receiver, &ULoadingTestReceiver::OnLoadingStateChanged);

    TestFalse(TEXT("初始无活跃屏障"), LoadingSubsystem->HasActiveBarriers());
    TestEqual(TEXT("初始活跃屏障数为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    const FString LevelOne = TEXT("/Game/Maps/BarrierLevel_One");
    TestTrue(TEXT("进入加载态"), TestWorld.BroadcastPreLoadMap(LevelOne));
    TestEqual(TEXT("进入加载开始广播计数为 1"), Receiver->LoadingChangeCount, 1);

    FLoadingBarrierToken EarlyToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("EarlyPreload")));
    TestTrue(TEXT("申请屏障后存在活跃屏障"), LoadingSubsystem->HasActiveBarriers());
    TestTrue(TEXT("令牌布尔有效"), static_cast<bool>(EarlyToken));
    TestTrue(TEXT("令牌 IsValid 为真"), EarlyToken.IsValid());
    TestFalse(TEXT("令牌尚未释放"), EarlyToken.IsReleased());
    TestEqual(TEXT("屏障名称一致"), EarlyToken.GetBarrierName(), FName(TEXT("EarlyPreload")));
    TestTrue(TEXT("代际句柄有效"), EarlyToken.GetBarrierHandle().IsValid());

    EarlyToken.ReportProgress(0.4f);
    TestTrue(TEXT("合法进度更新 GetProgress 返回 0.4f"), FMath::IsNearlyEqual(EarlyToken.GetProgress(), 0.4f, 0.001f));
    EarlyToken.ReportProgress(std::numeric_limits<float>::quiet_NaN());
    TestTrue(TEXT("NaN 进度被忽略保持 0.4f"), FMath::IsNearlyEqual(EarlyToken.GetProgress(), 0.4f, 0.001f));
    EarlyToken.ReportProgress(2.5f);
    TestTrue(TEXT("越界进度被截断为 1.0f"), FMath::IsNearlyEqual(EarlyToken.GetProgress(), 1.0f, 0.001f));

    EarlyToken.Release();
    EarlyToken.Release();
    TestTrue(TEXT("显式释放后令牌状态标记为已释放"), EarlyToken.IsReleased());
    TestFalse(TEXT("释放后代际句柄已重置"), EarlyToken.GetBarrierHandle().IsValid());
    TestFalse(TEXT("释放后令牌布尔转换为假"), static_cast<bool>(EarlyToken));
    TestFalse(TEXT("释放后 IsValid 为假"), EarlyToken.IsValid());
    EarlyToken.ReportProgress(0.1f);
    TestTrue(TEXT("已释放令牌 ReportProgress 静默忽略"), FMath::IsNearlyEqual(EarlyToken.GetProgress(), 1.0f, 0.001f));
    TestTrue(TEXT("PostLoadMap 尚未到达前释放屏障不会提前关闭加载幕布"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("未提前触发结束广播"), Receiver->LoadingChangeCount, 1);

    FLoadingBarrierToken DupTokenA = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("PoolWarmup")));
    {
        FLoadingBarrierToken DupTokenB = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("PoolWarmup")));
        TestNotEqual(TEXT("同名屏障分配独立代际句柄"), DupTokenA.GetBarrierHandle(), DupTokenB.GetBarrierHandle());
        TestEqual(TEXT("同名双屏障独立计数为 2"), LoadingSubsystem->GetActiveBarrierCount(), 2);

        TestWorld.BroadcastPostLoadMap();
        TestTrue(TEXT("PostLoadMap 已完成但屏障未清空时保持加载幕布开启"), LoadingSubsystem->IsLoading());
        TestEqual(TEXT("屏障挂起期间不广播加载完成"), Receiver->LoadingChangeCount, 1);
    }

    TestEqual(TEXT("DupTokenB 离开作用域析构后剩余 1 个活跃屏障"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    TestTrue(TEXT("尚余 1 票屏障时仍保持加载态"), LoadingSubsystem->IsLoading());
    TestEqual(TEXT("尚余 1 票屏障时不广播完成"), Receiver->LoadingChangeCount, 1);

    DupTokenA.Release();
    TestFalse(TEXT("最后一票屏障释放后立即退出加载态"), LoadingSubsystem->IsLoading());
    TestFalse(TEXT("全部屏障清空"), LoadingSubsystem->HasActiveBarriers());
    TestEqual(TEXT("最后一票释放触发加载结束广播"), Receiver->LoadingChangeCount, 2);
    TestFalse(TEXT("结束广播标志为 false"), Receiver->bLastIsLoading);
    TestEqual(TEXT("结束广播携带原目标地图名"), Receiver->LastTargetLevel, FName(*LevelOne));

    FLoadingBarrierToken SurvivingToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("SurvivingAfterShutdown")));
    TestWorld.ShutdownNow();
    TestFalse(TEXT("子系统销毁后残留令牌 IsValid 安全返回 false"), SurvivingToken.IsValid());
    SurvivingToken.Release();
    TestTrue(TEXT("子系统销毁后残留令牌释放安全无副作用"), SurvivingToken.IsReleased());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingBarrierTokenMoveTest,
    "Game.Flow.System.LoadingBarrierTokenMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLoadingBarrierToken 默认态、移动构造、覆盖活跃令牌移动赋值与自移动赋值安全性
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBarrierTokenMoveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FScopedTestLoadingWorld TestWorld(FName(TEXT("LoadingBarrierTokenMoveWorld")));
    TestTrue(TEXT("测试世界初始化成功"), TestWorld.IsValid());
    if (!TestWorld.IsValid()) {
        return false;
    }

    ULoadingSubsystem* LoadingSubsystem = TestWorld.GetSubsystem();

    FLoadingBarrierToken DefaultToken;
    TestTrue(TEXT("默认构造令牌初始标记为已释放"), DefaultToken.IsReleased());
    TestFalse(TEXT("默认构造令牌 IsValid 为假"), DefaultToken.IsValid());
    TestFalse(TEXT("默认构造令牌布尔值为假"), static_cast<bool>(DefaultToken));
    TestFalse(TEXT("默认构造令牌代际句柄无效"), DefaultToken.GetBarrierHandle().IsValid());
    TestEqual(TEXT("默认构造令牌名称为 None"), DefaultToken.GetBarrierName(), NAME_None);

    {
        FLoadingBarrierToken StackToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("StackDirect")));
        StackToken.ReportProgress(0.6f);
        const FLoadingBarrierHandle OriginalHandle = StackToken.GetBarrierHandle();
        TestTrue(TEXT("栈上令牌有效"), StackToken.IsValid());
        TestEqual(TEXT("活跃屏障数为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);

        DefaultToken = MoveTemp(StackToken);
        TestTrue(TEXT("源栈上令牌被移动后标记为已释放"), StackToken.IsReleased());
        TestFalse(TEXT("源栈上令牌代际句柄已清空"), StackToken.GetBarrierHandle().IsValid());
        TestTrue(TEXT("目标令牌接管有效状态"), DefaultToken.IsValid());
        TestEqual(TEXT("目标令牌继承代际句柄"), DefaultToken.GetBarrierHandle(), OriginalHandle);
        TestTrue(TEXT("目标令牌继承内部进度"), FMath::IsNearlyEqual(DefaultToken.GetProgress(), 0.6f, 0.001f));
        TestEqual(TEXT("活跃屏障数维持为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    }
    TestTrue(TEXT("原作用域退出后接管者仍持有有效屏障"), DefaultToken.IsValid());

    FLoadingBarrierToken ReplacementToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("Replacement")));
    const FLoadingBarrierHandle ReplacementHandle = ReplacementToken.GetBarrierHandle();
    TestEqual(TEXT("并发持有两个屏障时活跃数为 2"), LoadingSubsystem->GetActiveBarrierCount(), 2);

    DefaultToken = MoveTemp(ReplacementToken);
    TestEqual(TEXT("覆盖移动赋值自动释放旧屏障使活跃数降为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    TestEqual(TEXT("接管新屏障句柄"), DefaultToken.GetBarrierHandle(), ReplacementHandle);
    TestTrue(TEXT("新句柄有效"), DefaultToken.IsValid());

    FLoadingBarrierToken* SelfAlias = &DefaultToken;
    DefaultToken = MoveTemp(*SelfAlias);
    TestTrue(TEXT("自移动赋值保持屏障存活无自毁"), DefaultToken.IsValid());
    TestEqual(TEXT("自移动赋值不改变活跃屏障计数"), LoadingSubsystem->GetActiveBarrierCount(), 1);

    {
        FLoadingBarrierToken TokenB(MoveTemp(DefaultToken));
        TestTrue(TEXT("源对象移动构造后标记为已释放"), DefaultToken.IsReleased());
        TestTrue(TEXT("TokenB 接管有效屏障"), TokenB.IsValid());
        TestEqual(TEXT("TokenB 继承屏障句柄"), TokenB.GetBarrierHandle(), ReplacementHandle);
        TestEqual(TEXT("屏障总数依然为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    }
    TestEqual(TEXT("TokenB 离开作用域析构释放屏障，活跃数降为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    return true;
}

#endif
