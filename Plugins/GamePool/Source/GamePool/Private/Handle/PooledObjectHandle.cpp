// Copyright Epic Games, Inc. All Rights Reserved.

#include "Handle/PooledObjectHandle.h"
#include "Subsystem/ObjectPoolSubsystem.h"

namespace Game::Pool::Private {

/**
 * @brief 校验受管句柄的世代版本是否与对象池子系统记录一致
 * @param Subsystem 对象池子系统指针
 * @param Object 目标对象指针
 * @param Generation 句柄持有的世代序号
 * @return 世代匹配且有效返回 true
 */
bool ValidatePooledHandle(const UObjectPoolSubsystem* Subsystem, const UObject* Object, uint32 Generation) {
    if (Subsystem == nullptr || Object == nullptr || Generation == 0) {
        return false;
    }
    return Subsystem->ValidateHandleGeneration(Object, Generation);
}

/**
 * @brief 将受管 Actor 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Actor 待归还 Actor
 */
void ReleasePooledActor(UObjectPoolSubsystem* Subsystem, AActor* Actor) {
    if (Subsystem != nullptr && Actor != nullptr) {
        Subsystem->ReleaseActor(Actor);
    }
}

/**
 * @brief 将受管 UObject 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Object 待归还 UObject
 */
void ReleasePooledObject(UObjectPoolSubsystem* Subsystem, UObject* Object) {
    if (Subsystem != nullptr && Object != nullptr) {
        Subsystem->ReleaseObject(Object);
    }
}

}  // namespace Game::Pool::Private
