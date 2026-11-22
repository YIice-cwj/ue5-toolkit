// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file GameLoggingSubsystem.h
 * @brief 游戏实例级远程日志常驻子系统公共门面声明（CDO 零堆分配、零空转 Ticker 与统一配置契约）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Remote/GameRemoteLogOutputDevice.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/UniquePtr.h"

#include "GameLoggingSubsystem.generated.h"

/**
 * @brief 游戏实例级日志子系统：负责捕获设备生命周期托管、按需定时排空无锁队列并广播至解耦消息总线
 */
UCLASS()
class GAMELOGGING_API UGameLoggingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    static constexpr int32 DEFAULT_DRAIN_PER_TICK = 50;

public:
    /**
     * @brief 默认构造函数（保持 OutputDevice 为空，实现 CDO 零堆分配）
     */
    UGameLoggingSubsystem() = default;

    /**
     * @brief 析构函数：兜底确保脱钩输出设备与定时器
     */
    ~UGameLoggingSubsystem() override;

    /**
     * @brief 获取子系统单例引用（断言有效）
     * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
     * @return 子系统引用
     */
    [[nodiscard]] static UGameLoggingSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取子系统指针（可空）
     * @param WorldContextObject 关联的世界上下文对象（支持 UWorld、UGameInstance、UGameInstanceSubsystem 及场景对象）
     * @return 子系统指针，未就绪时返回 nullptr
     */
    [[nodiscard]] static UGameLoggingSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 子系统初始化生命周期回调：声明消息总线依赖、创建捕获设备并在启用时挂载 GLog 与轮询器
     * @param Collection 子系统容器
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统卸载生命周期回调：停止轮询与采集、彻底刷新残留队列并释放底层设备
     */
    void Deinitialize() override;

    /**
     * @brief 开启或关闭远程日志抓取并同步管理 GLog 挂载与主线程 Ticker（禁用态零空转）
     * @param bEnabled 是否启用远程日志抓取
     */
    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void SetRemoteLoggingEnabled(bool bEnabled);

    /**
     * @brief 查询当前是否已启用远程日志抓取
     * @return 已启用返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    bool IsRemoteLoggingEnabled() const noexcept;

    /**
     * @brief 设置远程日志抓取的最低严重程度阈值
     * @param InMinVerbosity 最低严重程度枚举
     */
    void SetMinVerbosity(ELogVerbosity::Type InMinVerbosity) noexcept;

    /**
     * @brief 查询远程日志抓取的最低严重程度阈值
     * @return 详细级别枚举
     */
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    /**
     * @brief 设置每帧由主线程排空并广播的日志条数上限（非正值忽略）
     * @param InMaxDrain 单帧条数上限
     */
    void SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept;

    /**
     * @brief 获取每帧主线程排空上限
     * @return 单帧条数上限
     */
    [[nodiscard]] int32 GetMaxDrainBatchPerTick() const noexcept;

    /**
     * @brief 设置底层队列最大容量上限（钳制至 [1, FGameRemoteLogOutputDevice::MAX_CAPACITY]）
     * @param InCapacity 队列容量上限
     */
    void SetMaxQueueCapacity(int32 InCapacity) noexcept;

    /**
     * @brief 获取当前底层队列最大容量上限
     * @return 队列容量上限
     */
    [[nodiscard]] int32 GetMaxQueueCapacity() const noexcept;

    /**
     * @brief 添加需要忽略采集的特定日志分类
     * @param CategoryName 待忽略的分类名称
     */
    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void AddIgnoredCategory(FName CategoryName);

    /**
     * @brief 移除已忽略的特定日志分类
     * @param CategoryName 待恢复采集的分类名称
     */
    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void RemoveIgnoredCategory(FName CategoryName);

    /**
     * @brief 查询指定日志分类是否处于忽略黑名单中
     * @param CategoryName 待查询的分类名称
     * @return 位于黑名单中返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    bool IsCategoryIgnored(FName CategoryName) const;

    /**
     * @brief 查询当前底层队列中尚未排出的条目数量
     * @return 排队条目数
     */
    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    int32 GetQueuedCount() const noexcept;

    /**
     * @brief 查询因队列满溢而被丢弃的条目累计总数
     * @return 丢弃累计数
     */
    UFUNCTION(BlueprintPure, Category = "GameLogging|Remote")
    int64 GetDroppedCount() const noexcept;

    /**
     * @brief 立即循环排空设备队列并将全部日志条目广播至消息总线（带上限防护防止无限循环）
     * @return 实际排空并处理的条目总数
     */
    int32 FlushQueue();

    /**
     * @brief 清空底层队列并重置丢弃计数器
     */
    UFUNCTION(BlueprintCallable, Category = "GameLogging|Remote")
    void ClearQueue();

private:
    /**
     * @brief 内部批量排空队列并广播至消息总线（空队短路 + 无监听者零分配丢弃 + 复用批次缓冲）
     * @param MaxCount 最大排空条目数量
     * @return 实际排空并处理的条目数量
     */
    int32 DrainAndBroadcast(int32 MaxCount);

    /**
     * @brief 主线程轮询回调，负责批量消费队列并分发至 GameplayMessageSubsystem
     * @param DeltaTime 帧间隔时间（秒）
     * @return 持续保持轮询返回 true
     */
    bool HandleTicker(float DeltaTime);

    /**
     * @brief 启动远程日志捕获：挂载输出设备至 GLog 并注册主线程 Ticker
     */
    void StartCapture();

    /**
     * @brief 停止远程日志捕获：注销主线程 Ticker 并从 GLog 解挂输出设备
     */
    void StopCapture();

private:
    TUniquePtr<FGameRemoteLogOutputDevice> OutputDevice;
    TArray<FGameRemoteLogEntry> ReusableBatch;
    FTSTicker::FDelegateHandle TickerHandle;
    int32 MaxDrainBatchPerTick{DEFAULT_DRAIN_PER_TICK};
    int32 DesiredMaxQueueCapacity{FGameRemoteLogOutputDevice::MAX_CAPACITY};
    ELogVerbosity::Type DesiredMinVerbosity{ELogVerbosity::Warning};
    bool bRemoteLoggingEnabled{true};
    bool bDeviceAttached{false};
    bool bInitialized{false};
};
