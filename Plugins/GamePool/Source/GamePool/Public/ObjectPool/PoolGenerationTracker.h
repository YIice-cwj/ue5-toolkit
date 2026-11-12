// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

/**
 * @brief 池化对象世代代数追踪器（防悬空句柄与 Use-After-Return 窜号访问）
 */
class FPoolGenerationTracker {
public:
    /**
     * @brief 获取并递增指定 Actor 借出世代版本
     * @param Actor 目标 Actor 实例
     * @return 最新的有效世代代数
     */
    uint32 GetOrIncrementGeneration(AActor* Actor) {
        if (Actor == nullptr) {
            return 0;
        }
        TWeakObjectPtr<AActor> Key(Actor);
        uint32& Gen = ActorGenerations.FindOrAdd(Key, 0);
        ++Gen;
        if (Gen == 0) {
            Gen = 1;
        }
        return Gen;
    }

    /**
     * @brief 获取并递增指定 UObject 借出世代版本
     * @param Object 目标 UObject 实例
     * @return 最新的有效世代代数
     */
    uint32 GetOrIncrementGeneration(UObject* Object) {
        if (Object == nullptr) {
            return 0;
        }
        TWeakObjectPtr<UObject> Key(Object);
        uint32& Gen = ObjectGenerations.FindOrAdd(Key, 0);
        ++Gen;
        if (Gen == 0) {
            Gen = 1;
        }
        return Gen;
    }

    /**
     * @brief 获取 Actor 实例当前的借出世代代数
     * @param Actor 目标 Actor 实例
     * @return 当前代数，未注册返回 0
     */
    [[nodiscard]] uint32 FindGeneration(const AActor* Actor) const {
        if (Actor == nullptr) {
            return 0;
        }
        const uint32* FoundGen = ActorGenerations.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
        return FoundGen != nullptr ? *FoundGen : 0;
    }

    /**
     * @brief 获取 UObject 实例当前的借出世代代数
     * @param Object 目标 UObject 实例
     * @return 当前代数，未注册返回 0
     */
    [[nodiscard]] uint32 FindGeneration(const UObject* Object) const {
        if (Object == nullptr) {
            return 0;
        }
        const uint32* FoundGen = ObjectGenerations.Find(TWeakObjectPtr<UObject>(const_cast<UObject*>(Object)));
        return FoundGen != nullptr ? *FoundGen : 0;
    }

    /**
     * @brief 校验 Actor 句柄世代代数是否依然处于有效借出状态
     * @param Actor 目标 Actor 实例
     * @param InGeneration 待比对代数
     * @return 活跃且代数一致返回 true
     */
    [[nodiscard]] bool ValidateGeneration(const AActor* Actor, uint32 InGeneration) const {
        if (Actor == nullptr || InGeneration == 0) {
            return false;
        }
        const uint32* FoundGen = ActorGenerations.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Actor)));
        return FoundGen != nullptr && *FoundGen == InGeneration;
    }

    /**
     * @brief 校验 UObject 句柄世代代数是否依然处于有效借出状态
     * @param Object 目标 UObject 实例
     * @param InGeneration 待比对代数
     * @return 活跃且代数一致返回 true
     */
    [[nodiscard]] bool ValidateGeneration(const UObject* Object, uint32 InGeneration) const {
        if (Object == nullptr || InGeneration == 0) {
            return false;
        }
        const uint32* FoundGen = ObjectGenerations.Find(TWeakObjectPtr<UObject>(const_cast<UObject*>(Object)));
        return FoundGen != nullptr && *FoundGen == InGeneration;
    }

    /**
     * @brief 使指定的 Actor 世代版本失效
     * @param Actor 目标 Actor 实例
     */
    void InvalidateGeneration(AActor* Actor) {
        if (Actor != nullptr) {
            TWeakObjectPtr<AActor> Key(Actor);
            if (uint32* Gen = ActorGenerations.Find(Key)) {
                ++(*Gen);
                if (*Gen == 0) {
                    *Gen = 1;
                }
            }
        }
    }

    /**
     * @brief 使指定的 UObject 世代版本失效
     * @param Object 目标 UObject 实例
     */
    void InvalidateGeneration(UObject* Object) {
        if (Object != nullptr) {
            TWeakObjectPtr<UObject> Key(Object);
            if (uint32* Gen = ObjectGenerations.Find(Key)) {
                ++(*Gen);
                if (*Gen == 0) {
                    *Gen = 1;
                }
            }
        }
    }

    /**
     * @brief 清除全部代数映射记录
     */
    void Reset() {
        ActorGenerations.Empty();
        ObjectGenerations.Empty();
    }

private:
    TMap<TWeakObjectPtr<AActor>, uint32> ActorGenerations;
    TMap<TWeakObjectPtr<UObject>, uint32> ObjectGenerations;
};
