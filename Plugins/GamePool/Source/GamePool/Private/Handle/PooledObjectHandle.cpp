/**
 * @file PooledObjectHandle.cpp
 * @brief 受管安全句柄私有子系统桥接函数实现
 */

#include "Handle/TPooledObjectHandle.h"
#include "Subsystem/UObjectPoolSubsystem.h"

namespace Game::Pool::Private {

/**
 * @brief 校验受管句柄的世代版本是否与对象池子系统记录一致
 * @param Subsystem 对象池子系统指针
 * @param ObjectWeak 目标对象弱引用
 * @param Generation 句柄持有的世代序号
 * @return 世代匹配且有效返回 true
 */
bool ValidatePooledHandle(
    const UObjectPoolSubsystem* Subsystem,
    const FWeakObjectPtr& ObjectWeak,
    const uint32 Generation
) {
    return Subsystem->ValidateHandleGeneration(ObjectWeak, Generation);
}

/**
 * @brief 将受管 Actor 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Actor 待归还 Actor
 */
void ReleasePooledActor(UObjectPoolSubsystem* Subsystem, AActor* Actor) {
    (void)Subsystem->ReleaseActor(Actor);
}

/**
 * @brief 将受管 UObject 归还给对象池子系统
 * @param Subsystem 对象池子系统指针
 * @param Object 待归还 UObject
 */
void ReleasePooledObject(UObjectPoolSubsystem* Subsystem, UObject* Object) {
    (void)Subsystem->ReleaseObject(Object);
}

}  // namespace Game::Pool::Private
