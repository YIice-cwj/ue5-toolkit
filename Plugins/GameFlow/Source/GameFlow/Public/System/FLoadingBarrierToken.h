/**
 * @file FLoadingBarrierToken.h
 * @brief 关卡加载幕布异步就绪屏障 RAII 令牌
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ULoadingSubsystem;

/**
 * @brief 加载幕布就绪屏障令牌（析构时自动释放屏障，支持手动提前幂等释放）
 */
class GAMEFLOW_API FLoadingBarrierToken {
public:
    FLoadingBarrierToken(const FLoadingBarrierToken&) = delete;
    FLoadingBarrierToken& operator=(const FLoadingBarrierToken&) = delete;
    FLoadingBarrierToken(FLoadingBarrierToken&&) = delete;
    FLoadingBarrierToken& operator=(FLoadingBarrierToken&&) = delete;

    /**
     * @brief 析构函数：若尚未释放则自动通知加载子系统解除当前屏障
     */
    ~FLoadingBarrierToken();

    /**
     * @brief 显式释放当前加载屏障（重复调用幂等安全无副作用）
     */
    void Release();

    /**
     * @brief 查询该屏障令牌是否已被释放
     * @return 已释放返回 true
     */
    [[nodiscard]] bool IsReleased() const noexcept {
        return bReleased;
    }

    /**
     * @brief 获取申请屏障时登记的诊断名称
     * @return 屏障名称
     */
    [[nodiscard]] FName GetBarrierName() const noexcept {
        return BarrierName;
    }

    /**
     * @brief 获取屏障唯一递增标识 ID
     * @return 屏障 ID
     */
    [[nodiscard]] uint64 GetBarrierId() const noexcept {
        return BarrierId;
    }

private:
    friend class ULoadingSubsystem;

    FLoadingBarrierToken(
        ULoadingSubsystem* InSubsystem,
        uint64 InBarrierId,
        const FName& InBarrierName) noexcept;

private:
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    uint64 BarrierId = 0ULL;
    FName BarrierName = NAME_None;
    bool bReleased = false;
};
