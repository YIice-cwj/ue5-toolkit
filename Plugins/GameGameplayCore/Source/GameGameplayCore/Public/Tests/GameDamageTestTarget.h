// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Damage/FGameDamageContext.h"
#include "Damage/FGameDamageResult.h"
#include "Damage/IDamageable.h"

#include "GameDamageTestTarget.generated.h"

/**
 * @brief 自动化测试用受击目标
 */
UCLASS()
class GAMEGAMEPLAYCORE_API UGameDamageTestTarget : public UObject, public IDamageable {
    GENERATED_BODY()

public:
    /**
     * @brief 响应伤害分发测试实现
     * @param Context 伤害分发上下文
     * @return 打击反馈结果
     */
    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) override;
};

/**
 * @brief 自动化测试用默认实现受击目标
 */
UCLASS()
class GAMEGAMEPLAYCORE_API UGameDamageDefaultTestTarget : public UObject, public IDamageable {
    GENERATED_BODY()
};
