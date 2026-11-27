/**
 * @file GameFeedbackInternalHelpers.h
 * @brief 模块内部共享无状态辅助函数（世界获取、命中点解析、组件装配与定义回退解析）
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Feedback/GameFeedbackSignal.h"
#include "GameFramework/Actor.h"
#include "System/GameFeedbackDefinitionProvider.h"

class UGameFeedbackDefinition;

namespace GameFeedback::Internal {

/**
 * @brief 安全解析上下文对象所属的 UWorld（兼容无 GEngine 的早期或隔离上下文）
 * @param WorldContextObject 世界上下文对象
 * @return 世界指针；上下文无效时返回 nullptr
 */
[[nodiscard]] inline UWorld* GetWorldFromContextSafe(const UObject* WorldContextObject) {
    if (!WorldContextObject) {
        return nullptr;
    }
    if (GEngine != nullptr) {
        if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
            return World;
        }
    }
    return WorldContextObject->GetWorld();
}

/**
 * @brief 尝试从信号中提取有效世界空间作用点（优先有效命中点，其次受击目标位置）
 * @param Signal 表现信号
 * @param OutLocation 输出世界空间坐标
 * @return 成功提取有效坐标返回 true；无命中信息且目标失效时返回 false
 */
[[nodiscard]] inline bool TryResolveImpactLocation(const FGameFeedbackSignal& Signal, FVector& OutLocation) {
    if (Signal.HitResult.bBlockingHit || !Signal.HitResult.ImpactPoint.IsNearlyZero()) {
        OutLocation = Signal.HitResult.ImpactPoint;
        return true;
    }
    if (const AActor* TargetActor = Signal.Target.Get()) {
        OutLocation = TargetActor->GetActorLocation();
        return true;
    }
    OutLocation = FVector::ZeroVector;
    return false;
}

/**
 * @brief 从信号提取世界空间作用点（无有效来源时返回零向量）
 * @param Signal 表现信号
 * @return 世界空间坐标
 */
[[nodiscard]] inline FVector ResolveImpactLocation(const FGameFeedbackSignal& Signal) {
    FVector Location = FVector::ZeroVector;
    (void)TryResolveImpactLocation(Signal, Location);
    return Location;
}

/**
 * @brief 确保目标 Actor 上存在指定类型的组件（缺失时动态创建、挂载并注册）
 * @tparam TComponent 目标组件类型
 * @param Owner 宿主 Actor
 * @return 已存在或新建的组件指针；宿主为空时返回 nullptr
 */
template <typename TComponent>
TComponent* EnsureComponent(AActor* Owner) {
    if (!Owner) {
        return nullptr;
    }

    if (TComponent* Existing = Owner->FindComponentByClass<TComponent>()) {
        return Existing;
    }

    TComponent* Component = NewObject<TComponent>(Owner);
    Owner->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

/**
 * @brief 解析本次表现生效的 Definition（优先复用已传入/预解析的 Override，否则向 Provider 查询）
 * @param WorldContextObject 世界上下文对象
 * @param Signal 表现信号
 * @param OverrideDefinition 已指定的覆盖或预解析资产
 * @return 生效的资产指针；未解析到时返回 nullptr
 */
[[nodiscard]] inline const UGameFeedbackDefinition* ResolveDefinitionForSignal(
    const UObject* WorldContextObject,
    const FGameFeedbackSignal& Signal,
    const UGameFeedbackDefinition* OverrideDefinition) {
    if (OverrideDefinition != nullptr) {
        return OverrideDefinition;
    }
    if (UGameFeedbackDefinitionProvider* Provider = UGameFeedbackDefinitionProvider::GetPtr(WorldContextObject)) {
        return Provider->Resolve(Signal);
    }
    return nullptr;
}

}  // namespace GameFeedback::Internal
