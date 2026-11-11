/**
 * @file FGameStateMachineTest.cpp
 * @brief TGameStateMachine 强类型通用状态机单元测试
 */

#include "State/TGameStateMachine.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

enum class ETestActorState : uint8 {
    Idle,
    Patrol,
    Combat,
    Dead
};

} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStateMachineTransitionsAndGuardsTest,
    "Game.Core.State.StateMachine.TransitionsAndPriorityGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证状态机启停、进入退出时序、驻留计时、优先级守卫自动跃迁与同态忽略契约
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameStateMachineTransitionsAndGuardsTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TGameStateMachine<ETestActorState> Fsm;
    TestFalse(TEXT("未启动时 IsRunning 为假"), Fsm.IsRunning());
    TestFalse(TEXT("未启动时 ChangeState 返回 false"), Fsm.ChangeState(ETestActorState::Patrol));
    Fsm.Update(0.5f);
    Fsm.Stop();

    TArray<FString> EventLog;
    bool bEnemySpotted = false;
    bool bHealthZero = false;
    float CombatUpdatedSeconds = 0.0f;

    Fsm.RegisterState(ETestActorState::Idle, {
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Enter:Idle")); },
        nullptr,
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Exit:Idle")); }
    });
    Fsm.RegisterState(ETestActorState::Patrol, {
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Enter:Patrol")); },
        nullptr,
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Exit:Patrol")); }
    });
    Fsm.RegisterState(ETestActorState::Combat, {
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Enter:Combat")); },
        [&CombatUpdatedSeconds](float Dt) { CombatUpdatedSeconds += Dt; },
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Exit:Combat")); }
    });
    Fsm.RegisterState(ETestActorState::Dead, {
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Enter:Dead")); },
        nullptr,
        [&EventLog](ETestActorState) { EventLog.Add(TEXT("Exit:Dead")); }
    });

    Fsm.AddTransition(ETestActorState::Idle, ETestActorState::Patrol, TGameStateMachine<ETestActorState>::FTransitionGuard{});
    Fsm.AddTransition(ETestActorState::Idle, ETestActorState::Combat, [&bEnemySpotted]() { return bEnemySpotted; }, 10);
    Fsm.AddTransition(ETestActorState::Idle, ETestActorState::Dead, [&bHealthZero]() { return bHealthZero; }, 100);

    TArray<TPair<ETestActorState, ETestActorState>> ObservedChanges;
    Fsm.SetOnStateChanged([&ObservedChanges](ETestActorState Prev, ETestActorState Next) {
        ObservedChanges.Emplace(Prev, Next);
    });

    Fsm.Start(ETestActorState::Idle);
    TestTrue(TEXT("Start 后处于运行态"), Fsm.IsRunning());
    TestEqual(TEXT("当前状态为 Idle"), Fsm.GetCurrentState(), ETestActorState::Idle);
    TestFalse(TEXT("同状态切换返回 false"), Fsm.ChangeState(ETestActorState::Idle));

    Fsm.Update(0.25f);
    Fsm.Update(-0.1f);
    TestTrue(TEXT("驻留时间仅累加正帧长"), FMath::IsNearlyEqual(Fsm.GetTimeInCurrentState(), 0.25f, 1e-4f));

    bEnemySpotted = true;
    bHealthZero = true;
    Fsm.Update(0.1f);
    TestEqual(TEXT("高优先级 Dead(100) 压制低优先级 Combat(10) 获胜"), Fsm.GetCurrentState(), ETestActorState::Dead);
    TestEqual(TEXT("前一状态记录为 Idle"), Fsm.GetPreviousState(), ETestActorState::Idle);
    TestTrue(TEXT("切换至新状态并完成本帧 Update 后驻留时间为 0.1s"), FMath::IsNearlyEqual(Fsm.GetTimeInCurrentState(), 0.1f, 1e-4f));

    TestTrue(TEXT("显式切换至 Combat 成功"), Fsm.ChangeState(ETestActorState::Combat));
    Fsm.Update(0.5f);
    TestTrue(TEXT("Combat OnUpdate 精确接收帧长"), FMath::IsNearlyEqual(CombatUpdatedSeconds, 0.5f, 1e-4f));
    TestEqual(TEXT("全局监听捕获 2 次状态变更"), ObservedChanges.Num(), 2);

    Fsm.Stop();
    TestFalse(TEXT("Stop 后不再处于运行态"), Fsm.IsRunning());
    TestEqual(TEXT("末尾日志为 Exit:Combat"), EventLog.Last(), FString(TEXT("Exit:Combat")));

    Fsm.Reset();
    TestFalse(TEXT("Reset 后未运行"), Fsm.IsRunning());
    TestEqual(TEXT("Reset 后驻留时间清零"), Fsm.GetTimeInCurrentState(), 0.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStateMachineReentrancyAndLoopGuardTest,
    "Game.Core.State.StateMachine.ReentrancyAndLoopGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 OnEnter 回调内链式调用 ChangeState 的因果拉平顺序及无限震荡熔断保护
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameStateMachineReentrancyAndLoopGuardTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TGameStateMachine<ETestActorState> ChainFsm;
    TArray<FString> Sequence;

    ChainFsm.RegisterState(ETestActorState::Idle, {
        [&Sequence, &ChainFsm](ETestActorState) {
            Sequence.Add(TEXT("Enter:Idle"));
            ChainFsm.ChangeState(ETestActorState::Patrol);
        },
        nullptr,
        [&Sequence](ETestActorState) { Sequence.Add(TEXT("Exit:Idle")); }
    });
    ChainFsm.RegisterState(ETestActorState::Patrol, {
        [&Sequence, &ChainFsm](ETestActorState) {
            Sequence.Add(TEXT("Enter:Patrol"));
            ChainFsm.ChangeState(ETestActorState::Combat);
        },
        nullptr,
        [&Sequence](ETestActorState) { Sequence.Add(TEXT("Exit:Patrol")); }
    });
    ChainFsm.RegisterState(ETestActorState::Combat, {
        [&Sequence](ETestActorState) { Sequence.Add(TEXT("Enter:Combat")); },
        nullptr,
        nullptr
    });

    TArray<ETestActorState> NotifiedTargets;
    ChainFsm.SetOnStateChanged([&NotifiedTargets](ETestActorState, ETestActorState Next) {
        NotifiedTargets.Add(Next);
    });

    ChainFsm.Start(ETestActorState::Idle);
    TestFalse(TEXT("正常链式跃迁未触发截断熔断"), ChainFsm.WasTransitionChainTruncated());
    TestEqual(TEXT("链式重入最终停靠于 Combat"), ChainFsm.GetCurrentState(), ETestActorState::Combat);
    TestEqual(TEXT("生命周期序列长度精确为 5"), Sequence.Num(), 5);
    if (Sequence.Num() == 5) {
        TestEqual(TEXT("步骤 0 为 Enter:Idle"), Sequence[0], FString(TEXT("Enter:Idle")));
        TestEqual(TEXT("步骤 1 为 Exit:Idle（先完整退出再进入下一态）"), Sequence[1], FString(TEXT("Exit:Idle")));
        TestEqual(TEXT("步骤 2 为 Enter:Patrol"), Sequence[2], FString(TEXT("Enter:Patrol")));
        TestEqual(TEXT("步骤 3 为 Exit:Patrol"), Sequence[3], FString(TEXT("Exit:Patrol")));
        TestEqual(TEXT("步骤 4 为 Enter:Combat"), Sequence[4], FString(TEXT("Enter:Combat")));
    }
    TestEqual(TEXT("OnStateChanged 顺序通知 Patrol -> Combat"), NotifiedTargets.Num(), 2);

    TGameStateMachine<ETestActorState> FifoFsm;
    TArray<ETestActorState> EnteredStates;
    FifoFsm.RegisterState(ETestActorState::Idle, {
        [&FifoFsm, &EnteredStates](ETestActorState) {
            EnteredStates.Add(ETestActorState::Idle);
            FifoFsm.ChangeState(ETestActorState::Patrol);
            FifoFsm.ChangeState(ETestActorState::Combat);
        },
        nullptr,
        nullptr
    });
    FifoFsm.RegisterState(ETestActorState::Patrol, {
        [&EnteredStates](ETestActorState) {
            EnteredStates.Add(ETestActorState::Patrol);
        },
        nullptr,
        nullptr
    });
    FifoFsm.RegisterState(ETestActorState::Combat, {
        [&EnteredStates](ETestActorState) {
            EnteredStates.Add(ETestActorState::Combat);
        },
        nullptr,
        nullptr
    });
    FifoFsm.Start(ETestActorState::Idle);
    TestEqual(TEXT("同一回调内排队多个状态时按 FIFO 依次经过全部状态"), EnteredStates.Num(), 3);
    TestFalse(TEXT("未注册 Dead 状态时 HasState 返回 false"), FifoFsm.HasState(ETestActorState::Dead));

    TGameStateMachine<ETestActorState> ReentrantMutationFsm;
    ReentrantMutationFsm.RegisterState(ETestActorState::Idle, {
        [&ReentrantMutationFsm](ETestActorState) {
            for (int32 StateId = 1; StateId <= 64; ++StateId) {
                const ETestActorState DynamicState = static_cast<ETestActorState>(StateId);
                ReentrantMutationFsm.RegisterState(DynamicState, {});
                ReentrantMutationFsm.AddTransition(
                    ETestActorState::Idle,
                    DynamicState,
                    [StateId]() { return StateId == 64; },
                    StateId
                );
            }
        },
        nullptr,
        nullptr
    });
    ReentrantMutationFsm.Start(ETestActorState::Idle);
    ReentrantMutationFsm.Update(0.016f);
    TestEqual(
        TEXT("回调内大规模扩容 States 与 Transitions 后安全跃迁至 64"),
        ReentrantMutationFsm.GetCurrentState(),
        static_cast<ETestActorState>(64)
    );

    TGameStateMachine<ETestActorState> OscillatingFsm;
    int32 BounceCount = 0;
    OscillatingFsm.RegisterState(ETestActorState::Patrol, {
        [&OscillatingFsm, &BounceCount](ETestActorState) {
            ++BounceCount;
            OscillatingFsm.ChangeState(ETestActorState::Combat);
        },
        nullptr,
        nullptr
    });
    OscillatingFsm.RegisterState(ETestActorState::Combat, {
        [&OscillatingFsm, &BounceCount](ETestActorState) {
            ++BounceCount;
            OscillatingFsm.ChangeState(ETestActorState::Patrol);
        },
        nullptr,
        nullptr
    });

    OscillatingFsm.Start(ETestActorState::Idle);
    OscillatingFsm.ChangeState(ETestActorState::Patrol);
    TestTrue(TEXT("双向死循环重入后 WasTransitionChainTruncated 为真"), OscillatingFsm.WasTransitionChainTruncated());
    TestEqual(
        TEXT("双向死循环重入被 MAX_CHAINED_TRANSITIONS 精确熔断而绝不栈溢出"),
        BounceCount,
        TGameStateMachine<ETestActorState>::MAX_CHAINED_TRANSITIONS
    );

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameStateMachineStopInExitCallbackTest,
    "Game.Core.State.StateMachine.StopInExitCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证在 OnExit 退出回调内部调用 Stop() 时，退出回调绝不重复执行两次
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameStateMachineStopInExitCallbackTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TGameStateMachine<ETestActorState> Fsm;
    int32 IdleExitInvocations = 0;
    int32 CombatEnterInvocations = 0;

    Fsm.RegisterState(ETestActorState::Idle, {
        nullptr,
        nullptr,
        [&Fsm, &IdleExitInvocations](ETestActorState Target) {
            ++IdleExitInvocations;
            // 在退出回调内部主动停止状态机
            Fsm.Stop();
        }
    });

    Fsm.RegisterState(ETestActorState::Combat, {
        [&CombatEnterInvocations](ETestActorState Prev) {
            ++CombatEnterInvocations;
        },
        nullptr,
        nullptr
    });

    Fsm.Start(ETestActorState::Idle);
    TestTrue(TEXT("状态机启动正常"), Fsm.IsRunning());

    // 请求切态到 Combat，触发 Idle 的 OnExit，其内部调用 Stop()
    Fsm.ChangeState(ETestActorState::Combat);

    TestFalse(TEXT("Stop 后状态机处于停止状态"), Fsm.IsRunning());
    TestEqual(TEXT("Idle 的 OnExit 回调必须严格仅触发 1 次，绝不重复重入"), IdleExitInvocations, 1);
    TestEqual(TEXT("目标态 Combat 的 OnEnter 绝不触发"), CombatEnterInvocations, 0);

    return true;
}

#endif
