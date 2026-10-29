/**
 * @file IGameStatValueSource.h
 * @brief 泛型数值源接口（游戏无关 · Tag 驱动 · 统一外部数值源契约）
 *
 * 设计思路：
 * 抽象统一的属性查询契约，使无论是 GAS AttributeSet、原生 Pawn 属性表、蓝图角色、还是外部配置驱动的数据源，
 * 均可通过本接口为 UGameStatComponent 提供实时属性数值，抹平数据源边界，彻底杜绝数据源撕裂。
 */

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"

#include "GameGameplayCore.h"
#include "IGameStatValueSource.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UGameStatValueSource : public UInterface {
    GENERATED_BODY()
};

/**
 * @brief 泛型数值源接口
 */
class GAMEGAMEPLAYCORE_API IGameStatValueSource {
    GENERATED_BODY()

public:
    /**
     * @brief 获取指定属性的当前数值
     * @param InStatTag 属性标签
     * @return 当前数值
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Stat")
    float GetStatCurrent(const FGameplayTag& InStatTag) const;

    /**
     * @brief 获取指定属性的上限数值
     * @param InStatTag 属性标签
     * @return 上限数值
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Stat")
    float GetStatMax(const FGameplayTag& InStatTag) const;

    virtual float GetStatCurrent_Implementation(const FGameplayTag& InStatTag) const {
        return 0.0f;
    }

    virtual float GetStatMax_Implementation(const FGameplayTag& InStatTag) const {
        return 100.0f;
    }
};

