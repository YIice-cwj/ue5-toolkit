/**
 * @file GameSaveTestTypes.h
 * @brief 存档子系统自动化测试专用 USaveGame 派生类
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"

#include "GameSaveTestTypes.generated.h"

/**
 * @brief 存档子系统测试用自定义存档对象（验证任意 USaveGame 类的存/取逃生口）
 */
UCLASS()
class GAMEDATACORE_API UGameSaveTestArchive : public USaveGame {
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 Value = 0;

    UPROPERTY()
    FString Label;
};

/**
 * @brief 存档子系统异步事件测试监听器
 */
UCLASS()
class GAMEDATACORE_API UGameSaveTestEventListener : public UObject {
    GENERATED_BODY()

public:
    /**
     * @brief 记录一次异步加载完成结果
     * @param SlotName 完成回调对应的槽位名称
     * @param bSuccess 加载是否成功
     */
    UFUNCTION()
    void RecordLoadComplete(const FString& SlotName, bool bSuccess);

    TArray<bool> CompletionResults;
};
