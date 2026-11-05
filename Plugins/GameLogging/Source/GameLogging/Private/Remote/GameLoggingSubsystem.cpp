// Copyright Epic Games, Inc. All Rights Reserved.

#include "Remote/GameLoggingSubsystem.h"

#include "Message/GameplayMessageSubsystem.h"

/**
 * @brief 构造函数实现
 */
UGameLoggingSubsystem::UGameLoggingSubsystem()
    : OutputDevice(MakeUnique<FGameRemoteLogOutputDevice>()) {
}

/**
 * @brief 析构函数实现：解绑设备并清理定时器
 */
UGameLoggingSubsystem::~UGameLoggingSubsystem() {
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }
    DetachDevice();
}

/**
 * @brief 子系统初始化实现：挂载定时器并在启用时附着 GLog
 * @param Collection 子系统容器
 */
void UGameLoggingSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);

    if (!OutputDevice.IsValid()) {
        OutputDevice = MakeUnique<FGameRemoteLogOutputDevice>();
    }

    TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UGameLoggingSubsystem::HandleTicker),
        0.0f
    );

    if (bRemoteLoggingEnabled) {
        AttachDevice();
    }
}

/**
 * @brief 子系统卸载实现：脱钩输出设备并最后清空队列
 */
void UGameLoggingSubsystem::Deinitialize() {
    if (TickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
        TickerHandle.Reset();
    }

    DetachDevice();
    FlushQueue();
    OutputDevice.Reset();

    Super::Deinitialize();
}

/**
 * @brief 判定是否创建子系统实现
 * @param Outer 宿主对象
 * @return 恒为 true
 */
bool UGameLoggingSubsystem::ShouldCreateSubsystem(UObject* Outer) const {
    return true;
}

/**
 * @brief 设置启用状态并动态同步 GLog 挂载状态实现
 * @param bEnabled 是否启用
 */
void UGameLoggingSubsystem::SetRemoteLoggingEnabled(bool bEnabled) {
    if (bRemoteLoggingEnabled == bEnabled) {
        return;
    }

    bRemoteLoggingEnabled = bEnabled;
    if (bRemoteLoggingEnabled) {
        AttachDevice();
    } else {
        DetachDevice();
    }
}

/**
 * @brief 查询当前启用状态实现
 * @return 是否启用
 */
bool UGameLoggingSubsystem::IsRemoteLoggingEnabled() const noexcept {
    return bRemoteLoggingEnabled;
}

/**
 * @brief 设置最低抓取严重级别实现
 * @param MinVerbosity 严重级别枚举
 */
void UGameLoggingSubsystem::SetMinVerbosity(ELogVerbosity::Type MinVerbosity) {
    if (OutputDevice.IsValid()) {
        OutputDevice->SetMinVerbosity(MinVerbosity);
    }
}

/**
 * @brief 查询最低抓取严重级别实现
 * @return 严重级别枚举
 */
ELogVerbosity::Type UGameLoggingSubsystem::GetMinVerbosity() const noexcept {
    if (OutputDevice.IsValid()) {
        return OutputDevice->GetMinVerbosity();
    }
    return ELogVerbosity::Warning;
}

/**
 * @brief 设置每帧最大排空条数实现
 * @param InMaxDrain 单帧上限
 */
void UGameLoggingSubsystem::SetMaxDrainBatchPerTick(int32 InMaxDrain) noexcept {
    if (InMaxDrain > 0) {
        MaxDrainBatchPerTick = InMaxDrain;
    }
}

/**
 * @brief 获取每帧最大排空条数实现
 * @return 单帧上限
 */
int32 UGameLoggingSubsystem::GetMaxDrainBatchPerTick() const noexcept {
    return MaxDrainBatchPerTick;
}

/**
 * @brief 立即排空队列并广播实现
 * @return 排出并广播的条数
 */
int32 UGameLoggingSubsystem::FlushQueue() {
    if (!OutputDevice.IsValid()) {
        return 0;
    }
    return DrainAndBroadcast(OutputDevice->GetQueuedCount());
}

/**
 * @brief 获取底层输出设备裸指针实现
 * @return 设备指针
 */
FGameRemoteLogOutputDevice* UGameLoggingSubsystem::GetOutputDevice() const noexcept {
    return OutputDevice.Get();
}

/**
 * @brief 内部批量排空队列并广播至消息总线实现
 * @param MaxCount 最大排空数量
 * @return 实际排空并广播的条目数量
 */
int32 UGameLoggingSubsystem::DrainAndBroadcast(int32 MaxCount) {
    if (!OutputDevice.IsValid() || MaxCount <= 0) {
        return 0;
    }

    TArray<FGameRemoteLogEntry> Batch;
    const int32 Drained = OutputDevice->DrainBatch(Batch, MaxCount);
    if (Drained <= 0) {
        return 0;
    }

    UGameplayMessageSubsystem* MessageBus = UGameplayMessageSubsystem::GetPtr(this);
    if (MessageBus == nullptr) {
        return Drained;
    }

    for (const FGameRemoteLogEntry& Entry : Batch) {
        MessageBus->BroadcastMessage(TAG_Game_Log_Remote, Entry);
    }

    return Drained;
}

/**
 * @brief 主线程定时器回调排空队列并广播实现
 * @param DeltaTime 帧时间
 * @return 恒为 true
 */
bool UGameLoggingSubsystem::HandleTicker([[maybe_unused]] float DeltaTime) {
    if (bRemoteLoggingEnabled) {
        DrainAndBroadcast(MaxDrainBatchPerTick);
    }
    return true;
}

/**
 * @brief 挂载设备至全局 GLog 实现
 */
void UGameLoggingSubsystem::AttachDevice() {
    if (GLog != nullptr && OutputDevice.IsValid() && !bDeviceAttached) {
        GLog->AddOutputDevice(OutputDevice.Get());
        bDeviceAttached = true;
    }
}

/**
 * @brief 从全局 GLog 注销设备实现
 */
void UGameLoggingSubsystem::DetachDevice() {
    if (GLog != nullptr && OutputDevice.IsValid() && bDeviceAttached) {
        GLog->RemoveOutputDevice(OutputDevice.Get());
        bDeviceAttached = false;
    }
}

