/**
 * @file UThrottledTickerSubsystemTest.cpp
 * @brief UThrottledTickerSubsystem 分级节流心跳子系统单元测试
 */

#include "Tasks/UThrottledTickerSubsystem.h"

#if WITH_AUTOMATION_TESTS

#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThrottledTickerSubsystemLifecycleAndRatesTest,
    "Game.Core.Tasks.ThrottledTicker.LifecycleAndRates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证分级节流心跳子系统的注册、每帧推进、间隔累加、动态调频与注销解挂
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FThrottledTickerSubsystemLifecycleAndRatesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TestNull(TEXT("空上下文 GetPtr 返回 nullptr"), UThrottledTickerSubsystem::GetPtr(nullptr));

    FThrottledTickHandle EmptyHandle;
    TestFalse(TEXT("默认空句柄非活跃"), EmptyHandle.IsActive());
    TestEqual(TEXT("默认空句柄 ID 为 0"), EmptyHandle.GetHandleId(), 0ULL);
    EmptyHandle.Unregister();

    UThrottledTickerSubsystem* Subsystem = NewObject<UThrottledTickerSubsystem>();
    TestFalse(TEXT("初始状态无注册项时 IsTickable 为假"), Subsystem->IsTickable());
    TestEqual(TEXT("初始注册数量为 0"), Subsystem->GetRegisteredCount(), 0);

    int32 EveryFrameCount = 0;
    float LastEveryFrameDelta = 0.0f;
    FThrottledTickHandle FrameHandle = Subsystem->RegisterTick(
        nullptr,
        EThrottledRate::EveryFrame,
        [&EveryFrameCount, &LastEveryFrameDelta](float DeltaTime) {
            ++EveryFrameCount;
            LastEveryFrameDelta = DeltaTime;
        }
    );

    TestTrue(TEXT("注册后句柄有效且处于活跃态"), FrameHandle.IsActive());
    TestTrue(TEXT("存在注册项时 IsTickable 为真"), Subsystem->IsTickable());
    TestEqual(TEXT("注册项计数为 1"), Subsystem->GetRegisteredCount(), 1);

    Subsystem->Tick(0.016f);
    TestEqual(TEXT("每帧心跳执行 1 次"), EveryFrameCount, 1);
    TestEqual(TEXT("每帧心跳传递正确 DeltaTime"), LastEveryFrameDelta, 0.016f);

    int32 ThrottledCount = 0;
    float LastThrottledDelta = 0.0f;
    FThrottledTickHandle ThrottledHandle = Subsystem->RegisterIntervalTick(
        nullptr,
        0.1f,
        [&ThrottledCount, &LastThrottledDelta](float DeltaTime) {
            ++ThrottledCount;
            LastThrottledDelta = DeltaTime;
        }
    );
    ThrottledHandle.SetInterval(0.1f);

    Subsystem->Tick(0.03f);
    TestEqual(TEXT("累积 0.03s 未达 0.1s 阈值不触发"), ThrottledCount, 0);

    Subsystem->Tick(0.04f);
    TestEqual(TEXT("累积 0.07s 未达 0.1s 阈值不触发"), ThrottledCount, 0);

    Subsystem->Tick(0.05f);
    TestEqual(TEXT("累积 0.12s 超过 0.1s 阈值触发 1 次"), ThrottledCount, 1);
    TestEqual(TEXT("触发时传递累计 DeltaTime"), LastThrottledDelta, 0.12f);

    ThrottledHandle.SetInterval(0.05f);
    Subsystem->Tick(0.06f);
    TestEqual(TEXT("动态调频缩短间隔后下一次累积超阈值触发"), ThrottledCount, 2);

    FrameHandle.Unregister();
    TestFalse(TEXT("解挂后句柄处于非活跃态"), FrameHandle.IsActive());
    TestEqual(TEXT("解挂后句柄 ID 归零"), FrameHandle.GetHandleId(), 0ULL);

    const int32 SavedFrameCount = EveryFrameCount;
    Subsystem->Tick(0.016f);
    TestEqual(TEXT("已解挂项后续 Tick 绝不再递增"), EveryFrameCount, SavedFrameCount);

    Subsystem->ClearAll();
    TestEqual(TEXT("ClearAll 后注册数量归零"), Subsystem->GetRegisteredCount(), 0);
    TestFalse(TEXT("清空后 IsTickable 为假"), Subsystem->IsTickable());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FThrottledTickerSubsystemOwnerGuardTest,
    "Game.Core.Tasks.ThrottledTicker.OwnerGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证宿主弱引用守卫在宿主销毁时自动短路并不再触发回调
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FThrottledTickerSubsystemOwnerGuardTest::RunTest([[maybe_unused]] const FString& Parameters) {
    UThrottledTickerSubsystem* Subsystem = NewObject<UThrottledTickerSubsystem>();
    UGameInstance* Owner = NewObject<UGameInstance>();

    int32 TickCount = 0;
    FThrottledTickHandle Handle = Subsystem->RegisterTick(
        Owner,
        EThrottledRate::EveryFrame,
        [&TickCount](float) {
            ++TickCount;
        }
    );

    TestTrue(TEXT("持有存活宿主时处于活跃态"), Handle.IsActive());
    Subsystem->Tick(0.016f);
    TestEqual(TEXT("宿主有效时正常 Tick 1 次"), TickCount, 1);

    Owner->MarkAsGarbage();
    TestFalse(TEXT("宿主被标记回收后句柄 IsActive 为假"), Handle.IsActive());

    Subsystem->Tick(0.016f);
    TestEqual(TEXT("宿主失效后回调被安全拦截绝不递增"), TickCount, 1);
    TestEqual(TEXT("失效宿主在 Tick 后被自动清理剔除"), Subsystem->GetRegisteredCount(), 0);

    return true;
}

#endif
