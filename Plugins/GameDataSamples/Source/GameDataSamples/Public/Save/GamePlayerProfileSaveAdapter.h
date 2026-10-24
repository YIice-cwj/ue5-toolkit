/**
 * @file GamePlayerProfileSaveAdapter.h
 * @brief 玩家档案存档适配器
 */

#pragma once

#include "CoreMinimal.h"
#include "Persistence/GameSaveArchive.h"

#include "GamePlayerProfileSaveAdapter.generated.h"

class UGamePlayerDataRegistry;
class USaveGameSubsystem;

/**
 * @brief 玩家档案存档适配器（示例业务：把 FPlayerProfileData 注册为通用存档段）
 *
 * 通用层（USaveGameSubsystem）不感知任何玩家字段；本适配器位于示例插件，
 * 通过注册存档段把档案序列化接入通用存档流程，作为旧 Profile API 的迁移载体。
 */
UCLASS(BlueprintType)
class GAMEDATASAMPLES_API UGamePlayerProfileSaveAdapter : public UObject {
    GENERATED_BODY()

public:
    /** 玩家档案存档段名 */
    static const FName SegmentName;

    void BeginDestroy() override;

    /**
     * @brief 绑定存档子系统与玩家数据注册表，并注册档案存档段
     * @param InSaveSubsystem 通用存档子系统
     * @param InRegistry 玩家数据注册表
     */
    UFUNCTION(BlueprintCallable, Category = "GameDataSamples|Save")
    void Setup(USaveGameSubsystem* InSaveSubsystem, UGamePlayerDataRegistry* InRegistry);

    /**
     * @brief 注销档案存档段并释放引用
     */
    UFUNCTION(BlueprintCallable, Category = "GameDataSamples|Save")
    void Teardown();

private:
    /** 通用存档子系统（Setup 后有效） */
    UPROPERTY()
    TObjectPtr<USaveGameSubsystem> SaveSubsystem;

    /** 关联的玩家数据注册表 */
    UPROPERTY()
    TObjectPtr<UGamePlayerDataRegistry> DataRegistry;
};
