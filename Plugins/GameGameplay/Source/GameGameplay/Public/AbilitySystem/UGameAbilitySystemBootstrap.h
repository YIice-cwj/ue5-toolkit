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

#include "GameGameplay.h"
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
class GAMEGAMEPLAY_API UGameAbilitySystemBootstrap : public UActorComponent {
    GENERATED_BODY()

public:
    UGameAbilitySystemBootstrap();

    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
     * @brief 统一幂等初始化入口（服务器与本地客户端均可调用，宿主握手钩子驱动）
     * @param InAvatar 当前附身的 Avatar（作为 ASC 的 AvatarActor）
     */
    void EnsureInitialized(AActor* InAvatar);

    /**
     * @brief 数据就绪通知（游戏层订阅静态数据版本后回调，唤醒数据未就绪时挂起的引导）
     */
    void NotifyDataReady();

    /**
     * @brief 兜底自愈入口（仅极端离线 / 重连脱节时触发，宿主 BeginPlay 调用）
     */
    void TryInitializeFromOwner();

    /**
     * @brief 查询初值是否已写入
     */
    [[nodiscard]] bool IsInitialAttributesWritten() const noexcept {
        return bInitialAttributesWritten;
    }

    /**
     * @brief 查询默认能力是否已授予
     */
    [[nodiscard]] bool IsDefaultAbilitiesGranted() const noexcept {
        return bDefaultAbilitiesGranted;
    }

protected:
    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS")
    TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxReplicationRetries = 8;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0"))
    int32 MaxInitRetries = 60;

    UPROPERTY(EditDefaultsOnly, Category = "Game|GAS", meta = (ClampMin = "0.01"))
    float RetryIntervalSeconds = 0.05f;

private:
    [[nodiscard]] APawn* ResolveOwnerAvatar() const;
    [[nodiscard]] UAbilitySystemComponent* ResolveASC(UAbilitySystemComponent* InAbilitySystemComponent) const;
    void AdvanceAuthoritativeInitialization(UAbilitySystemComponent* InAbilitySystemComponent = nullptr);
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
    int32 ReplicationRetryCount = 0;
    int32 InitRetryCount = 0;
};
