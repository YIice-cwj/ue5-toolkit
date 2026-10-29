/**
 * @file GamePlayerProfileSaveAdapter.h
 * @brief 玩家档案存档适配器
 */

#pragma once

#include "CoreMinimal.h"
#include "Save/GameSaveArchive.h"

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
    /**
     * @brief 玩家档案存档段固定标识名称
     */
    static const FName SegmentName;

    /**
     * @brief 对象销毁生命周期回调：注销档案存档段并释放子系统与注册表引用
     */
    void BeginDestroy() override;

    /**
     * @brief 绑定存档子系统与玩家数据注册表，并注册档案存档段契约
     * @param InSaveSubsystem 通用存档子系统指针
     * @param InRegistry 玩家数据注册表中枢指针
     */
    UFUNCTION(BlueprintCallable, Category = "GameDataSamples|Save")
    void Setup(USaveGameSubsystem* InSaveSubsystem, UGamePlayerDataRegistry* InRegistry);

    /**
     * @brief 手动注销档案存档段并清空引用
     */
    UFUNCTION(BlueprintCallable, Category = "GameDataSamples|Save")
    void Teardown();

private:
    /**
     * @brief 绑定的通用存档子系统指针（Setup 成功后有效）
     */
    UPROPERTY()
    TObjectPtr<USaveGameSubsystem> SaveSubsystem;

    /**
     * @brief 关联的玩家数据注册表中枢指针
     */
    UPROPERTY()
    TObjectPtr<UGamePlayerDataRegistry> DataRegistry;
};
