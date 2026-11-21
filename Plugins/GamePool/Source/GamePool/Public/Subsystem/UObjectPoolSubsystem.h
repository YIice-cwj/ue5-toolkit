/**
 * @file UObjectPoolSubsystem.h
 * @brief 世界级对象池外观门面子系统（统一编排 Actor 池、纯 UObject 池、代数句柄、协程预热、TTL 淘汰与容灾泄洪）
 */

#pragma once

#include "CoreMinimal.h"
#include "Async/FCancellationToken.h"
#include "Async/TGameCoroTask.h"
#include "Core/FPoolAcquirePayload.h"
#include "Core/IGamePoolable.h"
#include "Core/ObjectPoolTypes.h"
#include "Core/PoolErrorCodes.h"
#include "GameFramework/Actor.h"
#include "Handle/TPooledObjectHandle.h"
#include "Handle/TScopedPoolRental.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Types/TGameResult.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/SoftObjectPtr.h"

#include <type_traits>

#include "UObjectPoolSubsystem.generated.h"

/**
 * @brief 世界级通用对象池子系统外观门面（内部组件经 Pimpl 完整隐藏，CDO 零堆分配且零 FGCObject 注册）
 */
UCLASS()
class GAMEPOOL_API UObjectPoolSubsystem : public UWorldSubsystem {
    GENERATED_BODY()

private:
    struct FImpl;

public:
    /**
     * @brief 默认构造函数
     */
    UObjectPoolSubsystem();

    /**
     * @brief 虚析构函数（置于实现单元以支持 Pimpl 完整析构）
     */
    virtual ~UObjectPoolSubsystem() override;

    /**
     * @brief 初始化对象池子系统，创建底层管理器、调度器与弹性控制器上下文
     * @param Collection 子系统集合引用
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 反初始化对象池子系统，清理全部池实例、Stat 计数、延迟队列、调度器与容灾委托
     */
    virtual void Deinitialize() override;

    /**
     * @brief 同步预热指定数量的 Actor 到空闲对象池（严格受 MaxIdleCount 上限约束）
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void Prewarm(TSubclassOf<AActor> ActorClass, int32 PrewarmCount);

    /**
     * @brief 基于 C++20 协程的分步异步预热 Actor
     * @param ActorClass 目标 Actor 类
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数（防帧率尖刺）
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        TSubclassOf<AActor> ActorClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于软对象路径的 C++20 协程事件驱动流式加载与分步异步预热 Actor
     * @param ActorClassPath 目标 Actor 类的软对象路径
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        const FSoftObjectPath& ActorClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于强类型软类引用的 C++20 协程流式加载与分步异步预热 Actor
     * @param ActorSoftClass 目标 Actor 软类引用
     * @param PrewarmCount 预热生成的空闲实例总数量
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 实际成功预热生成的实例总数异步协程任务
     */
    TGameCoroTask<int32> PrewarmAsync(
        const TSoftClassPtr<AActor>& ActorSoftClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 1,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 设置指定 Actor 类别的最大空闲实例缓存容量（超限时立即裁剪多余空闲实例）
     * @param ActorClass 目标 Actor 类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxPoolCapacity(TSubclassOf<AActor> ActorClass, int32 InMaxIdleCount);

    /**
     * @brief 配置指定 Actor 类别归还入池时的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @param InResetFlags 重置位掩码
     */
    void SetPoolResetFlags(TSubclassOf<AActor> ActorClass, EPoolResetFlags InResetFlags);

    /**
     * @brief 查询指定 Actor 类别当前的组件自动重置策略掩码
     * @param ActorClass 目标 Actor 类
     * @return 当前配置的组件重置策略掩码
     */
    [[nodiscard]] EPoolResetFlags GetPoolResetFlags(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 从池中借出或生成 Actor 实例（蓝图专用外观方法）
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 借出的 Actor 原生指针，失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Actor", DeterminesOutputType = "ActorClass"))
    AActor* K2_AcquireActor(TSubclassOf<AActor> ActorClass, const FTransform& SpawnTransform);

    /**
     * @brief 从池中借出或生成强类型 Actor 实例
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类（默认 T::StaticClass()）
     * @param SpawnTransform 初始空间变换
     * @return 包含目标 Actor 指针的强类型状态结果
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<T*> AcquireActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 从池中借出强类型 Actor 实例并注入带编译期类型指纹的业务载荷
     * @tparam T 具体的 AActor 派生类
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务载荷常量引用
     * @return 包含目标 Actor 指针的强类型状态结果
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出 Actor 并包装为带世代防悬空校验的安全句柄
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandle(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 从池中借出 Actor 并注入业务载荷，包装为安全句柄
     * @tparam T 具体的 AActor 派生类
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务载荷常量引用
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireActorHandleWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 借出 Actor 的 RAII 作用域自动归还租约卫士
     * @tparam T 具体的 AActor 派生类
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = AActor>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActor(
        TSubclassOf<AActor> ActorClass = T::StaticClass(),
        const FTransform& SpawnTransform = FTransform::Identity
    );

    /**
     * @brief 借出携带业务载荷的 Actor RAII 作用域自动归还租约卫士
     * @tparam T 具体的 AActor 派生类
     * @tparam TPayload 业务载荷类型
     * @param ActorClass 目标 Actor 类
     * @param SpawnTransform 初始空间变换
     * @param Payload 业务载荷常量引用
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = AActor, typename TPayload>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedActorWithPayload(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        const TPayload& Payload
    );

    /**
     * @brief 将已借出的 Actor 归还回池缓存（蓝图专用外观方法）
     * @param Actor 待归还的 Actor 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Actor"))
    void K2_ReleaseActor(AActor* Actor);

    /**
     * @brief 将已借出的 Actor 归还回池缓存
     * @param Actor 待归还的 Actor 实例
     * @return 归还状态结果
     */
    TGameStatusResult<void> ReleaseActor(AActor* Actor);

    /**
     * @brief 通过安全句柄归还 Actor 回池（单次校验世代并拒绝跨子系统句柄）
     * @tparam T 具体的 AActor 派生类
     * @param Handle 待归还的受管句柄引用
     * @return 归还状态结果
     */
    template <typename T>
    TGameStatusResult<void> ReleaseActor(const TPooledObjectHandle<T>& Handle);

    /**
     * @brief 异步延时将 Actor 加入无锁归还队列（并发/多线程安全）
     * @param Actor 待归还的目标 Actor 实例
     * @return 成功入队返回 Success，队列已满或 Actor 无效返回 Failure
     */
    TGameStatusResult<void> ReleaseActorDeferred(AActor* Actor);

    /**
     * @brief 查询指定 Actor 类别的池容量与活跃统计快照
     * @param ActorClass 目标 Actor 类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetPoolStats(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 清空指定 Actor 类别的所有空闲实例并释放资源
     * @param ActorClass 目标 Actor 类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearPool(TSubclassOf<AActor> ActorClass);

    /**
     * @brief 清空所有 Actor 类别池中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllActorPools();

    /**
     * @brief 同步预热指定数量的纯 UObject 到对象池（严格受 MaxIdleCount 上限约束）
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热生成的实例数量
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void PrewarmObjectPool(TSubclassOf<UObject> ObjectClass, int32 PrewarmCount);

    /**
     * @brief 基于 C++20 协程的分步异步预热纯 UObject 对象池
     * @param ObjectClass 目标 UObject 类
     * @param PrewarmCount 预热生成的总空闲实例数
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 异步协程任务，返回实际成功实例化的数量
     */
    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        TSubclassOf<UObject> ObjectClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于软对象路径的 C++20 协程事件驱动流式加载与分步异步预热纯 UObject 对象池
     * @param ObjectClassPath 目标 UObject 类的软对象路径
     * @param PrewarmCount 预热生成的总空闲实例数
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 异步协程任务，返回实际成功实例化的数量
     */
    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        const FSoftObjectPath& ObjectClassPath,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 基于强类型软类引用的 C++20 协程流式加载与分步异步预热纯 UObject 对象池
     * @param ObjectSoftClass 目标 UObject 软类引用
     * @param PrewarmCount 预热生成的总空闲实例数
     * @param MaxSpawnsPerStep 单步最大生成实例数
     * @param CancellationToken 协作式取消令牌
     * @return 异步协程任务，返回实际成功实例化的数量
     */
    TGameCoroTask<int32> PrewarmObjectPoolAsync(
        const TSoftClassPtr<UObject>& ObjectSoftClass,
        int32 PrewarmCount,
        int32 MaxSpawnsPerStep = 4,
        FCancellationToken CancellationToken = FCancellationToken::None()
    );

    /**
     * @brief 设置指定非 Actor UObject 类别的最大空闲缓存容量（超限时立即裁剪多余空闲实例）
     * @param ObjectClass 目标 UObject 派生类
     * @param InMaxIdleCount 最大空闲数（<=0 表示无限制）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void SetMaxObjectPoolCapacity(TSubclassOf<UObject> ObjectClass, int32 InMaxIdleCount);

    /**
     * @brief 从池中借出或构建非 Actor UObject 实例（蓝图专用外观方法）
     * @param ObjectClass 目标 UObject 派生类
     * @return 激活的 UObject 实例指针，失败返回 nullptr
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Acquire Object", DeterminesOutputType = "ObjectClass"))
    UObject* K2_AcquireObject(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 从池中借出或构建强类型非 Actor UObject 实例
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类（默认 T::StaticClass()）
     * @return 包含对象指针的强类型状态结果
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<T*> AcquireObject(TSubclassOf<UObject> ObjectClass = T::StaticClass());

    /**
     * @brief 从池中借出纯 UObject 实例并注入带编译期类型指纹的业务载荷
     * @tparam T 具体的 UObject 派生类
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务载荷常量引用
     * @return 包含对象指针的强类型状态结果
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<T*> AcquireObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 从池中借出纯 UObject 并包装为带世代防悬空校验的安全句柄
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandle(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 从池中借出纯 UObject 并注入业务载荷，包装为安全句柄
     * @tparam T 具体的 UObject 派生类
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务载荷常量引用
     * @return 包含安全句柄的强类型状态结果
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<TPooledObjectHandle<T>> AcquireObjectHandleWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 借出纯 UObject 的 RAII 作用域自动归还租约卫士
     * @tparam T 具体的 UObject 派生类
     * @param ObjectClass 目标类
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = UObject>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObject(
        TSubclassOf<UObject> ObjectClass = T::StaticClass()
    );

    /**
     * @brief 借出携带业务载荷的纯 UObject RAII 作用域自动归还租约卫士
     * @tparam T 具体的 UObject 派生类
     * @tparam TPayload 业务载荷类型
     * @param ObjectClass 目标类
     * @param Payload 业务载荷常量引用
     * @return 包含作用域租约卫士的强类型状态结果
     */
    template <typename T = UObject, typename TPayload>
    [[nodiscard]] TGameStatusResult<TScopedPoolRental<T>> AcquireScopedObjectWithPayload(
        TSubclassOf<UObject> ObjectClass,
        const TPayload& Payload
    );

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存（蓝图专用外观方法）
     * @param Object 待归还的 UObject 实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool", meta = (DisplayName = "Release Object"))
    void K2_ReleaseObject(UObject* Object);

    /**
     * @brief 将已借出的非 Actor UObject 归还回池缓存
     * @param Object 待归还的 UObject 实例
     * @return 归还状态结果
     */
    TGameStatusResult<void> ReleaseObject(UObject* Object);

    /**
     * @brief 通过安全句柄归还非 Actor UObject 回池（单次校验世代并拒绝跨子系统句柄）
     * @tparam T 具体的 UObject 派生类
     * @param Handle 待归还的受管句柄引用
     * @return 归还状态结果
     */
    template <typename T>
    TGameStatusResult<void> ReleaseObject(const TPooledObjectHandle<T>& Handle);

    /**
     * @brief 异步延时将纯 UObject 加入无锁归还队列（并发/多线程安全，拒绝 AActor 派生实例）
     * @param Object 待归还的目标 UObject 实例
     * @return 成功入队返回 Success，队列已满、对象无效或属于 AActor 返回 Failure
     */
    TGameStatusResult<void> ReleaseObjectDeferred(UObject* Object);

    /**
     * @brief 查询指定纯 UObject 类的池容量与活跃统计快照
     * @param ObjectClass 目标类
     * @return 池状态统计结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|ObjectPool")
    [[nodiscard]] FPoolStats GetObjectPoolStats(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 清空指定纯 UObject 类别的所有空闲实例并释放垃圾回收引用
     * @param ObjectClass 目标类
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearObjectPool(TSubclassOf<UObject> ObjectClass);

    /**
     * @brief 清空所有非 Actor 纯 UObject 类别池中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllObjectPools();

    /**
     * @brief 清空所有池类别（Actor 与纯 UObject）中的全部空闲实例
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|ObjectPool")
    void ClearAllPools();

    /**
     * @brief 在 GameThread 批量收割无锁延迟归还队列中的待归还实例（Actor 与 UObject）
     * @param MaxProcessCount 本次最大成功归还配额（<=0 直接返回 0）
     * @return 实际成功归还入池的实例总数
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Deferred")
    int32 ProcessDeferredReleases(int32 MaxProcessCount = 64);

    /**
     * @brief 查询当前无锁延迟归还队列中的近似待处理条目数量
     * @return 近似积压数量
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Deferred")
    [[nodiscard]] int32 GetPendingDeferredReleaseCount() const noexcept;

    /**
     * @brief 配置指定 Actor 类别的闲置弹性缩容与淘汰策略（自动同步后台心跳周期或停机）
     * @param ActorClass 目标 Actor 类
     * @param Settings 淘汰策略配置
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass, const FPoolEvictionSettings& Settings);

    /**
     * @brief 查询指定 Actor 类别的闲置弹性缩容与淘汰策略
     * @param ActorClass 目标 Actor 类
     * @return 当前淘汰配置
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    [[nodiscard]] FPoolEvictionSettings GetActorPoolEvictionSettings(TSubclassOf<AActor> ActorClass) const;

    /**
     * @brief 配置指定纯 UObject 类别的闲置弹性缩容与淘汰策略（自动同步后台心跳周期或停机）
     * @param ObjectClass 目标 UObject 类
     * @param Settings 淘汰策略配置
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    void SetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass, const FPoolEvictionSettings& Settings);

    /**
     * @brief 查询指定纯 UObject 类的闲置弹性缩容与淘汰策略
     * @param ObjectClass 目标 UObject 类
     * @return 当前淘汰配置
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Eviction")
    [[nodiscard]] FPoolEvictionSettings GetObjectPoolEvictionSettings(TSubclassOf<UObject> ObjectClass) const;

    /**
     * @brief 强制触发一次全部池的超时冷实例淘汰（同时尊重全局配额与各池 MaxEvictionsPerStep 限流）
     * @param MaxEvictionsPerStep 单次最大总销毁配额（<=0 使用默认值 32）
     * @param CustomNowSeconds 自定义时间戳（<=0 使用当前硬件单调时间戳）
     * @return 实际销毁的冷实例总数
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Eviction")
    int32 ForceEvictExpired(int32 MaxEvictionsPerStep = 32, double CustomNowSeconds = 0.0);

    /**
     * @brief 获取当前对象池运行时的全局遥测快照
     * @return 全局遥测快照结构体
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Telemetry")
    [[nodiscard]] FPoolTelemetrySnapshot GetTelemetrySnapshot() const;

    /**
     * @brief 重置对象池遥测统计计数器（不影响当前活跃与空闲实例数）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Telemetry")
    void ResetTelemetry();

    /**
     * @brief 强制释放当前所有池中的闲置空闲实例
     * @param bPreserveMinCount 若为 true 则优先保留各池配置的 MinPreservedCount 保底数量，若为 false 则全部清空
     * @return 实际销毁并释放的空闲实例总数
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    int32 FlushAllIdleInstances(bool bPreserveMinCount = false);

    /**
     * @brief 设置是否启用在操作系统/引擎内存紧张时自动紧急泄洪缩容
     * @param bEnable true 为开启（默认开启）
     */
    UFUNCTION(BlueprintCallable, Category = "GamePool|Resilience")
    void SetLowMemoryAutoFlushEnabled(bool bEnable);

    /**
     * @brief 查询当前低内存自动泄洪保护是否开启
     * @return 若开启返回 true
     */
    UFUNCTION(BlueprintPure, Category = "GamePool|Resilience")
    [[nodiscard]] bool IsLowMemoryAutoFlushEnabled() const noexcept;

    /**
     * @brief 获取世界上下文绑定的对象池子系统单例引用（上下文无效时触发断言）
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例引用
     */
    [[nodiscard]] static UObjectPoolSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 安全获取世界上下文绑定的对象池子系统指针
     * @param WorldContextObject 世界上下文对象
     * @return 子系统实例指针，上下文无效返回 nullptr
     */
    [[nodiscard]] static UObjectPoolSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief UE 垃圾回收强引用收集静态入口（统一追踪 Actor 与 UObject 全部 Class Key、空闲与活跃实例）
     * @param InThis 子系统对象指针
     * @param Collector 引用收集器
     */
    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
    friend class FActorPoolManager;
    friend bool Game::Pool::Private::ValidatePooledHandle(
        const UObjectPoolSubsystem* Subsystem,
        const FWeakObjectPtr& ObjectWeak,
        uint32 Generation
    );

    UFUNCTION()
    void OnPooledActorDestroyed(AActor* DestroyedActor);

    TGameStatusResult<AActor*> AcquireActorInternal(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        FPoolAcquirePayload Payload
    );

    TGameStatusResult<UObject*> AcquireObjectInternal(
        TSubclassOf<UObject> ObjectClass,
        FPoolAcquirePayload Payload
    );

    [[nodiscard]] uint32 FindObjectGeneration(const UObject* Object) const;
    [[nodiscard]] bool ValidateHandleGeneration(const FWeakObjectPtr& ObjectWeak, uint32 ExpectedGeneration) const;

    template <typename T>
    [[nodiscard]] TGameStatusResult<T*> AcquireActorTyped(
        TSubclassOf<AActor> ActorClass,
        const FTransform& SpawnTransform,
        FPoolAcquirePayload Payload
    );

    template <typename T>
    [[nodiscard]] TGameStatusResult<T*> AcquireObjectTyped(
        TSubclassOf<UObject> ObjectClass,
        FPoolAcquirePayload Payload
    );

private:
    TSharedPtr<FImpl> Impl;
};

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActorTyped(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    FPoolAcquirePayload Payload
) {
    static_assert(std::is_base_of_v<AActor, T>, "AcquireActor<T> 仅支持 AActor 派生类型");
    if (!ActorClass) {
        return TGameStatusResult<T*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ActorClass is null"))
        );
    }
    if (!ActorClass->IsChildOf(T::StaticClass())) {
        return TGameStatusResult<T*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::TypeMismatch, TEXT("ActorClass is not derived from target type T"))
        );
    }
    const TGameStatusResult<AActor*> RawResult = AcquireActorInternal(ActorClass, SpawnTransform, Payload);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<T*>::Failure(RawResult.GetError());
    }
    return TGameStatusResult<T*>::Success(CastChecked<T>(RawResult.GetValue()));
}

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActor(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform
) {
    return AcquireActorTyped<T>(ActorClass, SpawnTransform, FPoolAcquirePayload());
}

template <typename T, typename TPayload>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireActorWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    return AcquireActorTyped<T>(ActorClass, SpawnTransform, FPoolAcquirePayload::From(Payload));
}

template <typename T>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireActorHandle(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform
) {
    const TGameStatusResult<T*> RawResult = AcquireActorTyped<T>(ActorClass, SpawnTransform, FPoolAcquirePayload());
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError());
    }
    T* Actor = RawResult.GetValue();
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(
        TPooledObjectHandle<T>(Actor, this, FindObjectGeneration(Actor))
    );
}

template <typename T, typename TPayload>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireActorHandleWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> RawResult = AcquireActorTyped<T>(ActorClass, SpawnTransform, FPoolAcquirePayload::From(Payload));
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError());
    }
    T* Actor = RawResult.GetValue();
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(
        TPooledObjectHandle<T>(Actor, this, FindObjectGeneration(Actor))
    );
}

template <typename T>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedActor(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult = AcquireActorHandle<T>(ActorClass, SpawnTransform);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError());
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}

template <typename T, typename TPayload>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedActorWithPayload(
    TSubclassOf<AActor> ActorClass,
    const FTransform& SpawnTransform,
    const TPayload& Payload
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult =
        AcquireActorHandleWithPayload<T, TPayload>(ActorClass, SpawnTransform, Payload);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError());
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}

template <typename T>
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseActor(const TPooledObjectHandle<T>& Handle) {
    static_assert(std::is_base_of_v<AActor, T>, "ReleaseActor(Handle) 仅支持 AActor 派生类型句柄");
    if (Handle.GetSubsystem() != this) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidHandle, TEXT("Handle does not belong to this UObjectPoolSubsystem"))
        );
    }
    T* RawActor = Handle.Get();
    if (RawActor == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidHandle, TEXT("Handle is invalid or expired"))
        );
    }
    return ReleaseActor(RawActor);
}

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireObjectTyped(
    TSubclassOf<UObject> ObjectClass,
    FPoolAcquirePayload Payload
) {
    static_assert(std::is_base_of_v<UObject, T>, "AcquireObject<T> 仅支持 UObject 派生类型");
    static_assert(!std::is_base_of_v<AActor, T>, "AActor 派生类请使用 AcquireActor<T>");
    if (!ObjectClass) {
        return TGameStatusResult<T*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidClass, TEXT("ObjectClass is null"))
        );
    }
    if (!ObjectClass->IsChildOf(T::StaticClass())) {
        return TGameStatusResult<T*>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::TypeMismatch, TEXT("ObjectClass is not derived from target type T"))
        );
    }
    const TGameStatusResult<UObject*> RawResult = AcquireObjectInternal(ObjectClass, Payload);
    if (RawResult.IsFailure()) {
        return TGameStatusResult<T*>::Failure(RawResult.GetError());
    }
    return TGameStatusResult<T*>::Success(CastChecked<T>(RawResult.GetValue()));
}

template <typename T>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireObject(TSubclassOf<UObject> ObjectClass) {
    return AcquireObjectTyped<T>(ObjectClass, FPoolAcquirePayload());
}

template <typename T, typename TPayload>
TGameStatusResult<T*> UObjectPoolSubsystem::AcquireObjectWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    return AcquireObjectTyped<T>(ObjectClass, FPoolAcquirePayload::From(Payload));
}

template <typename T>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireObjectHandle(
    TSubclassOf<UObject> ObjectClass
) {
    const TGameStatusResult<T*> RawResult = AcquireObjectTyped<T>(ObjectClass, FPoolAcquirePayload());
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError());
    }
    T* Object = RawResult.GetValue();
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(
        TPooledObjectHandle<T>(Object, this, FindObjectGeneration(Object))
    );
}

template <typename T, typename TPayload>
TGameStatusResult<TPooledObjectHandle<T>> UObjectPoolSubsystem::AcquireObjectHandleWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    const TGameStatusResult<T*> RawResult = AcquireObjectTyped<T>(ObjectClass, FPoolAcquirePayload::From(Payload));
    if (RawResult.IsFailure()) {
        return TGameStatusResult<TPooledObjectHandle<T>>::Failure(RawResult.GetError());
    }
    T* Object = RawResult.GetValue();
    return TGameStatusResult<TPooledObjectHandle<T>>::Success(
        TPooledObjectHandle<T>(Object, this, FindObjectGeneration(Object))
    );
}

template <typename T>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedObject(
    TSubclassOf<UObject> ObjectClass
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult = AcquireObjectHandle<T>(ObjectClass);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError());
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}

template <typename T, typename TPayload>
TGameStatusResult<TScopedPoolRental<T>> UObjectPoolSubsystem::AcquireScopedObjectWithPayload(
    TSubclassOf<UObject> ObjectClass,
    const TPayload& Payload
) {
    const TGameStatusResult<TPooledObjectHandle<T>> HandleResult =
        AcquireObjectHandleWithPayload<T, TPayload>(ObjectClass, Payload);
    if (HandleResult.IsFailure()) {
        return TGameStatusResult<TScopedPoolRental<T>>::Failure(HandleResult.GetError());
    }
    return TGameStatusResult<TScopedPoolRental<T>>::Success(TScopedPoolRental<T>(HandleResult.GetValue()));
}

template <typename T>
TGameStatusResult<void> UObjectPoolSubsystem::ReleaseObject(const TPooledObjectHandle<T>& Handle) {
    static_assert(std::is_base_of_v<UObject, T>, "ReleaseObject(Handle) 仅支持 UObject 派生类型句柄");
    static_assert(!std::is_base_of_v<AActor, T>, "AActor 句柄请使用 ReleaseActor(Handle)");
    if (Handle.GetSubsystem() != this) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidHandle, TEXT("Handle does not belong to this UObjectPoolSubsystem"))
        );
    }
    T* RawObject = Handle.Get();
    if (RawObject == nullptr) {
        return TGameStatusResult<void>::Failure(
            FGameError::Make(Game::ModuleId::Pool, Game::PoolErrorCodes::InvalidHandle, TEXT("Handle is invalid or expired"))
        );
    }
    return ReleaseObject(RawObject);
}
