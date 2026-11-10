/**
 * @file FSpatialThrottleDriver.cpp
 * @brief 空间视锥与重要度心跳节流驱动器实现
 */

#include "Spatial/FSpatialThrottleDriver.h"

#include "Async/UThrottledTickerSubsystem.h"

/**
 * @brief 构造函数：绑定纯 C++ 节流调度器实例
 * @param InScheduler 调度器引用
 */
FSpatialThrottleDriver::FSpatialThrottleDriver(FThrottledTickScheduler& InScheduler)
    : DirectScheduler(&InScheduler)
    , bBoundToSubsystem(false) {
}

/**
 * @brief 构造函数：绑定世界节流子系统（通过弱引用安全跟踪子系统生命周期，防子系统先行销毁悬垂）
 * @param InSubsystem 节流子系统引用
 */
FSpatialThrottleDriver::FSpatialThrottleDriver(UThrottledTickerSubsystem& InSubsystem)
    : DirectScheduler(nullptr)
    , SubsystemWeak(&InSubsystem)
    , bBoundToSubsystem(true) {
}

/**
 * @brief 析构函数：注销所有在调度器中注册的心跳
 */
FSpatialThrottleDriver::~FSpatialThrottleDriver() {
    Clear();
}

/**
 * @brief 注册空间实体心跳任务（初始以 0.0f 满帧间隔注册以保证生成首帧立即响应，首次调用 UpdateSpatialThrottle 后按空间距离梯级切换实际间隔）
 * @param InValidator 实体有效性校验器
 * @param InPositionGetter 实体世界坐标获取器
 * @param InCallback 实体 Tick 回调
 * @param bInHighSignificance 是否为高重要度实体
 * @return 驱动器句柄 ID
 */
uint64 FSpatialThrottleDriver::RegisterSpatialEntity(
    FOwnerValidator InValidator,
    FPositionGetter InPositionGetter,
    FThrottledTickCallback InCallback,
    bool bInHighSignificance
) {
    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    if (ActiveScheduler == nullptr || !InPositionGetter || !InCallback) {
        return 0ULL;
    }

    const ESpatialDistanceTier InitialTier =
        bInHighSignificance ? ESpatialDistanceTier::Near : ESpatialDistanceTier::OutOfRange;
    const uint64 SchedHandle = ActiveScheduler->RegisterIntervalTick(InValidator, 0.0f, InCallback);
    if (SchedHandle == 0ULL) {
        return 0ULL;
    }

    const uint64 DriverHandle = NextHandleSeed++;
    FSpatialEntity Entry;
    Entry.DriverHandle = DriverHandle;
    Entry.SchedulerHandle = SchedHandle;
    Entry.Validator = MoveTemp(InValidator);
    Entry.PositionGetter = MoveTemp(InPositionGetter);
    Entry.CurrentInterval = 0.0f;
    Entry.CurrentTier = InitialTier;
    Entry.bHighSignificance = bInHighSignificance;
    Entry.bPendingRemoval = false;

    Entities.Add(MoveTemp(Entry));
    ++ActiveEntityCount;
    return DriverHandle;
}

/**
 * @brief 注销指定空间实体（支持在 UpdateSpatialThrottle 回调中安全重入注销）
 * @param InEntityHandle 实体句柄
 * @return 成功注销返回 true
 */
bool FSpatialThrottleDriver::UnregisterSpatialEntity(uint64 InEntityHandle) {
    if (InEntityHandle == 0ULL) {
        return false;
    }

    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    for (int32 Index = 0; Index < Entities.Num(); ++Index) {
        if (Entities[Index].DriverHandle == InEntityHandle && !Entities[Index].bPendingRemoval) {
            if (ActiveScheduler != nullptr) {
                ActiveScheduler->UnregisterTick(Entities[Index].SchedulerHandle);
            }
            --ActiveEntityCount;
            if (bIsUpdating) {
                Entities[Index].bPendingRemoval = true;
            } else {
                Entities.RemoveAtSwap(Index);
            }
            return true;
        }
    }

    return false;
}

/**
 * @brief 动态调整实体重要度标记
 * @param InEntityHandle 实体句柄
 * @param bInHighSignificance 是否高优保真
 * @return 找到并修改返回 true
 */
bool FSpatialThrottleDriver::SetEntitySignificance(uint64 InEntityHandle, bool bInHighSignificance) {
    if (InEntityHandle == 0ULL) {
        return false;
    }

    for (FSpatialEntity& Entry : Entities) {
        if (Entry.DriverHandle == InEntityHandle && !Entry.bPendingRemoval) {
            Entry.bHighSignificance = bInHighSignificance;
            return true;
        }
    }

    return false;
}

/**
 * @brief 依观察者上下文集中批量评估并下发新节流周期（含视锥角度与距离双向迟滞带保护及回调重入扩容/注销防护）
 * @param ViewContext 空间视锥与观察者上下文
 */
void FSpatialThrottleDriver::UpdateSpatialThrottle(const FSpatialViewContext& ViewContext) {
    if (bIsUpdating) {
        return;
    }

    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    if (ActiveScheduler == nullptr) {
        Entities.Reset();
        ActiveEntityCount = 0;
        return;
    }

    bIsUpdating = true;

    const float BaseHalfFovDeg = FMath::Clamp(ViewContext.FOVDegrees * 0.5f, 0.0f, 180.0f);
    const float ExpandedHalfFovDeg = FMath::Clamp(BaseHalfFovDeg + FMath::Max(0.0f, Config.HysteresisDegrees), 0.0f, 180.0f);
    const float CosEnterFov = FMath::Cos(FMath::DegreesToRadians(BaseHalfFovDeg));
    const float CosKeepFov = FMath::Cos(FMath::DegreesToRadians(ExpandedHalfFovDeg));
    const float DistMargin = FMath::Max(0.0f, Config.DistanceHysteresis);
    const FVector SafeViewForward = ViewContext.ViewForward.GetSafeNormal();

    const int32 InitialCount = Entities.Num();
    for (int32 Index = 0; Index < InitialCount; ++Index) {
        if (Entities[Index].bPendingRemoval) {
            continue;
        }

        FOwnerValidator LocalValidator = Entities[Index].Validator;
        if (LocalValidator && !LocalValidator()) {
            if (!Entities[Index].bPendingRemoval) {
                ActiveScheduler->UnregisterTick(Entities[Index].SchedulerHandle);
                Entities[Index].bPendingRemoval = true;
                --ActiveEntityCount;
            }
            continue;
        }

        if (Entities[Index].bPendingRemoval) {
            continue;
        }

        FPositionGetter LocalPositionGetter = Entities[Index].PositionGetter;
        const FVector EntityLocation = LocalPositionGetter();
        if (Entities[Index].bPendingRemoval) {
            continue;
        }

        FSpatialEntity& Entry = Entities[Index];
        const FVector ToEntity = EntityLocation - ViewContext.ViewLocation;
        const float Distance = ToEntity.Size();

        ESpatialDistanceTier TargetTier = ESpatialDistanceTier::OutOfRange;
        float TargetInterval = Config.OutOfRangeOrBehindInterval;

        if (Entry.bHighSignificance || Distance <= KINDA_SMALL_NUMBER) {
            TargetTier = ESpatialDistanceTier::Near;
            TargetInterval = Config.NearInterval;
        } else {
            const float MaxDistThreshold = (Entry.CurrentTier == ESpatialDistanceTier::OutOfRange)
                ? ViewContext.MaxDistance
                : (ViewContext.MaxDistance + DistMargin);

            if (Distance <= MaxDistThreshold) {
                const FVector ToEntityDir = ToEntity / Distance;
                const float DotProduct = FVector::DotProduct(ToEntityDir, SafeViewForward);
                const float CosThreshold = (Entry.CurrentTier == ESpatialDistanceTier::OutOfRange) ? CosEnterFov : CosKeepFov;

                if (DotProduct >= CosThreshold) {
                    const float NearThreshold = (Entry.CurrentTier == ESpatialDistanceTier::Near)
                        ? (Config.DistanceNear + DistMargin)
                        : ((Entry.CurrentTier == ESpatialDistanceTier::Mid)
                            ? FMath::Max(0.0f, Config.DistanceNear - DistMargin)
                            : Config.DistanceNear);

                    const float MidThreshold = (Entry.CurrentTier == ESpatialDistanceTier::Mid)
                        ? (Config.DistanceMid + DistMargin)
                        : ((Entry.CurrentTier == ESpatialDistanceTier::Far)
                            ? FMath::Max(NearThreshold, Config.DistanceMid - DistMargin)
                            : Config.DistanceMid);

                    if (Distance <= NearThreshold) {
                        TargetTier = ESpatialDistanceTier::Near;
                        TargetInterval = Config.NearInterval;
                    } else if (Distance <= MidThreshold) {
                        TargetTier = ESpatialDistanceTier::Mid;
                        TargetInterval = Config.MidInterval;
                    } else {
                        TargetTier = ESpatialDistanceTier::Far;
                        TargetInterval = Config.FarInterval;
                    }
                }
            }
        }

        Entry.CurrentTier = TargetTier;
        if (FMath::Abs(TargetInterval - Entry.CurrentInterval) > KINDA_SMALL_NUMBER) {
            ActiveScheduler->SetTickInterval(Entry.SchedulerHandle, TargetInterval);
            Entry.CurrentInterval = TargetInterval;
        }
    }

    for (int32 Index = Entities.Num() - 1; Index >= 0; --Index) {
        if (Entities[Index].bPendingRemoval) {
            Entities.RemoveAtSwap(Index);
        }
    }

    bIsUpdating = false;
}

/**
 * @brief 设置距离梯级与节流配置
 * @param InConfig 新配置参数
 */
void FSpatialThrottleDriver::SetTierConfig(const FSpatialThrottleTierConfig& InConfig) {
    Config = InConfig;
}

/**
 * @brief 获取当前距离梯级配置
 * @return 配置常量引用
 */
const FSpatialThrottleTierConfig& FSpatialThrottleDriver::GetTierConfig() const noexcept {
    return Config;
}

/**
 * @brief 获取当前注册的活跃实体总数
 * @return 活跃实体数量
 */
int32 FSpatialThrottleDriver::Num() const noexcept {
    return ActiveEntityCount;
}

/**
 * @brief 清空并注销所有实体（支持在 UpdateSpatialThrottle 回调中安全重入清空）
 */
void FSpatialThrottleDriver::Clear() {
    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    for (FSpatialEntity& Entry : Entities) {
        if (!Entry.bPendingRemoval) {
            if (ActiveScheduler != nullptr) {
                ActiveScheduler->UnregisterTick(Entry.SchedulerHandle);
            }
            Entry.bPendingRemoval = true;
        }
    }
    ActiveEntityCount = 0;
    if (!bIsUpdating) {
        Entities.Reset();
    }
}

/**
 * @brief 解析当前活跃的底层节流调度器指针（若绑定的子系统已销毁则返回 nullptr）
 * @return 活跃调度器指针
 */
FThrottledTickScheduler* FSpatialThrottleDriver::GetActiveScheduler() const noexcept {
    if (!bBoundToSubsystem) {
        return DirectScheduler;
    }
    if (UThrottledTickerSubsystem* Subsystem = SubsystemWeak.Get()) {
        return &Subsystem->GetScheduler();
    }
    return nullptr;
}
