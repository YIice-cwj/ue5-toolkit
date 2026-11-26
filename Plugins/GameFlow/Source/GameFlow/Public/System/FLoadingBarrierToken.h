/**
 * @file FLoadingBarrierToken.h
 * @brief 关卡加载幕布异步就绪屏障零堆分配 RAII 令牌声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/GameFlowTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

class ULoadingSubsystem;

/**
 * @brief 加载幕布就绪屏障值语义 RAII 令牌（零堆分配，析构自动释放屏障，支持手动提前幂等释放与移动所有权转移）
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
     * @brief 移动构造函数（接管屏障生命周期并将源对象重置为已释放空状态）
     * @param Other 源屏障令牌
     */
    FLoadingBarrierToken(FLoadingBarrierToken&& Other) noexcept;

    /**
     * @brief 移动赋值运算符（先释放当前持有的屏障，再接管源对象所有权）
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
     * @brief 上报该屏障当前内部执行进度与当前阶段说明（用于驱动全局平滑进度条；非有限浮点输入将被忽略）
     * @param InProgress 进度百分比（0.0f ~ 1.0f，内部自动 clamp）
     * @param InDescription 当前步骤说明文案
     */
    void ReportProgress(float InProgress, const FText& InDescription = FText::GetEmpty());

    /**
     * @brief 查询该令牌本地是否已被显式释放、默认构造或被移动转移
     * @return 本地已释放返回 true
     */
    [[nodiscard]] bool IsReleased() const noexcept {
        return bReleased;
    }

    /**
     * @brief 查询该屏障当前是否仍在加载子系统中处于活跃有效状态（若已被看门狗强制超时或外部释放则返回 false）
     * @return 子系统存活且该屏障代际句柄仍活跃返回 true
     */
    [[nodiscard]] bool IsValid() const noexcept;

    /**
     * @brief 布尔类型转换操作符（等价于 IsValid()）
     * @return 当前持有活跃有效屏障返回 true
     */
    [[nodiscard]] explicit operator bool() const noexcept {
        return IsValid();
    }

    /**
     * @brief 获取当前屏障最近一次成功上报的内部完成度进度
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
     * @brief 获取屏障 64 位强类型代际句柄
     * @return 强类型代际句柄（已释放或默认构造时为无效句柄）
     */
    [[nodiscard]] FLoadingBarrierHandle GetBarrierHandle() const noexcept {
        return BarrierHandle;
    }

private:
    friend class ULoadingSubsystem;

    /**
     * @brief 仅供 ULoadingSubsystem 构造有效屏障令牌的内部构造函数
     * @param InSubsystem 所属加载子系统指针
     * @param InBarrierHandle 分配的代际句柄
     * @param InBarrierName 屏障诊断名称
     */
    FLoadingBarrierToken(
        ULoadingSubsystem* InSubsystem,
        FLoadingBarrierHandle InBarrierHandle,
        const FName& InBarrierName
    ) noexcept;

    /**
     * @brief 从右值源令牌接管字段所有权并彻底重置源令牌为空状态
     * @param Other 源屏障令牌
     */
    void TakeOwnershipFrom(FLoadingBarrierToken&& Other) noexcept;

private:
    TWeakObjectPtr<ULoadingSubsystem> SubsystemWeak;
    FLoadingBarrierHandle BarrierHandle;
    FName BarrierName = NAME_None;
    float CurrentProgress = 0.0f;
    bool bReleased = true;
};
