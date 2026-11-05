// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Remote/GameRemoteLogOutputDevice.h"
#include "Remote/GameRemoteLogTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "GameLoggingSubsystem.generated.h"

/**
 * @brief 游戏实例级日志子系统：负责捕获设备生命周期挂载、定时排空无锁队列并广播至解耦消息总线
 */
UCLASS()
class GAMELOGGING_API UGameLoggingSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    static constexpr int32 DEFAULT_DRAIN_PER_TICK = 50;

public:
    /**
     * @brief 默认构造函数
     */
    UGameLoggingSubsystem();

    /**
     * @brief 析构函数：保证脱钩输出设备与定时器
     */
    ~UGameLoggingSubsystem() override;

    /**
     * @brief 子系统初始化生命周期回调
     * @param Collection 子系统容器
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统卸载生命周期回调
     */
    void Deinitialize() override;

    /**
     * @brief 判定当前上下文环境是否应当创建该子系统
     * @param Outer 宿主对象
     * @return 允许创建返回 true
     */
    bool ShouldCreateSubsystem(UObject* Outer) const override;

    /**
     * @brief 开启或关闭远程日志抓取并动态挂载/脱钩 GLog
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
     * @param MinVerbosity 最低严重程度枚举
     */
    void SetMinVerbosity(ELogVerbosity::Type MinVerbosity);

    /**
     * @brief 查询远程日志抓取的最低严重程度阈值
     * @return 详细级别枚举
     */
    [[nodiscard]] ELogVerbosity::Type GetMinVerbosity() const noexcept;

    /**
     * @brief 设置每帧由主线程排空并广播的日志条数上限
     * @param InMaxDrain 单帧条数上限
     */
    void SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept;

    /**
     * @brief 获取每帧主线程排空上限
     * @return 单帧条数上限
     */
    [[nodiscard]] int32 GetMaxDrainBatchPerTick() const noexcept;

    /**
     * @brief 立即排空设备队列并将全部日志条目广播至消息总线
     * @return 实际排空并广播的条目总数
     */
    int32 FlushQueue();

    /**
     * @brief 获取底层日志输出设备裸指针（支持高级配置与测试验证）
     * @return 设备裸指针
     */
    [[nodiscard]] FGameRemoteLogOutputDevice* GetOutputDevice() const noexcept;

private:
    /**
     * @brief 内部批量排空队列并将条目广播至解耦消息总线
     * @param MaxCount 最大排空条目数量
     * @return 实际排空并广播的数量
     */
    int32 DrainAndBroadcast(int32 MaxCount);

    /**
     * @brief 主线程轮询回调，负责批量消费队列并分发至 GameplayMessageSubsystem
     * @param DeltaTime 帧间隔时间（秒）
     * @return 持续保持轮询返回 true
     */
    bool HandleTicker(float DeltaTime);

    /**
     * @brief 将输出设备挂载至引擎全局 GLog 重定向器
     */
    void AttachDevice();

    /**
     * @brief 从引擎全局 GLog 重定向器注销输出设备
     */
    void DetachDevice();

private:
    TUniquePtr<FGameRemoteLogOutputDevice> OutputDevice;
    FTSTicker::FDelegateHandle TickerHandle;
    int32 MaxDrainBatchPerTick{DEFAULT_DRAIN_PER_TICK};
    bool bRemoteLoggingEnabled{true};
    bool bDeviceAttached{false};
};

