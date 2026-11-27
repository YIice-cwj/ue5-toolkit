// Copyright Epic Games, Inc. All Rights Reserved.

#include "Time/TimeDilationSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Logging/GameLogCategories.h"
#include "System/GameFeedbackInternalHelpers.h"

/**
 * @brief 构造函数：顿帧表与膨胀混音器由成员默认构造
 */
UTimeDilationSubsystem::UTimeDilationSubsystem() = default;

/**
 * @brief 获取世界子系统引用（断言有效）
 * @param WorldContextObject 上下文对象
 * @return 子系统引用
 */
UTimeDilationSubsystem& UTimeDilationSubsystem::Get(const UObject* WorldContextObject) {
    UTimeDilationSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取世界子系统指针（静默返回空）
 * @param WorldContextObject 上下文对象
 * @return 子系统指针
 */
UTimeDilationSubsystem* UTimeDilationSubsystem::GetPtr(const UObject* WorldContextObject) {
    const UWorld* World = GameFeedback::Internal::GetWorldFromContextSafe(WorldContextObject);
    return World ? World->GetSubsystem<UTimeDilationSubsystem>() : nullptr;
}

/**
 * @brief 仅在游戏世界创建（排除编辑器视口与预览世界）
 * @param Outer 外层世界对象
 * @return 允许创建返回 true
 */
bool UTimeDilationSubsystem::ShouldCreateSubsystem(UObject* Outer) const {
    if (!Super::ShouldCreateSubsystem(Outer)) {
        return false;
    }
    const UWorld* World = Outer ? Outer->GetWorld() : nullptr;
    return World && World->IsGameWorld();
}

/**
 * @brief 子系统初始化生命周期回调：初始化真实时间基准与世界膨胀缓存
 * @param Collection 子系统管理器集合
 */
void UTimeDilationSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);
    LastRealTime = FPlatformTime::Seconds();
    LastAppliedWorldDilation = 1.0f;
    GAME_LOG(Verbose, TEXT("[TimeDilation] 时间膨胀子系统初始化就绪。"));
}

/**
 * @brief 子系统销毁生命周期回调：清理全部顿帧记录与全局时间膨胀通道
 */
void UTimeDilationSubsystem::Deinitialize() {
    GAME_LOG(Verbose, TEXT("[TimeDilation] 子系统注销，清理所有定格与通道膨胀。"));
    ClearAllHitStops();
    ClearAllGlobalDilations();
    Super::Deinitialize();
}

/**
 * @brief 每帧推进时间层叠（暂停期间仍刷新 LastRealTime，避免解除暂停时时间突跳）
 * @param DeltaTime 帧间隔时间（秒）
 */
void UTimeDilationSubsystem::Tick(float DeltaTime) {
    Super::Tick(DeltaTime);

    const double CurrentRealTime = FPlatformTime::Seconds();
    float RealDeltaSeconds = (LastRealTime > 0.0) ? static_cast<float>(CurrentRealTime - LastRealTime) : DeltaTime;
    LastRealTime = CurrentRealTime;

    if (const UWorld* World = GetWorld(); World && World->IsPaused()) {
        return;
    }

    RealDeltaSeconds = FMath::Clamp(RealDeltaSeconds, 0.0f, 1.0f);
    AdvanceTime(RealDeltaSeconds);
}

/**
 * @brief Tickable 统计 ID（STATGROUP_Tickables）
 * @return 统计 ID
 */
TStatId UTimeDilationSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UTimeDilationSubsystem, STATGROUP_Tickables);
}

/**
 * @brief 仅当存在活跃顿帧或全局时间膨胀通道且非 CDO 时参与引擎 Tick，空闲期零开销休眠
 * @return 需要 Tick 返回 true
 */
bool UTimeDilationSubsystem::IsTickable() const {
    return !HasAnyFlags(RF_ClassDefaultObject) && (HitStopTable.HasActive() || DilationMixer.HasActive());
}

/**
 * @brief 活跃状态下暂停期间仍参与 Tick 以刷新真实时间基准，避免解除暂停后时间突跳
 * @return 恒为 true
 */
bool UTimeDilationSubsystem::IsTickableWhenPaused() const {
    return true;
}

/**
 * @brief 请求对单个 Actor 施加打击顿帧（Duration 非正数/NaN 或 Target 无效时不生效）
 * @param Target 目标 Actor
 * @param Duration 顿帧持续真实时间（秒）
 * @param TimeDilation 顿帧期间局部时间缩放（钳制到 [0.0001, 20]，默认 0.001 即几乎定格）
 */
void UTimeDilationSubsystem::RequestHitStop(AActor* Target, float Duration, float TimeDilation) {
    if (!IsValid(Target) || !(Duration > 0.0f)) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[TimeDilation] 请求定格失败：目标无效或时长非正。"));
        return;
    }

    RefreshClockIfIdle();
    HitStopTable.Request(*Target, Duration, TimeDilation);

    GAME_LOG_THROTTLE(0.5, Verbose, TEXT("[TimeDilation] 请求定格成功: 目标=[%s], 时长=%.3f 秒, 膨胀系数=%.4f"),
        *GetNameSafe(Target), Duration, Target->CustomTimeDilation);
}

/**
 * @brief 清除指定 Actor 的打击顿帧并立即还原时间缩放
 * @param Target 目标 Actor
 */
void UTimeDilationSubsystem::ClearHitStop(AActor* Target) {
    if (!Target) {
        return;
    }

    HitStopTable.Clear(Target);

    GAME_LOG_THROTTLE(0.5, Verbose, TEXT("[TimeDilation] 恢复定格目标: [%s], 恢复膨胀系数=%.4f"),
        *GetNameSafe(Target), Target->CustomTimeDilation);
}

/**
 * @brief 检查指定 Actor 当前是否正处于打击顿帧中
 * @param Target 目标 Actor
 * @return 处于顿帧中返回 true
 */
bool UTimeDilationSubsystem::IsActorInHitStop(const AActor* Target) const {
    return HitStopTable.IsActive(Target);
}

/**
 * @brief 清除所有 Actor 的打击顿帧
 */
void UTimeDilationSubsystem::ClearAllHitStops() {
    HitStopTable.ClearAll();
}

/**
 * @brief 在指定通道请求全局时间膨胀（重复请求整体覆盖该通道既有状态）
 * @param Channel 目标通道
 * @param Duration 持续时间（秒，<= 0 表示持久有效直至手动清除）
 * @param TimeDilation 目标时间缩放（钳制到 [0.0001, 20]）
 * @param BlendOutDuration 结束时的平滑淡出时间（秒，负数按 0 处理）
 */
void UTimeDilationSubsystem::RequestGlobalDilation(ETimeDilationChannel Channel, float Duration, float TimeDilation, float BlendOutDuration) {
    RefreshClockIfIdle();
    DilationMixer.Request(Channel, Duration, TimeDilation, BlendOutDuration);
    SyncWorldDilation();

    GAME_LOG_THROTTLE(0.5, Verbose, TEXT("[TimeDilation] 设置全局通道膨胀: 通道=%d, 系数=%.4f, 时长=%.3f 秒, 淡出时长=%.3f 秒"),
        static_cast<int32>(Channel), DilationMixer.GetChannelDilation(Channel), Duration, BlendOutDuration);
}

/**
 * @brief 清除指定通道的全局时间膨胀
 * @param Channel 目标通道
 */
void UTimeDilationSubsystem::ClearGlobalDilation(ETimeDilationChannel Channel) {
    if (!DilationMixer.IsActive(Channel)) {
        return;
    }

    DilationMixer.Clear(Channel);
    SyncWorldDilation();

    GAME_LOG_THROTTLE(0.5, Verbose, TEXT("[TimeDilation] 清除全局通道膨胀: 通道=%d"), static_cast<int32>(Channel));
}

/**
 * @brief 清除指定通道的全局时间膨胀，并支持平滑淡出恢复
 * @param Channel 目标通道
 * @param BlendOutDuration 淡出过渡时间（秒，<= 0 时等价于立即清除）
 */
void UTimeDilationSubsystem::ClearGlobalDilationWithBlend(ETimeDilationChannel Channel, float BlendOutDuration) {
    if (!DilationMixer.IsActive(Channel)) {
        return;
    }

    if (BlendOutDuration <= 0.0f) {
        ClearGlobalDilation(Channel);
        return;
    }

    DilationMixer.ClearWithBlend(Channel, BlendOutDuration);
    SyncWorldDilation();
}

/**
 * @brief 清除所有通道的全局时间膨胀
 */
void UTimeDilationSubsystem::ClearAllGlobalDilations() {
    DilationMixer.ClearAll();
    SyncWorldDilation();
}

/**
 * @brief 获取当前生效的最终全局时间缩放值
 * @return 最终全局时间缩放
 */
float UTimeDilationSubsystem::GetEffectiveGlobalTimeDilation() const {
    return DilationMixer.GetEffectiveDilation();
}

/**
 * @brief 获取指定通道当前的活跃状态
 * @param Channel 目标通道
 * @return 通道是否处于激活状态
 */
bool UTimeDilationSubsystem::IsChannelActive(ETimeDilationChannel Channel) const {
    return DilationMixer.IsActive(Channel);
}

/**
 * @brief 获取指定通道当前的时间缩放值
 * @param Channel 目标通道
 * @return 通道的时间缩放值（未激活返回 1.0f）
 */
float UTimeDilationSubsystem::GetChannelTimeDilation(ETimeDilationChannel Channel) const {
    return DilationMixer.GetChannelDilation(Channel);
}

/**
 * @brief 手动推进内部真实时间步长（用于单测或自定义时钟驱动）
 * @param RealDeltaSeconds 真实流逝秒数
 */
void UTimeDilationSubsystem::AdvanceTime(float RealDeltaSeconds) {
    if (!(RealDeltaSeconds > 0.0f)) {
        return;
    }

    DilationMixer.Advance(RealDeltaSeconds);
    HitStopTable.Advance(RealDeltaSeconds);
    SyncWorldDilation();
}

/**
 * @brief 若当前处于空闲休眠态，则在激活新任务前同步刷新真实时间基准
 */
void UTimeDilationSubsystem::RefreshClockIfIdle() {
    if (!HitStopTable.HasActive() && !DilationMixer.HasActive()) {
        LastRealTime = FPlatformTime::Seconds();
    }
}

/**
 * @brief 将混音器生效时间缩放写回世界设置（生效值未变化时直接返回）
 */
void UTimeDilationSubsystem::SyncWorldDilation() {
    const float EffectiveDilation = DilationMixer.GetEffectiveDilation();
    if (FMath::IsNearlyEqual(LastAppliedWorldDilation, EffectiveDilation)) {
        return;
    }

    if (UWorld* World = GetWorld()) {
        if (AWorldSettings* WorldSettings = World->GetWorldSettings()) {
            WorldSettings->SetTimeDilation(EffectiveDilation);
            LastAppliedWorldDilation = EffectiveDilation;
        }
    }
}
