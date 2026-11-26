/**
 * @file FSpatialThrottleDriver.cpp
 * @brief 空间视锥与实体重要度心跳节流驱动器实现
 */

#include "Throttle/FSpatialThrottleDriver.h"

#include "Async/UThrottledTickerSubsystem.h"
#include "Core/SpatialQueryCommon.h"
#include "Logging/GameLogCategories.h"

/**
 * @brief 构造函数：绑定纯 C++ 节流调度器实例
 * @param InScheduler 调度器引用
 */
FSpatialThrottleDriver::FSpatialThrottleDriver(FThrottledTickScheduler& InScheduler)
    : DirectScheduler(&InScheduler)
    , bBoundToSubsystem(false) {
}

/**
 * @brief 构造函数：绑定世界节流子系统（通过弱引用安全跟踪子系统生命周期，防先行销毁悬垂）
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
 * @brief 注册空间实体心跳任务（初始以 0.0f 满帧间隔注册，更新期间重入安全暂存至 PendingAdditions）
 * @param InValidator 实体有效性校验器
 * @param InPositionGetter 实体世界坐标获取器
 * @param InCallback 实体 Tick 回调
 * @param bInHighSignificance 是否为高重要度实体
 * @return 驱动器句柄 ID（0 代表注册失败）
 */
uint64 FSpatialThrottleDriver::RegisterSpatialEntity(
    FOwnerValidator InValidator,
    FPositionGetter InPositionGetter,
    FThrottledTickCallback InCallback,
    bool bInHighSignificance
) {
    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    if (ActiveScheduler == nullptr || !InPositionGetter || !InCallback) {
        GAME_LOG(Verbose, TEXT("[ThrottleDriver] 拒绝注册空间实体：调度器无效或位置/心跳回调为空。"));
        return 0ULL;
    }

    const uint64 SchedHandle = ActiveScheduler->RegisterIntervalTick(InValidator, 0.0f, MoveTemp(InCallback));
    if (SchedHandle == 0ULL) {
        GAME_LOG(Verbose, TEXT("[ThrottleDriver] 底层 FThrottledTickScheduler 拒绝注册心跳（校验器初始即失效）。"));
        return 0ULL;
    }

    const uint64 DriverHandle = NextHandleSeed++;
    FSpatialEntity Entry;
    Entry.DriverHandle = DriverHandle;
    Entry.SchedulerHandle = SchedHandle;
    Entry.Validator = MoveTemp(InValidator);
    Entry.PositionGetter = MoveTemp(InPositionGetter);
    Entry.CurrentInterval = 0.0f;
    Entry.CurrentTier = bInHighSignificance ? ESpatialDistanceTier::Near : ESpatialDistanceTier::OutOfRange;
    Entry.bHighSignificance = bInHighSignificance;
    Entry.bPendingRemoval = false;

    if (bIsUpdating) {
        PendingAdditions.Add(MoveTemp(Entry));
    } else {
        const int32 NewIndex = Entities.Add(MoveTemp(Entry));
        HandleToIndex.Add(DriverHandle, NewIndex);
    }

    ++ActiveEntityCount;
    return DriverHandle;
}

/**
 * @brief O(1) 注销指定空间实体（支持在 UpdateSpatialThrottle 回调中安全重入注销）
 * @param InEntityHandle 实体句柄
 * @return 成功注销返回 true
 */
bool FSpatialThrottleDriver::UnregisterSpatialEntity(uint64 InEntityHandle) {
    if (InEntityHandle == 0ULL) {
        return false;
    }

    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    if (const int32* FoundIndex = HandleToIndex.Find(InEntityHandle)) {
        const int32 Index = *FoundIndex;
        FSpatialEntity& Entry = Entities[Index];
        if (ActiveScheduler != nullptr) {
            ActiveScheduler->UnregisterTick(Entry.SchedulerHandle);
        }
        --ActiveEntityCount;

        if (bIsUpdating) {
            Entry.bPendingRemoval = true;
            HandleToIndex.Remove(InEntityHandle);
        } else {
            RemoveEntityAtImmediate(Index);
        }
        return true;
    }

    if (!bIsUpdating) {
        return false;
    }

    for (int32 PendingIndex = 0; PendingIndex < PendingAdditions.Num(); ++PendingIndex) {
        if (PendingAdditions[PendingIndex].DriverHandle == InEntityHandle) {
            if (ActiveScheduler != nullptr) {
                ActiveScheduler->UnregisterTick(PendingAdditions[PendingIndex].SchedulerHandle);
            }
            PendingAdditions.RemoveAtSwap(PendingIndex, EAllowShrinking::No);
            --ActiveEntityCount;
            return true;
        }
    }

    return false;
}

/**
 * @brief O(1) 动态调整实体重要度标记（于下一次 UpdateSpatialThrottle 生效）
 * @param InEntityHandle 实体句柄
 * @param bInHighSignificance 是否高优保真
 * @return 找到并修改返回 true
 */
bool FSpatialThrottleDriver::SetEntitySignificance(uint64 InEntityHandle, bool bInHighSignificance) {
    if (InEntityHandle == 0ULL) {
        return false;
    }

    if (const int32* FoundIndex = HandleToIndex.Find(InEntityHandle)) {
        Entities[*FoundIndex].bHighSignificance = bInHighSignificance;
        return true;
    }

    if (!bIsUpdating) {
        return false;
    }

    for (FSpatialEntity& Entry : PendingAdditions) {
        if (Entry.DriverHandle == InEntityHandle) {
            Entry.bHighSignificance = bInHighSignificance;
            return true;
        }
    }

    return false;
}

/**
 * @brief O(1) 查询指定实体句柄当前是否处于活跃注册状态
 * @param InEntityHandle 实体句柄
 * @return 已注册且未注销返回 true
 */
bool FSpatialThrottleDriver::IsEntityRegistered(uint64 InEntityHandle) const noexcept {
    if (InEntityHandle == 0ULL) {
        return false;
    }

    if (HandleToIndex.Contains(InEntityHandle)) {
        return true;
    }

    if (!bIsUpdating) {
        return false;
    }

    for (const FSpatialEntity& Entry : PendingAdditions) {
        if (Entry.DriverHandle == InEntityHandle) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 依观察者上下文集中批量评估并下发新节流周期（零 TFunction 拷贝，支持回调重入增删清）
 * @param ViewContext 空间视锥与观察者上下文
 */
void FSpatialThrottleDriver::UpdateSpatialThrottle(const FSpatialViewContext& ViewContext) {
    if (bIsUpdating) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[ThrottleDriver] 检测到 UpdateSpatialThrottle 递归重入调用，已忽略本次重入。"));
        return;
    }

    FThrottledTickScheduler* ActiveScheduler = GetActiveScheduler();
    if (ActiveScheduler == nullptr) {
        if (!Entities.IsEmpty()) {
            GAME_LOG(Warning, TEXT("[ThrottleDriver] 绑定的 UThrottledTickerSubsystem 已失效销毁，自动清空残留空间实体 (%d)。"), ActiveEntityCount);
            Entities.Reset();
            PendingAdditions.Reset();
            HandleToIndex.Reset();
            ActiveEntityCount = 0;
        }
        return;
    }

    if (!Game::Spatial::Private::IsFiniteVector(ViewContext.ViewLocation)
        || !Game::Spatial::Private::IsFiniteVector(ViewContext.ViewForward)) {
        GAME_LOG_THROTTLE(1.0, Warning, TEXT("[ThrottleDriver] 观察者上下文包含非有限浮点坐标/朝向，跳过本帧评估。"));
        return;
    }

    bIsUpdating = true;

    const FPrecomputedViewParams ViewParams = BuildViewParams(ViewContext);
    const int32 InitialCount = Entities.Num();

    for (int32 Index = 0; Index < InitialCount; ++Index) {
        FSpatialEntity& Entry = Entities[Index];
        if (Entry.bPendingRemoval) {
            continue;
        }

        if (Entry.Validator && !Entry.Validator()) {
            if (!Entry.bPendingRemoval) {
                ActiveScheduler->UnregisterTick(Entry.SchedulerHandle);
                Entry.bPendingRemoval = true;
                HandleToIndex.Remove(Entry.DriverHandle);
                --ActiveEntityCount;
            }
            continue;
        }

        if (Entry.bPendingRemoval) {
            continue;
        }

        const FVector EntityLocation = Entry.PositionGetter();
        if (Entry.bPendingRemoval) {
            continue;
        }

        ESpatialDistanceTier TargetTier = ESpatialDistanceTier::OutOfRange;
        const float TargetInterval = EvaluateEntityTier(Entry, EntityLocation, ViewParams, TargetTier);
        Entry.CurrentTier = TargetTier;

        if (FMath::Abs(TargetInterval - Entry.CurrentInterval) > KINDA_SMALL_NUMBER) {
            ActiveScheduler->SetTickInterval(Entry.SchedulerHandle, TargetInterval);
            Entry.CurrentInterval = TargetInterval;
        }
    }

    CompactRemovedEntities();
    FlushPendingAdditions();

    bIsUpdating = false;
}

/**
 * @brief 设置距离梯级与节流配置（自动钳制非负、递增与合法角度区间）
 * @param InConfig 新配置参数
 */
void FSpatialThrottleDriver::SetTierConfig(const FSpatialThrottleTierConfig& InConfig) {
    Config.DistanceNear = FMath::IsFinite(InConfig.DistanceNear) ? FMath::Max(0.0f, InConfig.DistanceNear) : 0.0f;
    Config.DistanceMid = FMath::IsFinite(InConfig.DistanceMid)
        ? FMath::Max(Config.DistanceNear, InConfig.DistanceMid)
        : Config.DistanceNear;
    Config.DistanceFar = FMath::IsFinite(InConfig.DistanceFar)
        ? FMath::Max(Config.DistanceMid, InConfig.DistanceFar)
        : Config.DistanceMid;
    Config.DistanceHysteresis = FMath::IsFinite(InConfig.DistanceHysteresis) ? FMath::Max(0.0f, InConfig.DistanceHysteresis) : 0.0f;
    Config.NearInterval = FMath::IsFinite(InConfig.NearInterval) ? FMath::Max(0.0f, InConfig.NearInterval) : 0.0f;
    Config.MidInterval = FMath::IsFinite(InConfig.MidInterval) ? FMath::Max(0.0f, InConfig.MidInterval) : 0.0f;
    Config.FarInterval = FMath::IsFinite(InConfig.FarInterval) ? FMath::Max(0.0f, InConfig.FarInterval) : 0.0f;
    Config.OutOfRangeOrBehindInterval = FMath::IsFinite(InConfig.OutOfRangeOrBehindInterval)
        ? FMath::Max(0.0f, InConfig.OutOfRangeOrBehindInterval)
        : 0.0f;
    Config.HysteresisDegrees = FMath::IsFinite(InConfig.HysteresisDegrees)
        ? FMath::Clamp(InConfig.HysteresisDegrees, 0.0f, 90.0f)
        : 0.0f;
}

/**
 * @brief 获取当前距离梯级配置
 * @return 配置常量引用
 */
const FSpatialThrottleTierConfig& FSpatialThrottleDriver::GetTierConfig() const noexcept {
    return Config;
}

/**
 * @brief 获取当前活跃注册的实体总数
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

    for (const FSpatialEntity& Entry : PendingAdditions) {
        if (ActiveScheduler != nullptr) {
            ActiveScheduler->UnregisterTick(Entry.SchedulerHandle);
        }
    }

    PendingAdditions.Reset();
    HandleToIndex.Reset();
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

/**
 * @brief 将观察者上下文与配置预计算为单帧只读评估参数包
 * @param ViewContext 空间观察上下文
 * @return 预计算视图参数包
 */
FSpatialThrottleDriver::FPrecomputedViewParams FSpatialThrottleDriver::BuildViewParams(
    const FSpatialViewContext& ViewContext
) const noexcept {
    FPrecomputedViewParams Params;
    Params.ViewLocation = ViewContext.ViewLocation;
    Params.bHasValidForward = !ViewContext.ViewForward.IsNearlyZero();
    Params.SafeViewForward = Params.bHasValidForward ? ViewContext.ViewForward.GetSafeNormal() : FVector::ZeroVector;

    const double RawFov = FMath::IsFinite(ViewContext.FOVDegrees) ? static_cast<double>(ViewContext.FOVDegrees) : 0.0;
    Params.bHasValidFov = (RawFov > 0.0);
    const double BaseHalfFovDeg = FMath::Clamp(RawFov * 0.5, 0.0, 180.0);
    const double ExpandedHalfFovDeg = FMath::Clamp(BaseHalfFovDeg + static_cast<double>(Config.HysteresisDegrees), 0.0, 180.0);
    Params.bFullEnterFov = (BaseHalfFovDeg >= 180.0);
    Params.bFullKeepFov = (ExpandedHalfFovDeg >= 180.0);
    Params.CosEnterFov = FMath::Cos(FMath::DegreesToRadians(BaseHalfFovDeg));
    Params.CosEnterFovSq = Params.CosEnterFov * Params.CosEnterFov;
    Params.CosKeepFov = FMath::Cos(FMath::DegreesToRadians(ExpandedHalfFovDeg));
    Params.CosKeepFovSq = Params.CosKeepFov * Params.CosKeepFov;

    const double RawViewMax = FMath::IsFinite(ViewContext.MaxDistance) ? static_cast<double>(ViewContext.MaxDistance) : 0.0;
    Params.EffectiveMaxDistance = FMath::Min(static_cast<double>(Config.DistanceFar), RawViewMax);
    Params.bHasValidRange = Params.EffectiveMaxDistance > 0.0;
    Params.DistMargin = static_cast<double>(Config.DistanceHysteresis);

    return Params;
}

/**
 * @brief 评估单个空间实体的目标距离档位与对应节流周期（零开方、零除法、线性早退）
 * @param Entry 空间实体记录
 * @param EntityLocation 实体当前世界坐标
 * @param ViewParams 预计算视图参数
 * @param OutTargetTier 输出目标距离档位
 * @return 应下发至调度器的目标周期（秒）
 */
float FSpatialThrottleDriver::EvaluateEntityTier(
    const FSpatialEntity& Entry,
    const FVector& EntityLocation,
    const FPrecomputedViewParams& ViewParams,
    ESpatialDistanceTier& OutTargetTier
) const noexcept {
    if (Entry.bHighSignificance) {
        OutTargetTier = ESpatialDistanceTier::Near;
        return Config.NearInterval;
    }

    if (!Game::Spatial::Private::IsFiniteVector(EntityLocation)) {
        OutTargetTier = ESpatialDistanceTier::OutOfRange;
        return Config.OutOfRangeOrBehindInterval;
    }

    const FVector ToEntity = EntityLocation - ViewParams.ViewLocation;
    const double DistSq = ToEntity.SizeSquared();
    if (DistSq <= UE_DOUBLE_KINDA_SMALL_NUMBER) {
        OutTargetTier = ESpatialDistanceTier::Near;
        return Config.NearInterval;
    }

    if (!ViewParams.bHasValidRange || !ViewParams.bHasValidForward || !ViewParams.bHasValidFov) {
        OutTargetTier = ESpatialDistanceTier::OutOfRange;
        return Config.OutOfRangeOrBehindInterval;
    }

    const bool bIsOutOfRange = (Entry.CurrentTier == ESpatialDistanceTier::OutOfRange);
    const double MaxDistThreshold = bIsOutOfRange
        ? ViewParams.EffectiveMaxDistance
        : (ViewParams.EffectiveMaxDistance + ViewParams.DistMargin);
    if (DistSq > MaxDistThreshold * MaxDistThreshold) {
        OutTargetTier = ESpatialDistanceTier::OutOfRange;
        return Config.OutOfRangeOrBehindInterval;
    }

    const bool bFullFov = bIsOutOfRange ? ViewParams.bFullEnterFov : ViewParams.bFullKeepFov;
    if (!bFullFov) {
        const double Proj = FVector::DotProduct(ToEntity, ViewParams.SafeViewForward);
        const double CosThreshold = bIsOutOfRange ? ViewParams.CosEnterFov : ViewParams.CosKeepFov;
        const double CosThresholdSq = bIsOutOfRange ? ViewParams.CosEnterFovSq : ViewParams.CosKeepFovSq;
        if (!Game::Spatial::Private::IsWithinConeAngle(Proj, DistSq, CosThreshold, CosThresholdSq)) {
            OutTargetTier = ESpatialDistanceTier::OutOfRange;
            return Config.OutOfRangeOrBehindInterval;
        }
    }

    const double NearThreshold = ComputeNearThreshold(Entry.CurrentTier, ViewParams.DistMargin);
    if (DistSq <= NearThreshold * NearThreshold) {
        OutTargetTier = ESpatialDistanceTier::Near;
        return Config.NearInterval;
    }

    const double MidThreshold = ComputeMidThreshold(Entry.CurrentTier, NearThreshold, ViewParams.DistMargin);
    if (DistSq <= MidThreshold * MidThreshold) {
        OutTargetTier = ESpatialDistanceTier::Mid;
        return Config.MidInterval;
    }

    OutTargetTier = ESpatialDistanceTier::Far;
    return Config.FarInterval;
}

/**
 * @brief 计算 Near/Mid 边界带迟滞保护的距离阈值
 * @param CurrentTier 实体当前档位
 * @param DistMargin 距离迟滞带宽度
 * @return 近距判定阈值
 */
double FSpatialThrottleDriver::ComputeNearThreshold(ESpatialDistanceTier CurrentTier, double DistMargin) const noexcept {
    const double BaseNear = static_cast<double>(Config.DistanceNear);
    switch (CurrentTier) {
        case ESpatialDistanceTier::Near:
            return BaseNear + DistMargin;
        case ESpatialDistanceTier::Mid:
            return FMath::Max(0.0, BaseNear - DistMargin);
        default:
            return BaseNear;
    }
}

/**
 * @brief 计算 Mid/Far 边界带迟滞保护的距离阈值
 * @param CurrentTier 实体当前档位
 * @param NearThreshold 已计算的近距阈值下界
 * @param DistMargin 距离迟滞带宽度
 * @return 中距判定阈值
 */
double FSpatialThrottleDriver::ComputeMidThreshold(
    ESpatialDistanceTier CurrentTier,
    double NearThreshold,
    double DistMargin
) const noexcept {
    const double BaseMid = static_cast<double>(Config.DistanceMid);
    switch (CurrentTier) {
        case ESpatialDistanceTier::Mid:
            return BaseMid + DistMargin;
        case ESpatialDistanceTier::Far:
            return FMath::Max(NearThreshold, BaseMid - DistMargin);
        default:
            return BaseMid;
    }
}

/**
 * @brief 非更新态下 O(1) 尾部交换移除指定索引实体并同步维护 HandleToIndex
 * @param Index 待移除实体在 Entities 中的索引
 */
void FSpatialThrottleDriver::RemoveEntityAtImmediate(int32 Index) {
    const uint64 RemovedHandle = Entities[Index].DriverHandle;
    HandleToIndex.Remove(RemovedHandle);

    const int32 LastIndex = Entities.Num() - 1;
    if (Index != LastIndex) {
        Entities[Index] = MoveTemp(Entities[LastIndex]);
        HandleToIndex.FindChecked(Entities[Index].DriverHandle) = Index;
    }

    Entities.Pop(EAllowShrinking::No);
}

/**
 * @brief 更新循环结束后逆序回收所有标记为待移除的实体并同步修正 HandleToIndex
 */
void FSpatialThrottleDriver::CompactRemovedEntities() {
    for (int32 Index = Entities.Num() - 1; Index >= 0; --Index) {
        if (!Entities[Index].bPendingRemoval) {
            continue;
        }

        const int32 LastIndex = Entities.Num() - 1;
        if (Index != LastIndex) {
            Entities[Index] = MoveTemp(Entities[LastIndex]);
            HandleToIndex.FindChecked(Entities[Index].DriverHandle) = Index;
        }
        Entities.Pop(EAllowShrinking::No);
    }
}

/**
 * @brief 将更新期间重入注册的实体批量刷入活跃数组与句柄索引表
 */
void FSpatialThrottleDriver::FlushPendingAdditions() {
    if (PendingAdditions.IsEmpty()) {
        return;
    }

    Entities.Reserve(Entities.Num() + PendingAdditions.Num());
    for (FSpatialEntity& PendingEntry : PendingAdditions) {
        const uint64 Handle = PendingEntry.DriverHandle;
        const int32 NewIndex = Entities.Add(MoveTemp(PendingEntry));
        HandleToIndex.Add(Handle, NewIndex);
    }
    PendingAdditions.Reset();
}
