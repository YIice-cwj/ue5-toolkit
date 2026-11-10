/**
 * @file TGameStateMachine.h
 * @brief 强类型通用有限状态机模板（支持优先级守卫跃迁、链式重入拉平与驻留计时）
 */

#pragma once

#include "CoreMinimal.h"

#include <type_traits>

/**
 * @brief 状态枚举编译期约束概念
 * @tparam T 待校验的状态枚举类型
 */
template <typename T>
concept CGameStateEnum = std::is_enum_v<T>;

/**
 * @brief 强类型事件与守卫驱动有限状态机模板
 * @tparam TStateEnum 状态枚举类型
 */
template <CGameStateEnum TStateEnum>
class TGameStateMachine {
public:
    using StateType = TStateEnum;
    using FEnterCallback = TFunction<void(StateType PreviousState)>;
    using FUpdateCallback = TFunction<void(float DeltaSeconds)>;
    using FExitCallback = TFunction<void(StateType NextState)>;
    using FTransitionGuard = TFunction<bool()>;
    using FStateChangedObserver = TFunction<void(StateType PreviousState, StateType NewState)>;

    struct FStateCallbacks {
        FEnterCallback OnEnter;
        FUpdateCallback OnUpdate;
        FExitCallback OnExit;
    };

private:
    struct FTransitionRule {
        StateType TargetState{};
        FTransitionGuard Guard;
        int32 Priority = 0;
    };

public:
    static constexpr int32 MAX_CHAINED_TRANSITIONS = 8;

public:
    TGameStateMachine() = default;

    /**
     * @brief 注册或覆盖指定状态的生命周期回调集合
     * @param State 目标状态枚举值
     * @param Callbacks 进入、更新与退出回调
     */
    void RegisterState(StateType State, FStateCallbacks Callbacks);

    /**
     * @brief 为源状态添加一条带优先级的条件守卫自动转换规则（高优先级规则优先求值；空 Guard 或自转移规则将被忽略）
     * @param FromState 源状态
     * @param ToState 目标状态
     * @param Guard 返回 true 时触发跃迁的守卫谓词
     * @param Priority 规则求值优先级（数值越大越先检查，默认 0）
     */
    void AddTransition(StateType FromState, StateType ToState, FTransitionGuard Guard, int32 Priority = 0);

    /**
     * @brief 以指定初始状态启动状态机并触发初始态 OnEnter（若已处于运行态则先执行 Stop 触发旧态 OnExit；回调内请求切态按 FIFO 队列拉平执行）
     * @param InitialState 初始状态枚举值
     */
    void Start(StateType InitialState);

    /**
     * @brief 停止状态机运行并触发当前状态 OnExit（停止态下传入当前状态自身表示终止退出）
     */
    void Stop();

    /**
     * @brief 推进单帧逻辑：先基于规则快照按优先级求值守卫，再执行活跃状态 OnUpdate 并累加驻留时长
     * @param DeltaSeconds 帧流逝时间（秒）
     */
    void Update(float DeltaSeconds);

    /**
     * @brief 显式请求切换至目标状态（允许跃迁至未绑定回调的轻量纯枚举标签状态；若需强制校验已注册回调请使用 ChangeStateIfRegistered；若处于回调重入中则入 FIFO 队列拉平顺序执行，同状态切换直接忽略）
     * @param NewState 目标新状态
     * @return 成功触发或入队状态跃迁返回 true
     */
    bool ChangeState(StateType NewState);

    /**
     * @brief 仅当目标状态已通过 RegisterState 显式注册时请求切换状态（严格校验目标状态已绑定回调配置）
     * @param NewState 已注册的目标新状态
     * @return 目标状态已注册且成功切换或入队返回 true
     */
    bool ChangeStateIfRegistered(StateType NewState);

    /**
     * @brief 检查指定状态是否已显式注册回调节点
     * @param State 待查询状态枚举值
     * @return 已注册返回 true
     */
    [[nodiscard]] bool HasState(StateType State) const noexcept {
        return States.Contains(State);
    }

    /**
     * @brief 设置全局状态跃迁完成监听回调
     * @param InObserver 监听闭包
     */
    void SetOnStateChanged(FStateChangedObserver InObserver);

    /**
     * @brief 清空所有状态定义、转换规则与运行态
     * @param bFireExitIfRunning 若当前处于运行态，是否在清表前先调用 Stop 触发当前态 OnExit
     */
    void Reset(bool bFireExitIfRunning = false);

    /**
     * @brief 获取当前所处状态
     * @return 当前状态枚举值
     */
    [[nodiscard]] StateType GetCurrentState() const noexcept {
        return CurrentState;
    }

    /**
     * @brief 获取上一次所处状态
     * @return 前一状态枚举值
     */
    [[nodiscard]] StateType GetPreviousState() const noexcept {
        return PreviousState;
    }

    /**
     * @brief 获取在当前状态已累计驻留的秒数
     * @return 驻留时长（秒）
     */
    [[nodiscard]] float GetTimeInCurrentState() const noexcept {
        return TimeInCurrentState;
    }

    /**
     * @brief 检查状态机是否正在运行中
     * @return 已启动未停止返回 true
     */
    [[nodiscard]] bool IsRunning() const noexcept {
        return bIsRunning;
    }

    /**
     * @brief 查询最近一次状态跃迁是否因达到 MAX_CHAINED_TRANSITIONS 上限而被截断熔断
     * @return 发生链式熔断返回 true
     */
    [[nodiscard]] bool WasTransitionChainTruncated() const noexcept {
        return bChainTruncated;
    }

private:
    void ExecuteTransitionLoop(StateType InitialTarget);

private:
    TMap<StateType, FStateCallbacks> States;
    TMap<StateType, TSharedPtr<TArray<FTransitionRule>>> Transitions;
    FStateChangedObserver OnStateChanged;
    TArray<StateType, TInlineAllocator<MAX_CHAINED_TRANSITIONS>> QueuedStates;
    StateType CurrentState{};
    StateType PreviousState{};
    float TimeInCurrentState = 0.0f;
    bool bIsRunning = false;
    bool bIsTransitioning = false;
    bool bChainTruncated = false;
};

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::RegisterState(StateType State, FStateCallbacks Callbacks) {
    States.Add(State, MoveTemp(Callbacks));
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::AddTransition(
    StateType FromState,
    StateType ToState,
    FTransitionGuard Guard,
    int32 Priority
) {
    if (!Guard || FromState == ToState) {
        return;
    }
    TSharedPtr<TArray<FTransitionRule>>& RuleListPtr = Transitions.FindOrAdd(FromState);
    if (!RuleListPtr.IsValid()) {
        RuleListPtr = MakeShared<TArray<FTransitionRule>>();
    } else if (!RuleListPtr.IsUnique()) {
        RuleListPtr = MakeShared<TArray<FTransitionRule>>(*RuleListPtr);
    }

    TArray<FTransitionRule>& RuleList = *RuleListPtr;
    int32 InsertIndex = RuleList.Num();
    for (int32 Index = 0; Index < RuleList.Num(); ++Index) {
        if (Priority > RuleList[Index].Priority) {
            InsertIndex = Index;
            break;
        }
    }
    RuleList.Insert(FTransitionRule{ToState, MoveTemp(Guard), Priority}, InsertIndex);
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Start(StateType InitialState) {
    if (bIsRunning) {
        Stop();
    }

    QueuedStates.Reset();
    bChainTruncated = false;
    PreviousState = InitialState;
    CurrentState = InitialState;
    TimeInCurrentState = 0.0f;
    bIsRunning = true;

    bIsTransitioning = true;
    FEnterCallback EnterCopy;
    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        EnterCopy = Node->OnEnter;
    }
    if (EnterCopy) {
        EnterCopy(PreviousState);
    }
    bIsTransitioning = false;

    while (bIsRunning && !QueuedStates.IsEmpty()) {
        const StateType NextTarget = QueuedStates[0];
        QueuedStates.RemoveAt(0, EAllowShrinking::No);
        if (NextTarget != CurrentState) {
            ExecuteTransitionLoop(NextTarget);
            break;
        }
    }
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Stop() {
    if (!bIsRunning) {
        return;
    }
    QueuedStates.Reset();
    bIsRunning = false;
    bIsTransitioning = false;

    FExitCallback ExitCopy;
    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        ExitCopy = Node->OnExit;
    }
    if (ExitCopy) {
        ExitCopy(CurrentState);
    }
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Update(float DeltaSeconds) {
    if (!bIsRunning) {
        return;
    }

    if (const TSharedPtr<TArray<FTransitionRule>>* RulesPtr = Transitions.Find(CurrentState)) {
        if (RulesPtr->IsValid()) {
            const TSharedPtr<const TArray<FTransitionRule>> RulesSnapshot = *RulesPtr;
            for (const FTransitionRule& Rule : *RulesSnapshot) {
                if (!bIsRunning) {
                    return;
                }
                if (Rule.TargetState != CurrentState && Rule.Guard && Rule.Guard()) {
                    if (bIsRunning) {
                        ChangeState(Rule.TargetState);
                    }
                    break;
                }
            }
        }
    }

    if (!bIsRunning) {
        return;
    }

    if (DeltaSeconds > 0.0f) {
        TimeInCurrentState += DeltaSeconds;
    }

    FUpdateCallback UpdateCopy;
    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        UpdateCopy = Node->OnUpdate;
    }
    if (UpdateCopy) {
        UpdateCopy(DeltaSeconds);
    }
}

template <CGameStateEnum TStateEnum>
bool TGameStateMachine<TStateEnum>::ChangeState(StateType NewState) {
    if (!bIsRunning) {
        return false;
    }
    if (bIsTransitioning) {
        if (QueuedStates.Num() < MAX_CHAINED_TRANSITIONS) {
            QueuedStates.Add(NewState);
            return true;
        }
        bChainTruncated = true;
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("TGameStateMachine: Transition queue overflowed MAX_CHAINED_TRANSITIONS (%d); truncating chain."),
            MAX_CHAINED_TRANSITIONS
        );
        return false;
    }
    if (CurrentState == NewState) {
        return false;
    }

    bChainTruncated = false;
    ExecuteTransitionLoop(NewState);
    return true;
}

template <CGameStateEnum TStateEnum>
bool TGameStateMachine<TStateEnum>::ChangeStateIfRegistered(StateType NewState) {
    if (!States.Contains(NewState)) {
        return false;
    }
    return ChangeState(NewState);
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::SetOnStateChanged(FStateChangedObserver InObserver) {
    OnStateChanged = MoveTemp(InObserver);
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Reset(bool bFireExitIfRunning) {
    if (bFireExitIfRunning && bIsRunning) {
        Stop();
    }
    States.Empty();
    Transitions.Empty();
    OnStateChanged = nullptr;
    QueuedStates.Reset();
    CurrentState = StateType{};
    PreviousState = StateType{};
    TimeInCurrentState = 0.0f;
    bIsRunning = false;
    bIsTransitioning = false;
    bChainTruncated = false;
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::ExecuteTransitionLoop(StateType InitialTarget) {
    bIsTransitioning = true;
    StateType PendingTarget = InitialTarget;
    int32 ChainStep = 0;

    for (; ChainStep < MAX_CHAINED_TRANSITIONS && bIsRunning; ++ChainStep) {
        if (PendingTarget == CurrentState) {
            if (QueuedStates.IsEmpty()) {
                break;
            }
            PendingTarget = QueuedStates[0];
            QueuedStates.RemoveAt(0, EAllowShrinking::No);
            continue;
        }

        const StateType DepartingState = CurrentState;
        FExitCallback ExitCopy;
        if (const FStateCallbacks* OldNode = States.Find(DepartingState)) {
            ExitCopy = OldNode->OnExit;
        }
        if (ExitCopy) {
            ExitCopy(PendingTarget);
        }

        if (!bIsRunning) {
            break;
        }

        PreviousState = DepartingState;
        CurrentState = PendingTarget;
        TimeInCurrentState = 0.0f;

        FEnterCallback EnterCopy;
        if (const FStateCallbacks* NewNode = States.Find(CurrentState)) {
            EnterCopy = NewNode->OnEnter;
        }
        if (EnterCopy) {
            EnterCopy(PreviousState);
        }

        if (!bIsRunning) {
            break;
        }

        FStateChangedObserver ObserverCopy = OnStateChanged;
        if (ObserverCopy) {
            ObserverCopy(PreviousState, CurrentState);
        }

        if (!bIsRunning || QueuedStates.IsEmpty()) {
            break;
        }
        if (ChainStep + 1 >= MAX_CHAINED_TRANSITIONS) {
            bChainTruncated = true;
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("TGameStateMachine: Chained transition limit MAX_CHAINED_TRANSITIONS (%d) reached; breaking loop."),
                MAX_CHAINED_TRANSITIONS
            );
            break;
        }
        PendingTarget = QueuedStates[0];
        QueuedStates.RemoveAt(0, EAllowShrinking::No);
    }

    QueuedStates.Reset();
    bIsTransitioning = false;
}
