/**
 * @file UGameDamageDefaultTestTarget.h
 * @brief 私有自动化测试用 IDamageable 默认空实现受击目标声明
 */

#pragma once

#include "CoreMinimal.h"
#include "Damage/IDamageable.h"
#include "UObject/Object.h"

#include "UGameDamageDefaultTestTarget.generated.h"

/**
 * @brief 私有自动化测试用默认实现受击目标（验证 IDamageable 默认空实现回退）
 */
UCLASS(NotBlueprintable, Hidden, Transient)
class UGameDamageDefaultTestTarget : public UObject, public IDamageable {
    GENERATED_BODY()
};
