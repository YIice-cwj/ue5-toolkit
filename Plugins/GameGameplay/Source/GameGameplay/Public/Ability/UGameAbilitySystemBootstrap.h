/**
 * @file UGameAbilitySystemBootstrap.h
 * @brief GAS 引导通用组件（触发矩阵全时序固化，游戏层零机制代码）
 *
 * 设计思路：把能力系统握手的全部时序收敛到框架层——幂等门（以 Avatar 身份与 ASC 实例作短路闩，
 * 兼收监听服务器主机 OnRep 双调用与多 Pawn 重生重建）、数据就绪门、复制就绪门（限次重试 + 超限强制）、
 * 兜底自愈；游戏层只需注入三个委托（取 ASC / 写初值 / 数据就绪判定）并配置默认能力表，不写任何引导时序。
 * 宿主形态无关：OwnerActor 取自 ASC 自身的拥有者，Avatar 由宿主握手钩子显式传入。
 */

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "GameplayAbilitySpecHandle.h"

#include "Ability/FBootstrapRetryGate.h"
#include "Core/IGamePoolable.h"
#include "UGameAbilitySystemBootstrap.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class APawn;

/**
 * @brief 取 ASC 委托（游戏层实现：返回宿主对象的 AbilitySystemComponent）
 */
DECLARE_DELEGATE_RetVal(UAbilitySystemComponent*, FResolveAbilitySystemComponent);

/**
 * @brief 写初值委托（游戏层实现：准备并注册属性集、写入初值，含随静态数据派生的字段）
 */
DECLARE_DELEGATE_OneParam(FWriteInitialAttributes, UAbilitySystemComponent*);

/**
 * @brief 数据就绪判定委托（游戏层实现：返回静态数据是否已就绪；未注入视为恒就绪）
 */
DECLARE_DELEGATE_RetVal(bool, FQueryDataReady);

/**
 * @brief GAS 引导通用组件（游戏无关）
 */
UCLASS(ClassGroup = (Game), meta = (BlueprintSpawnableComponent))
class GAMEGAMEPLAY_API UGameAbilitySystemBootstrap : public UActorComponent, public IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：关闭组件 Tick 并用默认配置初始化重试闸门
     */
    UGameAbilitySystemBootstrap();

    /**
     * @brief 组件启动生命周期回调：同步重试闸门配置并尝试从拥有者兜底引导
     */
    void BeginPlay() override;

    /**
     * @brief 组件销毁生命周期回调：清理重试定时器并撤销本组件授予的全部默认能力，避免句柄残留
     * @param EndPlayReason 结束运行的原因
     */
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /**
     * @brief 从对象池激活时重置重试门累计次数并同步最新重试配置
     */
    void OnAcquireFromPool_Implementation() override;

    /**
     * @brief 归还对象池时清理定时器、撤销已授予能力并重置握手状态
     */
    void OnReleaseToPool_Implementation() override;

    /**
     * @brief 注入取 ASC 委托（须在引导前完成）
     * @param InDelegate 取 ASC 委托
     */
    void SetResolveAbilitySystemComponentDelegate(const FResolveAbilitySystemComponent& InDelegate);

    /**
     * @brief 注入写初值委托（须在引导前完成）
     * @param InDelegate 写初值委托
     */
    void SetWriteInitialAttributesDelegate(const FWriteInitialAttributes& InDelegate);

    /**
     * @brief 注入数据就绪判定委托
     * @param InDelegate 数据就绪判定委托
     */
    void SetDataReadyQueryDelegate(const FQueryDataReady& InDelegate);

    /**
     * @brief 配置引导完成后权威授予的默认能力类列表
     * @param InDefaultAbilities 默认能力类数组
     */
    void SetDefaultAbilities(TArray<TSubclassOf<UGameplayAbility>> InDefaultAbilities);

    /**
     * @brief 配置重试间隔与初始化/复制最大重试次数（立即同步至内部闸门）
     * @param InRetryIntervalSeconds 重试时间间隔（秒，下限 0.01）
     * @param InMaxInitRetries 兜底初始化最大重试次数（下限 0）
     * @param InMaxReplicationRetries 等待复制就绪最大重试次数（下限 0）
     */
    void ConfigureRetryPolicy(float InRetryIntervalSeconds, int32 InMaxInitRetries, int32 InMaxReplicationRetries);

    /**
     * @brief 统一幂等初始化入口（服务器与本地客户端均可调用，宿主握手钩子驱动）
     * @param InAvatar 当前附身的 Avatar（作为 ASC 的 AvatarActor）
     */
    void EnsureInitialized(AActor* InAvatar);

    /**
     * @brief 数据就绪通知（仅推进已完成 ActorInfo 握手的引导，未握手时等待 EnsureInitialized）
     */
    void NotifyDataReady();

    /**
     * @brief 兜底自愈入口（仅极端离线 / 重连脱节时触发，宿主 BeginPlay 调用）
     */
    void TryInitializeFromOwner();

    /**
     * @brief 查询初值是否已写入
     * @return 已写入返回 true
     */
    [[nodiscard]] bool IsInitialAttributesWritten() const noexcept {
        return bInitialAttributesWritten;
    }

    /**
     * @brief 查询默认能力是否已授予
     * @return 已授予返回 true
     */
    [[nodiscard]] bool IsDefaultAbilitiesGranted() const noexcept {
        return bDefaultAbilitiesGranted;
    }

protected:
    /**
     * @brief 引导完成后向 ASC 权威授予的默认能力规格列表
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS")
    TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

    /**
     * @brief 等待 ASC 复制通道就绪的最大重试次数
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxReplicationRetries = 8;

    /**
     * @brief 宿主兜底初始化寻找有效 Avatar 的最大重试次数
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxInitRetries = 60;

    /**
     * @brief 引导与复制重试的时间间隔（秒）
     */
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0.01"))
    float RetryIntervalSeconds = 0.05f;

private:
    /**
     * @brief 按组件配置同步初始化与 ASC 复制重试门的间隔和次数上限
     */
    void ConfigureRetryGates();

    /**
     * @brief 撤销本组件已授予给当前 ASC 的默认能力并清空句柄列表
     */
    void ClearGrantedAbilities();

    /**
     * @brief 从组件拥有者解析兜底引导使用的 Pawn Avatar
     * @return 拥有者自身或其 PlayerState 关联的 Pawn；无法解析时返回 nullptr
     */
    [[nodiscard]] APawn* ResolveOwnerAvatar() const;

    /**
     * @brief 对已解析的非空 ASC 与 Avatar 执行幂等 ActorInfo 握手与权威阶段推进
     * @param InAbilitySystemComponent 已解析的非空 ASC
     * @param InAvatar 已解析的非空 Avatar
     */
    void InitializeWithResolvedASC(UAbilitySystemComponent* InAbilitySystemComponent, AActor* InAvatar);

    /**
     * @brief 调度下一次兜底自愈定时器重试，超过次数上限则输出告警并中止
     * @param InTimeoutReason 超时日志中说明缺失对象的文本
     */
    void ScheduleInitRetry(const TCHAR* InTimeoutReason);

    /**
     * @brief 在 ActorInfo 已初始化且数据就绪后写入初值并推进默认能力授予
     */
    void AdvanceAuthoritativeInitialization();

    /**
     * @brief 向当前已完成握手的 ASC 权威授予默认能力并跟踪句柄
     */
    void GrantDefaultAbilities();

    TWeakObjectPtr<AActor> CurrentAvatar;
    TWeakObjectPtr<UAbilitySystemComponent> CurrentASC;
    TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

    FResolveAbilitySystemComponent ResolveAbilitySystemComponent;
    FWriteInitialAttributes WriteInitialAttributes;
    FQueryDataReady DataReadyQuery;

    FTimerHandle InitRetryTimerHandle;
    FTimerHandle ReplicationRetryTimerHandle;
    bool bInitialAttributesWritten = false;
    bool bDefaultAbilitiesGranted = false;

    FBootstrapRetryGate InitRetryGate;
    FBootstrapRetryGate ReplicationRetryGate;
};
