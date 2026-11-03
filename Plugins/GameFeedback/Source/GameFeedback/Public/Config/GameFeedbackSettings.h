/**
 * @file GameFeedbackSettings.h
 * @brief 打击感表现中枢项目全局配置
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "UObject/SoftObjectPtr.h"

#include "GameFeedbackSettings.generated.h"

class UGameFeedbackRegistry;

/**
 * @brief 项目级反馈中枢全局配置（Project Settings -> Game -> Game Feedback）
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Game Feedback"))
class GAMEFEEDBACK_API UGameFeedbackSettings : public UDeveloperSettings {
    GENERATED_BODY()

public:
    /**
     * @brief 默认构造全局配置
     */
    UGameFeedbackSettings() = default;

    /**
     * @brief 获取配置所属分类名
     * @return 容器名
     */
    FName GetCategoryName() const override {
        return TEXT("Game");
    }

    /**
     * @brief 总功能热关断开关（风险应急回滚）
     */
    UPROPERTY(Config, EditAnywhere, Category = "General")
    bool bEnableFeedback = true;

    /**
     * @brief 全局表现映射注册表
     */
    UPROPERTY(Config, EditAnywhere, Category = "Registry")
    TSoftObjectPtr<UGameFeedbackRegistry> GlobalRegistry;

    /**
     * @brief 全局无障碍手柄触觉震动倍率
     */
    UPROPERTY(Config, EditAnywhere, Category = "Haptics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float GlobalHapticScale = 1.0f;

    /**
     * @brief 权威端（Listen Server / Dedicated Server）是否允许物理定格
     */
    UPROPERTY(Config, EditAnywhere, Category = "HitStop")
    bool bAllowAuthoritativeHitStop = false;

    /**
     * @brief 默认网络视距裁剪距离（厘米）
     */
    UPROPERTY(Config, EditAnywhere, Category = "Network")
    float DefaultNetworkCullDistance = 5000.0f;

    /**
     * @brief 单帧网络派发事件上限（0 表示不限流）
     */
    UPROPERTY(Config, EditAnywhere, Category = "Network", meta = (ClampMin = "0"))
    int32 MaxDispatchPerFrame = 32;
};

