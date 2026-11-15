// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

/**
 * @brief 池化对象世代代数追踪器（防悬空句柄与 Use-After-Return 窜号访问）
 */
class FPoolGenerationTracker {
public:
    /**
     * @brief 获取并递增指定对象的借出世代版本
     * @param Object 目标对象实例
     * @return 最新的有效世代代数
     */
    uint32 GetOrIncrementGeneration(const UObject* Object) {
        if (Object == nullptr) {
            return 0;
        }
        TWeakObjectPtr<const UObject> Key(Object);
        uint32& Gen = Generations.FindOrAdd(Key, 0);
        ++Gen;
        if (Gen == 0) {
            Gen = 1;
        }
        return Gen;
    }

    /**
     * @brief 获取对象当前的借出世代代数
     * @param Object 目标对象实例
     * @return 当前代数，未注册返回 0
     */
    [[nodiscard]] uint32 FindGeneration(const UObject* Object) const {
        if (Object == nullptr) {
            return 0;
        }
        const uint32* FoundGen = Generations.Find(TWeakObjectPtr<const UObject>(Object));
        return FoundGen != nullptr ? *FoundGen : 0;
    }

    /**
     * @brief 校验对象句柄世代代数是否依然处于有效借出状态
     * @param Object 目标对象实例
     * @param InGeneration 待比对代数
     * @return 活跃且代数一致返回 true
     */
    [[nodiscard]] bool ValidateGeneration(const UObject* Object, uint32 InGeneration) const {
        if (Object == nullptr || InGeneration == 0) {
            return false;
        }
        const uint32* FoundGen = Generations.Find(TWeakObjectPtr<const UObject>(Object));
        return FoundGen != nullptr && *FoundGen == InGeneration;
    }

    /**
     * @brief 使指定的对象世代版本失效（递增代数，导致历史句柄失效）
     * @param Object 目标对象实例
     */
    void InvalidateGeneration(const UObject* Object) {
        if (Object != nullptr) {
            TWeakObjectPtr<const UObject> Key(Object);
            if (uint32* Gen = Generations.Find(Key)) {
                ++(*Gen);
                if (*Gen == 0) {
                    *Gen = 1;
                }
            }
        }
    }

    /**
     * @brief 彻底移除指定对象的世代记录（在对象彻底销毁时调用，防止死键堆积）
     * @param Object 目标对象实例
     */
    void RemoveGeneration(const UObject* Object) {
        if (Object != nullptr) {
            Generations.Remove(TWeakObjectPtr<const UObject>(Object));
        }
    }

    /**
     * @brief 清除全部代数映射记录
     */
    void Reset() {
        Generations.Empty();
    }

private:
    TMap<TWeakObjectPtr<const UObject>, uint32> Generations;
};
