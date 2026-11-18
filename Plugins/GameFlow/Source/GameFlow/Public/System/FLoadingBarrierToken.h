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
    /**
     * @brief 默认构造函数（构造一个初始处于已释放状态的空令牌）
     */
    FLoadingBarrierToken() noexcept = default;

    FLoadingBarrierToken(const FLoadingBarrierToken&) = delete;
    FLoadingBarrierToken& operator=(const FLoadingBarrierToken&) = delete;

    /**
     * @brief 移动构造函数（接管屏障生命周期并将源对象标记为已释放）
     * @param Other 源屏障令牌
     */
    FLoadingBarrierToken(FLoadingBarrierToken&& Other) noexcept;

    /**
     * @brief 移动赋值运算符（释放当前持有的屏障并接管源对象）
     * @param Other 源屏障令牌
     * @return 自身引用
     */
    FLoadingBarrierToken& operator=(FLoadingBarrierToken&& Other) noexcept;

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
     * @brief 查询当前屏障令牌是否有效（未释放且关联子系统仍然存活）
     * @return 有效返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept {
        return !bReleased && SubsystemWeak.IsValid();
    }

    /**
     * @brief 布尔类型转换操作符
     * @return 当前持有有效屏障返回 true
     */
    explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 上报该屏障当前内部执行进度与当前阶段说明（用于驱动全局平滑进度条）
     * @param InProgress 进度百分比（0.0f ~ 1.0f，内部自动 clamp）
     * @param InDescription 当前步骤说明文案
     */
    void ReportProgress(float InProgress, const FText& InDescription = FText::GetEmpty());

    /**
     * @brief 获取当前屏障上报的内部完成度进度
     * @return 进度值 0.0f ~ 1.0f
     */
    [[nodiscard]] float GetProgress() const noexcept {
        return CurrentProgress;
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
    float CurrentProgress = 0.0f;
    bool bReleased = true;
};
