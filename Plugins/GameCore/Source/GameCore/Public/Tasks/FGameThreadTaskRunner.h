/**
 * @file FGameThreadTaskRunner.h
 * @brief 后台任务与回主线程续体的统一执行工具（Core 级异步规范）
 *
 * 设计思路：以 UE::Tasks::Launch 派发后台工作，经 AsyncTask 投递回 GameThread 执行续体；
 * 续体执行前校验宿主 TWeakObjectPtr 有效性，宿主已销毁即静默丢弃（不回调、不解引用）。
 * 全项目异步路径统一经此工具，禁止各处自行拼装 Launch + AsyncTask 组合。
 *
 * 边界约束：后台工作运行于工作线程，严禁触碰 UObject/GC/引擎单例的可变状态；
 * 一切 UObject 交互必须落在回主线程的续体内。
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

/**
 * @brief 后台任务与回主线程续体统一执行工具（纯静态类，禁止实例化）
 */
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    /**
     * @brief 派发后台任务（工作线程执行）
     * @param Owner 宿主弱引用（已失效则提前短路不派发）
     * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
     */
    static void Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);

    /**
     * @brief 回主线程执行续体（宿主失效时静默丢弃，续体内解引用宿主安全）
     * @param Owner 宿主弱引用
     * @param GameThreadWork 续体（GameThread 执行，可安全操作 UObject）
     */
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);

    /**
     * @brief 派发后台任务，并在完成后将计算结果安全投递回 GameThread 续体执行
     * @tparam TResult 计算结果类型（支持移动语义）
     * @param Owner 宿主弱引用（任意阶段失效即自动短路丢弃）
     * @param BackgroundWork 后台工作体（工作线程执行，返回计算结果，禁止操作 UObject）
     * @param GameThreadContinuation 主线程续体（主线程执行，接收计算结果，安全访问 UObject）
     */
    template <typename TResult>
    static void RunWithContinuation(
        TWeakObjectPtr<UObject> Owner,
        TFunction<TResult()> BackgroundWork,
        TFunction<void(TResult)> GameThreadContinuation
    );

    /**
     * @brief 派发后台无返回值任务，并在完成后安全投递回 GameThread 续体执行
     * @param Owner 宿主弱引用（任意阶段失效即自动短路丢弃）
     * @param BackgroundWork 后台工作体（工作线程执行）
     * @param GameThreadContinuation 主线程续体（主线程执行）
     */
    static void RunWithContinuation(
        TWeakObjectPtr<UObject> Owner,
        TFunction<void()> BackgroundWork,
        TFunction<void()> GameThreadContinuation
    );
};

template <typename TResult>
void FGameThreadTaskRunner::RunWithContinuation(
    TWeakObjectPtr<UObject> Owner,
    TFunction<TResult()> BackgroundWork,
    TFunction<void(TResult)> GameThreadContinuation
) {
    Run(Owner, [Owner, BgWork = MoveTemp(BackgroundWork), Continuation = MoveTemp(GameThreadContinuation)]() mutable {
        TResult Result = BgWork();
        RunOnGameThread(Owner, [Continuation = MoveTemp(Continuation), Res = MoveTemp(Result)]() mutable {
            Continuation(MoveTemp(Res));
        });
    });
}
