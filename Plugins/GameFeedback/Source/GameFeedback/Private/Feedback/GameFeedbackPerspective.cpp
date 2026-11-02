// Copyright Epic Games, Inc. All Rights Reserved.

#include "Feedback/GameFeedbackPerspective.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace GameFeedback {

/**
 * @brief 判定指定本地玩家在本次表现中的观察视角
 * @param LocalPC 本地玩家控制器
 * @param Instigator 动作发起者
 * @param DamageCauser 伤害造成实体
 * @param Target 目标实体
 * @return 视角分类
 */
EGameFeedbackPerspective ResolvePerspective(
    const APlayerController* LocalPC,
    const AActor* Instigator,
    const AActor* DamageCauser,
    const AActor* Target)
{
    if (!LocalPC) {
        return EGameFeedbackPerspective::Bystander;
    }

    const APawn* LocalPawn = LocalPC->GetPawn();

    auto IsOwnedByLocal = [LocalPC, LocalPawn](const AActor* InActor) -> bool {
        int32 Depth = 0;
        const AActor* Curr = InActor;
        while (Curr && Depth++ < 5) {
            if (Curr == LocalPC || Curr == LocalPawn || Curr->GetInstigatorController() == LocalPC) {
                return true;
            }
            Curr = Curr->GetOwner();
        }
        return false;
    };

    if (IsOwnedByLocal(Instigator) || IsOwnedByLocal(DamageCauser)) {
        return EGameFeedbackPerspective::LocalAttacker;
    }

    if (IsOwnedByLocal(Target)) {
        return EGameFeedbackPerspective::LocalVictim;
    }

    return EGameFeedbackPerspective::Bystander;
}

}
