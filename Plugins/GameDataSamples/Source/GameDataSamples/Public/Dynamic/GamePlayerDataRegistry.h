/**
 * @file GamePlayerDataRegistry.h
 * @brief 玩家动态持久化数据注册中枢
 */

#pragma once

#include "CoreMinimal.h"
#include "Dynamic/PlayerDataTypes.h"

#include "GamePlayerDataRegistry.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlayerProfileChanged, const FPlayerProfileData&);

/**
 * @brief 玩家动态持久化数据注册中枢
 */
UCLASS(BlueprintType)
class GAMEDATASAMPLES_API UGamePlayerDataRegistry : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：使用默认档案值初始化，避免在 CDO 构造期触发日志与动态广播
     */
    UGamePlayerDataRegistry();

    /**
     * @brief 获取当前玩家档案（只读引用，随 SetProfile / ResetToDefault 更新）
     * @return 当前档案引用
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Player")
    [[nodiscard]] const FPlayerProfileData& GetProfile() const;

    /**
     * @brief 更新当前玩家档案并广播变更事件
     * @param InProfile 新的玩家档案数据
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void SetProfile(const FPlayerProfileData& InProfile);

    /**
     * @brief 重置玩家档案为默认初始值并广播变更事件
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void ResetToDefault();

    /**
     * @brief 玩家档案变更原生多播委托
     */
    FOnPlayerProfileChanged OnProfileChanged;

private:
    UPROPERTY()
    FPlayerProfileData CurrentProfile;
};
