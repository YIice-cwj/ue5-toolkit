/**
 * @file IDamageable.h
 * @brief 可受伤目标泛型接口（框架层不依赖 MassEntity，可移植性不被尸潮方案绑架）
 *
 * 设计思路：伤害管线对上层只暴露 FGameDamageContext；实现者集合开放
 * （GAS 角色 / Mass 适配器 / 可破坏物 / 载具机关均实现本接口），
 * 攻击端对实现者名单保持无知，新增目标类型零修改。
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "Combat/FGameDamageContext.h"
#include "Combat/FGameDamageResult.h"

#include "IDamageable.generated.h"

/**
 * @brief 可受伤目标接口的 UINTERFACE 反射声明
 */
UINTERFACE(MinimalAPI, BlueprintType, Blueprintable)
class UDamageable : public UInterface {
    GENERATED_BODY()
};

/**
 * @brief 可受伤目标接口
 */
class GAMEGAMEPLAYCORE_API IDamageable {
    GENERATED_BODY()

public:

    /**
     * @brief 应用一次伤害分发并同步返回打击反馈
     * @param Context 伤害分发上下文
     * @return 打击反馈（同步链路：属性结算在返回前完成）
     */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Game|Damage")
    FGameDamageResult ApplyDamageContext(const FGameDamageContext& Context);

    virtual FGameDamageResult ApplyDamageContext_Implementation(const FGameDamageContext& Context) {
        return FGameDamageResult();
    }
};
