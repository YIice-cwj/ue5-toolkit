/**
 * @file LoadingBarrierTokenTest.cpp
 * @brief FLoadingBarrierToken 异步就绪屏障 RAII 令牌与移动语义自动化测试
 */

#include "System/FLoadingBarrierToken.h"
#include "System/ULoadingSubsystem.h"
#include "Tests/LoadingTestReceiver.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace {

ULoadingSubsystem* CreateTestSubsystemForToken(UGameInstance*& OutGameInstance) {
    if (GEngine == nullptr) {
        return nullptr;
    }
    OutGameInstance = NewObject<UGameInstance>(GEngine);
    OutGameInstance->InitializeStandalone(FName(TEXT("LoadingBarrierTokenTestWorld")));
    return OutGameInstance->GetSubsystem<ULoadingSubsystem>();
}

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameLoadingBarrierTokenTest,
    "Game.Flow.System.LoadingBarrierToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLoadingBarrierToken 异步就绪屏障投票、同名多票独立计数、RAII 自动释放与提前释放契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBarrierTokenTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystemForToken(GameInstance);
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
    TestTrue(TEXT("令牌布尔有效"), static_cast<bool>(*EarlyToken));
    TestTrue(TEXT("令牌 IsValid 为真"), EarlyToken->IsValid());
    TestEqual(TEXT("屏障名称一致"), EarlyToken->GetBarrierName(), FName(TEXT("EarlyPreload")));
    TestTrue(TEXT("屏障 ID 大于 0"), EarlyToken->GetBarrierId() > 0ULL);

    EarlyToken->Release();
    EarlyToken->Release();
    TestTrue(TEXT("显式释放后令牌状态标记为已释放"), EarlyToken->IsReleased());
    TestFalse(TEXT("释放后令牌布尔转换为假"), static_cast<bool>(*EarlyToken));
    TestFalse(TEXT("释放后 IsValid 为假"), EarlyToken->IsValid());
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
    FGameLoadingBarrierTokenMoveTest,
    "Game.Flow.System.LoadingBarrierTokenMove",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 FLoadingBarrierToken 移动构造与移动赋值语义
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameLoadingBarrierTokenMoveTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UGameInstance* GameInstance = nullptr;
    ULoadingSubsystem* LoadingSubsystem = CreateTestSubsystemForToken(GameInstance);
    TestNotNull(TEXT("加载子系统有效"), LoadingSubsystem);
    if (!LoadingSubsystem || !GameInstance) {
        return false;
    }

    FLoadingBarrierToken DefaultToken;
    TestTrue(TEXT("默认构造令牌初始标记为已释放"), DefaultToken.IsReleased());
    TestFalse(TEXT("默认构造令牌 IsValid 为假"), DefaultToken.IsValid());
    TestFalse(TEXT("默认构造令牌布尔值为假"), static_cast<bool>(DefaultToken));
    TestEqual(TEXT("默认构造令牌 ID 为 0"), DefaultToken.GetBarrierId(), 0ULL);
    TestEqual(TEXT("默认构造令牌名称为 None"), DefaultToken.GetBarrierName(), NAME_None);

    {
        FLoadingBarrierToken StackToken = LoadingSubsystem->AcquireBarrierToken(FName(TEXT("StackDirect")));
        TestTrue(TEXT("栈上令牌有效"), StackToken.IsValid());
        TestEqual(TEXT("栈上令牌分配有效 ID"), LoadingSubsystem->GetActiveBarrierCount(), 1);
        DefaultToken = MoveTemp(StackToken);
        TestTrue(TEXT("源栈上令牌被移动后标记为已释放"), StackToken.IsReleased());
        TestTrue(TEXT("目标令牌接管有效状态"), DefaultToken.IsValid());
        TestEqual(TEXT("活跃屏障数维持为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    }
    TestTrue(TEXT("原作用域退出后接管者仍持有有效屏障"), DefaultToken.IsValid());
    DefaultToken.Release();
    TestEqual(TEXT("显式释放接管令牌后活跃屏障数为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    TSharedRef<FLoadingBarrierToken> SourceRef = LoadingSubsystem->AcquireLoadingBarrier(FName(TEXT("MoveSource")));
    TestEqual(TEXT("申请一个屏障"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    const uint64 OriginalId = SourceRef->GetBarrierId();

    FLoadingBarrierToken TokenA(MoveTemp(*SourceRef));
    TestTrue(TEXT("源对象移动后标记为已释放"), SourceRef->IsReleased());
    TestFalse(TEXT("源对象移动后布尔为假"), static_cast<bool>(*SourceRef));
    TestTrue(TEXT("目标对象移动后持有有效屏障"), TokenA.IsValid());
    TestEqual(TEXT("目标对象接管屏障 ID"), TokenA.GetBarrierId(), OriginalId);
    TestEqual(TEXT("移动不改变子系统活跃屏障计数"), LoadingSubsystem->GetActiveBarrierCount(), 1);

    {
        FLoadingBarrierToken TokenB(MoveTemp(TokenA));
        TestTrue(TEXT("TokenA 移动后标记为已释放"), TokenA.IsReleased());
        TestTrue(TEXT("TokenB 接管有效屏障"), TokenB.IsValid());
        TestEqual(TEXT("TokenB 继承屏障 ID"), TokenB.GetBarrierId(), OriginalId);
        TestEqual(TEXT("屏障总数依然为 1"), LoadingSubsystem->GetActiveBarrierCount(), 1);
    }
    TestEqual(TEXT("TokenB 离开作用域析构释放屏障，活跃数降为 0"), LoadingSubsystem->GetActiveBarrierCount(), 0);

    GameInstance->Shutdown();
    return true;
}

#endif
