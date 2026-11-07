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
     * @brief 为源状态添加一条带优先级的条件守卫自动转换规则（高优先级规则优先求值）
     * @param FromState 源状态
     * @param ToState 目标状态
     * @param Guard 返回 true 时触发跃迁的守卫谓词
     * @param Priority 规则求值优先级（数值越大越先检查，默认 0）
     */
    void AddTransition(StateType FromState, StateType ToState, FTransitionGuard Guard, int32 Priority = 0);

    /**
     * @brief 以指定初始状态启动状态机并触发初始态 OnEnter（若回调内再次请求切态则按队列拉平执行）
     * @param InitialState 初始状态枚举值
     */
    void Start(StateType InitialState);

    /**
     * @brief 停止状态机运行并触发当前状态 OnExit
     */
    void Stop();

    /**
     * @brief 推进单帧逻辑：先按优先级求值当前状态守卫规则，再执行活跃状态 OnUpdate 并累加驻留时长
     * @param DeltaSeconds 帧流逝时间（秒）
     */
    void Update(float DeltaSeconds);

    /**
     * @brief 显式请求切换至目标状态（若处于回调重入中则入队拉平顺序执行，同状态切换直接忽略）
     * @param NewState 目标新状态
     * @return 成功触发或排队状态跃迁返回 true
     */
    bool ChangeState(StateType NewState);

    /**
     * @brief 设置全局状态跃迁完成监听回调
     * @param InObserver 监听闭包
     */
    void SetOnStateChanged(FStateChangedObserver InObserver);

    /**
     * @brief 清空所有状态定义、转换规则与运行态
     */
    void Reset();

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

private:
    void ExecuteTransitionLoop(StateType InitialTarget);

private:
    TMap<StateType, FStateCallbacks> States;
    TMap<StateType, TArray<FTransitionRule>> Transitions;
    FStateChangedObserver OnStateChanged;
    TOptional<StateType> QueuedState;
    StateType CurrentState{};
    StateType PreviousState{};
    float TimeInCurrentState = 0.0f;
    bool bIsRunning = false;
    bool bIsTransitioning = false;
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
    if (!Guard) {
        return;
    }
    TArray<FTransitionRule>& RuleList = Transitions.FindOrAdd(FromState);
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
    QueuedState.Reset();
    PreviousState = InitialState;
    CurrentState = InitialState;
    TimeInCurrentState = 0.0f;
    bIsRunning = true;

    bIsTransitioning = true;
    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        if (Node->OnEnter) {
            Node->OnEnter(PreviousState);
        }
    }
    bIsTransitioning = false;

    if (QueuedState.IsSet()) {
        const StateType NextTarget = QueuedState.GetValue();
        QueuedState.Reset();
        if (NextTarget != CurrentState) {
            ExecuteTransitionLoop(NextTarget);
        }
    }
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Stop() {
    if (!bIsRunning) {
        return;
    }
    QueuedState.Reset();
    bIsRunning = false;
    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        if (Node->OnExit) {
            Node->OnExit(CurrentState);
        }
    }
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Update(float DeltaSeconds) {
    if (!bIsRunning) {
        return;
    }

    if (const TArray<FTransitionRule>* Rules = Transitions.Find(CurrentState)) {
        for (const FTransitionRule& Rule : *Rules) {
            if (Rule.TargetState != CurrentState && Rule.Guard && Rule.Guard()) {
                ChangeState(Rule.TargetState);
                break;
            }
        }
    }

    if (!bIsRunning) {
        return;
    }

    if (DeltaSeconds > 0.0f) {
        TimeInCurrentState += DeltaSeconds;
    }

    if (const FStateCallbacks* Node = States.Find(CurrentState)) {
        if (Node->OnUpdate) {
            Node->OnUpdate(DeltaSeconds);
        }
    }
}

template <CGameStateEnum TStateEnum>
bool TGameStateMachine<TStateEnum>::ChangeState(StateType NewState) {
    if (!bIsRunning) {
        return false;
    }
    if (bIsTransitioning) {
        QueuedState = NewState;
        return true;
    }
    if (CurrentState == NewState) {
        return false;
    }

    ExecuteTransitionLoop(NewState);
    return true;
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::SetOnStateChanged(FStateChangedObserver InObserver) {
    OnStateChanged = MoveTemp(InObserver);
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::Reset() {
    States.Empty();
    Transitions.Empty();
    OnStateChanged = nullptr;
    QueuedState.Reset();
    CurrentState = StateType{};
    PreviousState = StateType{};
    TimeInCurrentState = 0.0f;
    bIsRunning = false;
    bIsTransitioning = false;
}

template <CGameStateEnum TStateEnum>
void TGameStateMachine<TStateEnum>::ExecuteTransitionLoop(StateType InitialTarget) {
    bIsTransitioning = true;
    StateType PendingTarget = InitialTarget;

    for (int32 ChainStep = 0; ChainStep < MAX_CHAINED_TRANSITIONS && bIsRunning; ++ChainStep) {
        if (PendingTarget == CurrentState) {
            break;
        }

        const StateType DepartingState = CurrentState;
        if (const FStateCallbacks* OldNode = States.Find(DepartingState)) {
            if (OldNode->OnExit) {
                OldNode->OnExit(PendingTarget);
            }
        }

        if (!bIsRunning) {
            break;
        }

        PreviousState = DepartingState;
        CurrentState = PendingTarget;
        TimeInCurrentState = 0.0f;

        if (const FStateCallbacks* NewNode = States.Find(CurrentState)) {
            if (NewNode->OnEnter) {
                NewNode->OnEnter(PreviousState);
            }
        }

        if (OnStateChanged) {
            OnStateChanged(PreviousState, CurrentState);
        }

        if (!QueuedState.IsSet()) {
            break;
        }
        PendingTarget = QueuedState.GetValue();
        QueuedState.Reset();
    }

    QueuedState.Reset();
    bIsTransitioning = false;
}
