/**
 * @file FSpatialThrottleDriver.h
 * @brief 空间视锥与重要度心跳节流驱动器
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FThrottledTickScheduler.h"

/**
 * @brief 摄像机/观察者空间观察上下文
 */
struct FSpatialViewContext {
    FVector ViewLocation = FVector::ZeroVector;
    FVector ViewForward = FVector::ForwardVector;
    float FOVDegrees = 90.0f;
    float MaxDistance = 10000.0f;

    FSpatialViewContext() = default;

    FSpatialViewContext(
        const FVector& InLocation,
        const FVector& InForward,
        float InFov = 90.0f,
        float InMaxDist = 10000.0f
    )
        : ViewLocation(InLocation)
        , ViewForward(InForward)
        , FOVDegrees(InFov)
        , MaxDistance(InMaxDist) {
    }
};

/**
 * @brief 空间分级节流配置参数
 */
struct FSpatialThrottleTierConfig {
    float DistanceNear = 1500.0f;
    float DistanceMid = 3000.0f;
    float DistanceFar = 6000.0f;
    float DistanceHysteresis = 50.0f;
    float NearInterval = 0.0f;
    float MidInterval = 0.0333f;
    float FarInterval = 0.0666f;
    float OutOfRangeOrBehindInterval = 0.5f;
    float HysteresisDegrees = 5.0f;
};

class UThrottledTickerSubsystem;

/**
 * @brief 空间视锥与实体重要度节流驱动器
 *
 * 集中评估场景实体的空间距离、视锥投影包含度与重要度优先级，
 * 动态调整 FThrottledTickScheduler 中的执行频率，
 * 消除分散计算开销与视野内画面抽搐。
 */
class GAMESPATIAL_API FSpatialThrottleDriver {
public:
    using FOwnerValidator = FThrottledTickScheduler::FOwnerValidator;
    using FThrottledTickCallback = FThrottledTickScheduler::FThrottledTickCallback;
    using FPositionGetter = TFunction<FVector()>;

private:
    enum class ESpatialDistanceTier : uint8 {
        Near = 0,
        Mid = 1,
        Far = 2,
        OutOfRange = 3
    };

    struct FSpatialEntity {
        uint64 DriverHandle = 0;
        uint64 SchedulerHandle = 0;
        FOwnerValidator Validator;
        FPositionGetter PositionGetter;
        float CurrentInterval = 0.0f;
        ESpatialDistanceTier CurrentTier = ESpatialDistanceTier::OutOfRange;
        bool bHighSignificance = false;
        bool bPendingRemoval = false;
    };

public:
    /**
     * @brief 构造函数：绑定纯 C++ 节流调度器实例
     * @param InScheduler 调度器引用
     */
    explicit FSpatialThrottleDriver(FThrottledTickScheduler& InScheduler);

    /**
     * @brief 构造函数：绑定世界节流子系统（通过弱引用安全跟踪子系统生命周期，防子系统先行销毁悬垂）
     * @param InSubsystem 节流子系统引用
     */
    explicit FSpatialThrottleDriver(UThrottledTickerSubsystem& InSubsystem);

    /**
     * @brief 析构函数：注销所有在调度器中注册的心跳
     */
    ~FSpatialThrottleDriver();

    FSpatialThrottleDriver(const FSpatialThrottleDriver&) = delete;
    FSpatialThrottleDriver& operator=(const FSpatialThrottleDriver&) = delete;
    FSpatialThrottleDriver(FSpatialThrottleDriver&&) = delete;
    FSpatialThrottleDriver& operator=(FSpatialThrottleDriver&&) = delete;

    /**
     * @brief 注册空间实体心跳任务（初始以 0.0f 满帧间隔注册以保证生成首帧立即响应，首次调用 UpdateSpatialThrottle 后按空间距离梯级切换实际间隔）
     * @param InValidator 实体有效性校验器
     * @param InPositionGetter 实体世界坐标获取器
     * @param InCallback 实体 Tick 回调
     * @param bInHighSignificance 是否为高重要度实体（如锁定目标/Boss，强制满帧）
     * @return 驱动器句柄 ID（0 代表失败）
     */
    uint64 RegisterSpatialEntity(
        FOwnerValidator InValidator,
        FPositionGetter InPositionGetter,
        FThrottledTickCallback InCallback,
        bool bInHighSignificance = false
    );

    /**
     * @brief 注销指定空间实体
     * @param InEntityHandle 实体句柄
     * @return 成功注销返回 true
     */
    bool UnregisterSpatialEntity(uint64 InEntityHandle);

    /**
     * @brief 动态调整实体重要度标记
     * @param InEntityHandle 实体句柄
     * @param bInHighSignificance 是否高优保真
     * @return 找到并修改返回 true
     */
    bool SetEntitySignificance(uint64 InEntityHandle, bool bInHighSignificance);

    /**
     * @brief 依观察者上下文集中批量评估并下发新节流周期
     * @param ViewContext 空间视锥与观察者上下文
     */
    void UpdateSpatialThrottle(const FSpatialViewContext& ViewContext);

    /**
     * @brief 设置距离梯级与节流配置
     * @param InConfig 新配置参数
     */
    void SetTierConfig(const FSpatialThrottleTierConfig& InConfig);

    /**
     * @brief 获取当前距离梯级配置
     * @return 配置常量引用
     */
    [[nodiscard]] const FSpatialThrottleTierConfig& GetTierConfig() const noexcept;

    /**
     * @brief 获取当前注册的实体总数
     * @return 实体数量
     */
    [[nodiscard]] int32 Num() const noexcept;

    /**
     * @brief 清空并注销所有实体
     */
    void Clear();

private:
    /**
     * @brief 解析并返回当前有效的底层节流心跳调度器指针
     * @return 调度器指针，若绑定的子系统已销毁则返回 nullptr
     */
    [[nodiscard]] FThrottledTickScheduler* GetActiveScheduler() const noexcept;

private:
    FThrottledTickScheduler* DirectScheduler = nullptr;
    TWeakObjectPtr<UThrottledTickerSubsystem> SubsystemWeak;
    FSpatialThrottleTierConfig Config;
    TArray<FSpatialEntity> Entities;
    uint64 NextHandleSeed = 1ULL;
    int32 ActiveEntityCount = 0;
    bool bBoundToSubsystem = false;
    bool bIsUpdating = false;
};
