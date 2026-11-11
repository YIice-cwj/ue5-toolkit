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
#include "Async/FCancellationToken.h"
#include "Misc/Timespan.h"
#include "Tasks/Task.h"
#include "UObject/Object.h"

/**
 * @brief 后台异步任务控制句柄（封装 UE::Tasks::FTask 与任务级协同取消状态，支持主动 Cancel / Wait / Stop）
 */
class GAMECORE_API FGameAsyncTaskHandle {
public:
    FGameAsyncTaskHandle() = default;

    /**
     * @brief 构造绑定底层任务与取消状态的异步任务句柄
     * @param InTask 底层 UE Tasks 句柄
     * @param InCancelState 任务级协同取消共享状态
     */
    FGameAsyncTaskHandle(UE::Tasks::FTask InTask, TSharedPtr<FCancellationState> InCancelState) noexcept;

    /**
     * @brief 查询句柄是否绑定了有效后台任务
     * @return 有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * @brief 查询后台任务是否已执行完成
     * @return 已完成或未绑定有效任务返回 true
     */
    [[nodiscard]] bool IsCompleted() const noexcept;

    /**
     * @brief 向当前后台任务发送协同取消请求
     * @return 首次成功触发取消返回 true
     */
    bool Cancel();

    /**
     * @brief 等待后台任务执行完毕（参与 UE TaskGraph 工作窃取回撤，零忙等自旋）
     */
    void Wait() const;

    /**
     * @brief 带超时等待后台任务执行完毕
     * @param Timeout 最大等待时长
     * @return 超时前任务完成返回 true
     */
    bool WaitWithTimeout(FTimespan Timeout) const;

    /**
     * @brief 请求取消并等待当前后台任务退出（Stop 语义）
     * @param Timeout 最大等待时长
     * @return 超时前任务完成返回 true
     */
    bool Stop(FTimespan Timeout = FTimespan::FromSeconds(2.0));

private:
    UE::Tasks::FTask Task;
    TSharedPtr<FCancellationState> CancelState;
};

/**
 * @brief 后台任务与回主线程续体统一执行工具（纯静态类，禁止实例化，支持任务句柄控制、在飞任务追踪、协同取消与排空等待）
 */
class GAMECORE_API FGameThreadTaskRunner {
public:
    FGameThreadTaskRunner() = delete;

    /**
     * @brief 派发后台任务（工作线程执行，自动追踪在飞计数并返回可控任务句柄）
     * @param Owner 宿主弱引用（已失效则提前短路不派发）
     * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
     * @return 异步任务控制句柄
     */
    static FGameAsyncTaskHandle Run(TWeakObjectPtr<UObject> Owner, TFunction<void()> BackgroundWork);

    /**
     * @brief 派发支持协同取消令牌的后台任务（工作线程执行，自动追踪在飞计数并返回可控任务句柄）
     * @param Owner 宿主弱引用（已失效则提前短路不派发）
     * @param CancellationToken 协同取消令牌（已取消则提前短路不执行）
     * @param BackgroundWork 后台工作体
     * @return 异步任务控制句柄
     */
    static FGameAsyncTaskHandle Run(
        TWeakObjectPtr<UObject> Owner,
        FCancellationToken CancellationToken,
        TFunction<void()> BackgroundWork
    );

    /**
     * @brief 派发无特定 UObject 宿主绑定的全局后台任务（支持协同取消令牌与在飞计数追踪）
     * @param CancellationToken 协同取消令牌
     * @param BackgroundWork 后台工作体（工作线程执行，禁止触碰 UObject）
     * @return 异步任务控制句柄
     */
    static FGameAsyncTaskHandle Run(
        FCancellationToken CancellationToken,
        TFunction<void()> BackgroundWork
    );

    /**
     * @brief 回主线程执行续体（宿主失效时静默丢弃，续体内解引用宿主安全）
     * @param Owner 宿主弱引用
     * @param GameThreadWork 续体（GameThread 执行，可安全操作 UObject）
     */
    static void RunOnGameThread(TWeakObjectPtr<UObject> Owner, TFunction<void()> GameThreadWork);

    /**
     * @brief 投递无特定 UObject 宿主绑定的全局续体至主线程执行（受全局取消世代保护）
     * @param GameThreadWork 主线程工作体
     */
    static void RunOnGameThread(TFunction<void()> GameThreadWork);

    /**
     * @brief 请求全局取消所有已排队但尚未开始执行（或正在等待投递回主线程）的后台任务
     */
    static void CancelAllPendingTasks() noexcept;

    /**
     * @brief 查询当前正在后台工作线程中执行的在飞任务数量
     * @return 在飞后台任务计数
     */
    [[nodiscard]] static int32 GetInFlightTaskCount() noexcept;

    /**
     * @brief 等待所有已派发的后台在飞任务执行完毕（主线程安全等待并泵送续体队列，消除自旋死锁，供模块关停或单元测试排空使用）
     * @param TimeoutSeconds 最大等待超时秒数
     * @return 超时前全部在飞任务排空返回 true
     */
    static bool FlushInFlightTasks(double TimeoutSeconds = 2.0);
};
