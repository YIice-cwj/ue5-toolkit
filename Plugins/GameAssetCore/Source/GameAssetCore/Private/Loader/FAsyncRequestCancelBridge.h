/**
 * @file FAsyncRequestCancelBridge.h
 * @brief 异步流式回调与 FCancellationToken 双向竞态安全桥接状态（私有内部头文件）
 */

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/FGameThreadTaskRunner.h"
#include "Engine/StreamableManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"

namespace Game::Asset::Private {

/**
 * @brief 隔离流式回调同步触发、句柄主动取消与外部取消令牌跨线程触发的共享桥接状态
 */
struct FAsyncRequestCancelBridge {
    FCriticalSection Lock;
    TWeakPtr<FStreamableHandle> WeakHandle;
    FCancellationTokenRegistration TokenRegistration;
    std::atomic<bool> bFinished{false};
    bool bClosed = false;
    bool bCancelRequestedBeforeBind = false;

    /**
     * @brief 确保底层 FStreamableHandle 的取消与最终析构严格落在 GameThread 执行
     * @param InHandle 待取消的流式加载句柄（所有权转移至本函数）
     */
    static void CancelHandleOnGameThread(TSharedPtr<FStreamableHandle> InHandle) {
        if (!InHandle.IsValid()) {
            return;
        }
        if (IsInGameThread()) {
            if (InHandle->IsActive()) {
                InHandle->CancelHandle();
            }
            return;
        }
        FGameThreadTaskRunner::RunOnGameThread([Handle = MoveTemp(InHandle)]() mutable {
            if (Handle.IsValid() && Handle->IsActive()) {
                Handle->CancelHandle();
            }
        });
    }

    /**
     * @brief 绑定流式加载句柄；若绑定前已收到取消信号则安全投递至 GameThread 立即取消
     * @param InHandle 底层流式句柄
     */
    void AttachHandle(const TSharedPtr<FStreamableHandle>& InHandle) {
        bool bShouldCancelImmediately = false;
        {
            FScopeLock ScopeLock(&Lock);
            WeakHandle = InHandle;
            bShouldCancelImmediately = bCancelRequestedBeforeBind;
        }
        if (bShouldCancelImmediately) {
            CancelHandleOnGameThread(InHandle);
        }
    }

    /**
     * @brief 在互斥锁保护下挂载取消令牌注销句柄；若桥接状态已关闭则出锁立即注销防止泄漏
     * @param InRegistration 取消令牌注销句柄
     */
    void AttachTokenRegistration(FCancellationTokenRegistration InRegistration) {
        FCancellationTokenRegistration ToUnregister;
        {
            FScopeLock ScopeLock(&Lock);
            if (bClosed) {
                ToUnregister = MoveTemp(InRegistration);
            } else {
                TokenRegistration = MoveTemp(InRegistration);
            }
        }
        ToUnregister.Unregister();
    }

    /**
     * @brief 响应协同取消令牌触发，原子抢占终态、出锁解绑令牌并安全在 GameThread 取消在途流式句柄
     */
    void CancelFromToken() {
        if (bFinished.exchange(true, std::memory_order_acq_rel)) {
            return;
        }
        TSharedPtr<FStreamableHandle> PinnedHandle;
        FCancellationTokenRegistration LocalRegistration;
        {
            FScopeLock ScopeLock(&Lock);
            bClosed = true;
            bCancelRequestedBeforeBind = true;
            PinnedHandle = WeakHandle.Pin();
            LocalRegistration = MoveTemp(TokenRegistration);
        }
        LocalRegistration.Unregister();
        CancelHandleOnGameThread(MoveTemp(PinnedHandle));
    }

    /**
     * @brief 响应底层流式完成回调，原子抢占终态并在锁外注销取消令牌监听
     * @return 首次进入终态返回 true，已被取消抢占返回 false
     */
    [[nodiscard]] bool TryFinishFromStreamable() {
        if (bFinished.exchange(true, std::memory_order_acq_rel)) {
            return false;
        }
        FCancellationTokenRegistration LocalRegistration;
        {
            FScopeLock ScopeLock(&Lock);
            bClosed = true;
            LocalRegistration = MoveTemp(TokenRegistration);
        }
        LocalRegistration.Unregister();
        return true;
    }

    /**
     * @brief 响应外部通过 FGameAssetLoadHandle::Cancel() 主动取消，立即解绑令牌监听防止长生命周期令牌残留
     */
    void NotifyHandleCanceled() {
        (void)TryFinishFromStreamable();
    }

    /**
     * @brief 校验批量流式句柄无底层错误且全部去重目标资产均已解析为有效对象
     * @param ExpectedUniqueCount 期望成功解析的唯一资产总数
     * @return 全部资产均有效加载返回 true
     */
    [[nodiscard]] bool AreAllLoadedAssetsValid(int32 ExpectedUniqueCount) {
        TSharedPtr<FStreamableHandle> PinnedHandle;
        {
            FScopeLock ScopeLock(&Lock);
            PinnedHandle = WeakHandle.Pin();
        }
        if (!PinnedHandle.IsValid() || PinnedHandle->HasError()) {
            return false;
        }
        int32 ValidLoadedCount = 0;
        PinnedHandle->ForEachLoadedAsset([&ValidLoadedCount](UObject* LoadedAsset) {
            if (::GetValid(LoadedAsset) != nullptr) {
                ++ValidLoadedCount;
            }
        });
        return ValidLoadedCount == ExpectedUniqueCount;
    }
};

}  // namespace Game::Asset::Private
