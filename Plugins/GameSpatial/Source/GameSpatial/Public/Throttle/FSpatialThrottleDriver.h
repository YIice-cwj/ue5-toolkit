/**
 * @file FSpatialThrottleDriver.h
 * @brief 空间视锥与实体重要度心跳节流驱动器声明（零热路径拷贝与双向迟滞带保护）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FThrottledTickScheduler.h"
#include "Throttle/FSpatialThrottleTierConfig.h"
#include "Throttle/FSpatialViewContext.h"

class UThrottledTickerSubsystem;

namespace Game::Spatial::Detail {

    /**
     * @brief 节流驱动器内部空间距离档位枚举
     */
    enum class ESpatialDistanceTier : uint8 {
        Near = 0,
        Mid = 1,
        Far = 2,
        OutOfRange = 3
    };

}  // namespace Game::Spatial::Detail

/**
 * @brief 空间视锥与实体重要度节流驱动器
 *
 * 集中批量评估场景实体的空间距离（含 DistanceFar 与 ViewContext.MaxDistance 双上限）、
 * 视锥投影包含度、双向迟滞带与重要度优先级，动态驱动 FThrottledTickScheduler 的执行周期。
 * 非线程安全，所有方法必须在 GameThread 调用。
 */
class GAMESPATIAL_API FSpatialThrottleDriver {
public:
    using FOwnerValidator = FThrottledTickScheduler::FOwnerValidator;
    using FThrottledTickCallback = FThrottledTickScheduler::FThrottledTickCallback;
    using FPositionGetter = TFunction<FVector()>;

private:
    using ESpatialDistanceTier = Game::Spatial::Detail::ESpatialDistanceTier;

    struct FSpatialEntity {
        uint64 DriverHandle = 0ULL;
        uint64 SchedulerHandle = 0ULL;
        FOwnerValidator Validator;
        FPositionGetter PositionGetter;
        float CurrentInterval = 0.0f;
        ESpatialDistanceTier CurrentTier = ESpatialDistanceTier::OutOfRange;
        bool bHighSignificance = false;
        bool bPendingRemoval = false;
    };

public:
    /**
     * @brief 构造函数：绑定纯 C++ 节流调度器实例（调用方须保证 InScheduler 生命周期长于本驱动器）
     * @param InScheduler 调度器引用
     */
    explicit FSpatialThrottleDriver(FThrottledTickScheduler& InScheduler);

    /**
     * @brief 构造函数：绑定世界节流子系统（通过弱引用安全跟踪子系统生命周期，防先行销毁悬垂）
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
     * @brief 注册空间实体心跳任务（初始以 0.0f 满帧间隔注册以保证生成首帧响应，首次 UpdateSpatialThrottle 后按梯级切换）
     * @param InValidator 实体有效性校验器（可为空）
     * @param InPositionGetter 实体世界坐标获取器（必须非空）
     * @param InCallback 实体 Tick 回调（必须非空）
     * @param bInHighSignificance 是否为高重要度实体（如锁定目标/Boss，强制维持 NearInterval）
     * @return 驱动器句柄 ID（0 代表注册失败）
     */
    uint64 RegisterSpatialEntity(
        FOwnerValidator InValidator,
        FPositionGetter InPositionGetter,
        FThrottledTickCallback InCallback,
        bool bInHighSignificance = false
    );

    /**
     * @brief 注销指定空间实体（驱动器侧 O(1) 句柄索引移除并同步注销底层调度器心跳，支持更新期安全重入）
     * @param InEntityHandle 实体句柄
     * @return 成功注销返回 true，句柄无效或未注册返回 false
     */
    bool UnregisterSpatialEntity(uint64 InEntityHandle);

    /**
     * @brief O(1) 动态调整实体重要度标记（于下一次 UpdateSpatialThrottle 生效）
     * @param InEntityHandle 实体句柄
     * @param bInHighSignificance 是否高优保真
     * @return 找到并修改返回 true
     */
    bool SetEntitySignificance(uint64 InEntityHandle, bool bInHighSignificance);

    /**
     * @brief O(1) 查询指定实体句柄当前是否处于活跃注册状态
     * @param InEntityHandle 实体句柄
     * @return 已注册且未注销返回 true
     */
    [[nodiscard]] bool IsEntityRegistered(uint64 InEntityHandle) const noexcept;

    /**
     * @brief 依观察者上下文集中批量评估并下发新节流周期（含距离/视锥双向迟滞带及回调重入防护）
     * @param ViewContext 空间视锥与观察者上下文
     */
    void UpdateSpatialThrottle(const FSpatialViewContext& ViewContext);

    /**
     * @brief 设置距离梯级与节流配置（自动对非负与递增约束执行安全钳制）
     * @param InConfig 新配置参数
     */
    void SetTierConfig(const FSpatialThrottleTierConfig& InConfig);

    /**
     * @brief 获取当前距离梯级配置
     * @return 配置常量引用
     */
    [[nodiscard]] const FSpatialThrottleTierConfig& GetTierConfig() const noexcept;

    /**
     * @brief 获取当前活跃注册的实体总数
     * @return 活跃实体数量
     */
    [[nodiscard]] int32 Num() const noexcept;

    /**
     * @brief 清空并注销所有实体（支持在 UpdateSpatialThrottle 回调中安全重入清空）
     */
    void Clear();

private:
    [[nodiscard]] FThrottledTickScheduler* GetActiveScheduler() const noexcept;
    [[nodiscard]] int32 FindPendingAdditionIndex(uint64 InEntityHandle) const noexcept;
    void MarkInvalidEntityRemoved(FSpatialEntity& Entry, FThrottledTickScheduler& ActiveScheduler);
    void RemoveEntityAtImmediate(int32 Index);
    void CompactRemovedEntities();
    void FlushPendingAdditions();

private:
    FThrottledTickScheduler* DirectScheduler = nullptr;
    TWeakObjectPtr<UThrottledTickerSubsystem> SubsystemWeak;
    FSpatialThrottleTierConfig Config;
    TArray<FSpatialEntity> Entities;
    TArray<FSpatialEntity> PendingAdditions;
    TMap<uint64, int32> HandleToIndex;
    uint64 NextHandleSeed = 1ULL;
    int32 ActiveEntityCount = 0;
    bool bBoundToSubsystem = false;
    bool bIsUpdating = false;
};
