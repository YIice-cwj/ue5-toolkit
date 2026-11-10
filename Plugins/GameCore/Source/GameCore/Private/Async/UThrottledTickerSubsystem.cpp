/**
 * @file UThrottledTickerSubsystem.cpp
 * @brief 分级交错节流心跳调度子系统与句柄薄壳实现
 */

#include "Async/UThrottledTickerSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

/**
 * @brief 构造绑定节流子系统与句柄 ID 的节流心跳句柄
 * @param InSubsystem 所属节流子系统指针
 * @param InHandleId 分配的句柄 ID
 */
FThrottledTickHandle::FThrottledTickHandle(UThrottledTickerSubsystem* InSubsystem, uint64 InHandleId) noexcept
    : SubsystemWeak(InSubsystem)
    , HandleId(InHandleId) {}

/**
 * @brief 从所属节流子系统中注销当前心跳任务并重置句柄 ID
 * @return 注销成功返回 true
 */
bool FThrottledTickHandle::Unregister() {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        const uint64 OldId = HandleId;
        HandleId = 0ULL;
        return Subsystem->GetScheduler().UnregisterTick(OldId);
    }
    HandleId = 0ULL;
    return false;
}

/**
 * @brief 动态调整当前句柄绑定的预设节流频率档位
 * @param NewRate 新预设频率档位
 * @return 调整成功返回 true
 */
bool FThrottledTickHandle::SetRate(EThrottledRate NewRate) {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        return Subsystem->GetScheduler().SetTickInterval(
            HandleId,
            FThrottledTickScheduler::RateToIntervalSeconds(NewRate)
        );
    }
    return false;
}

/**
 * @brief 动态调整当前句柄绑定的自定义秒级心跳间隔
 * @param NewIntervalSeconds 新间隔秒数
 * @return 调整成功返回 true
 */
bool FThrottledTickHandle::SetInterval(float NewIntervalSeconds) {
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        return Subsystem->GetScheduler().SetTickInterval(HandleId, NewIntervalSeconds);
    }
    return false;
}

/**
 * @brief 查询当前句柄对应的心跳任务是否仍处于活跃注册态
 * @return 活跃返回 true
 */
bool FThrottledTickHandle::IsActive() const {
    if (const UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        return Subsystem->GetScheduler().IsTickActive(HandleId);
    }
    return false;
}

/**
 * @brief 世界注销生命周期回调：清空全部注册的心跳任务
 */
void UThrottledTickerSubsystem::Deinitialize() {
    ClearAll();
    Super::Deinitialize();
}

/**
 * @brief 每帧推进各个节流桶的心跳任务，自动剔除失效宿主与待注销项
 * @param DeltaTime 帧流逝时间
 */
void UThrottledTickerSubsystem::Tick(float DeltaTime) {
    Scheduler.Tick(DeltaTime);
}

/**
 * @brief 统计性能组标识
 * @return Tickable 统计 ID
 */
TStatId UThrottledTickerSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UThrottledTickerSubsystem, STATGROUP_Tickables);
}

/**
 * @brief 仅当存在活跃注册项且非类默认对象时参与引擎 Tick
 * @return 需要 Tick 返回 true
 */
bool UThrottledTickerSubsystem::IsTickable() const {
    return !HasAnyFlags(RF_ClassDefaultObject) && !Scheduler.IsEmpty();
}

/**
 * @brief 注册预设频率分级节流心跳（自动分相交错平摊调用峰值）
 * @param Owner 宿主对象弱引用（宿主销毁时自动解挂注销）
 * @param Rate 预设节流频率档位
 * @param Callback 心跳回调闭包（入参为自上次触发以来的实际累积流逝时间）
 * @return 可操作句柄
 */
FThrottledTickHandle UThrottledTickerSubsystem::RegisterTick(
    TWeakObjectPtr<UObject> Owner,
    EThrottledRate Rate,
    FThrottledTickCallback Callback) {
    return RegisterIntervalTick(
        MoveTemp(Owner),
        FThrottledTickScheduler::RateToIntervalSeconds(Rate),
        MoveTemp(Callback)
    );
}

/**
 * @brief 注册自定义秒级间隔节流心跳
 * @param Owner 宿主对象弱引用
 * @param IntervalSeconds 间隔秒数（小于等于 0 视为每帧全速更新）
 * @param Callback 心跳回调闭包
 * @return 可操作句柄
 */
FThrottledTickHandle UThrottledTickerSubsystem::RegisterIntervalTick(
    TWeakObjectPtr<UObject> Owner,
    float IntervalSeconds,
    FThrottledTickCallback Callback) {
    const uint64 AllocatedId = Scheduler.RegisterIntervalTick(MoveTemp(Owner), IntervalSeconds, MoveTemp(Callback));
    return (AllocatedId != 0ULL) ? FThrottledTickHandle(this, AllocatedId) : FThrottledTickHandle();
}

/**
 * @brief 查询当前已注册的心跳任务总数
 * @return 任务总数
 */
int32 UThrottledTickerSubsystem::GetRegisteredCount() const {
    return Scheduler.GetRegisteredCount();
}

/**
 * @brief 清空全部已注册的心跳任务
 */
void UThrottledTickerSubsystem::ClearAll() {
    Scheduler.ClearAll();
}

/**
 * @brief 获取世界上下文绑定的节流心跳子系统单例
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例引用
 */
UThrottledTickerSubsystem& UThrottledTickerSubsystem::Get(const UObject* WorldContextObject) {
    check(WorldContextObject != nullptr);
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::Assert);
    check(World != nullptr);
    UThrottledTickerSubsystem* Subsystem = World->GetSubsystem<UThrottledTickerSubsystem>();
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 安全获取世界上下文绑定的节流心跳子系统指针（空指针保护）
 * @param WorldContextObject 世界上下文对象
 * @return 子系统实例指针，获取失败返回 nullptr
 */
UThrottledTickerSubsystem* UThrottledTickerSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr || GEngine == nullptr) {
        return nullptr;
    }
    if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
        return World->GetSubsystem<UThrottledTickerSubsystem>();
    }
    return nullptr;
}
