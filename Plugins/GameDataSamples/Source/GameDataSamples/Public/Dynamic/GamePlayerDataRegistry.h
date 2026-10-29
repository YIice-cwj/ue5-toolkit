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
     * @brief 玩家档案变更原生多播委托
     */
    FOnPlayerProfileChanged OnProfileChanged;

    /**
     * @brief 构造函数：把档案初始化为默认值
     */
    UGamePlayerDataRegistry();

    /**
     * @brief 获取当前玩家档案（只读引用，随 SetProfile / ResetToDefault 更新）
     * @return 当前档案引用
     */
    UFUNCTION(BlueprintPure, Category = "GameData|Player")
    const FPlayerProfileData& GetProfile() const { return CurrentProfile; }

    /**
     * @brief 整体覆盖玩家档案并广播 OnProfileChanged
     * @param InProfile 新档案
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void SetProfile(const FPlayerProfileData& InProfile);

    /**
     * @brief 重置为默认档案（等价 SetProfile(FPlayerProfileData())，同时广播 OnProfileChanged）
     */
    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void ResetToDefault();

private:
    UPROPERTY()
    FPlayerProfileData CurrentProfile;
};
