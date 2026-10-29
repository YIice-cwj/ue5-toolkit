/**
 * @file ANetSmokePawn.h
 * @brief 携带 Buff 组件的冒烟 Pawn（服务端施加 Buff）
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "ANetSmokePawn.generated.h"

class UGameBuffComponent;

/**
 * @brief 携带 Buff 组件的冒烟 Pawn（服务端施加 Buff）
 */
UCLASS()
class ANetSmokePawn : public APawn {
    GENERATED_BODY()

public:
    /**
     * @brief 构造函数：创建 Buff 组件
     */
    ANetSmokePawn();

protected:
    /**
     * @brief 仅服务端施加带状态标签的 Buff，并把施放结果写入 ServerResult.txt
     */
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category = "NetSmoke")
    TObjectPtr<UGameBuffComponent> BuffComponent;
};

