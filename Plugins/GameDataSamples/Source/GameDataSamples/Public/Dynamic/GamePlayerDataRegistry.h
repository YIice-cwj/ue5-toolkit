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
    FOnPlayerProfileChanged OnProfileChanged;

    UGamePlayerDataRegistry();

    UFUNCTION(BlueprintPure, Category = "GameData|Player")
    const FPlayerProfileData& GetProfile() const { return CurrentProfile; }

    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void SetProfile(const FPlayerProfileData& InProfile);

    UFUNCTION(BlueprintCallable, Category = "GameData|Player")
    void ResetToDefault();

private:
    UPROPERTY()
    FPlayerProfileData CurrentProfile;
};
