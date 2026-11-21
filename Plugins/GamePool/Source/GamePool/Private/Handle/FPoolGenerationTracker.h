/**
 * @file FPoolGenerationTracker.h
 * @brief 池化对象世代代数内部追踪器（直接以 FWeakObjectPtr 索引防悬空与 Use-After-Return 窜号）
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

/**
 * @brief 池化对象世代代数内部追踪器
 */
class FPoolGenerationTracker {
public:
    /**
     * @brief 获取并递增指定对象的借出世代版本（0 为保留无效代数，回绕时自动跳过 0）
     * @param Object 目标对象实例
     * @return 最新的有效世代代数
     */
    uint32 GetOrIncrementGeneration(const UObject* Object) {
        if (Object == nullptr) {
            return 0u;
        }
        uint32& Gen = Generations.FindOrAdd(FWeakObjectPtr(Object), 0u);
        return AdvanceGeneration(Gen);
    }

    /**
     * @brief 获取对象当前的借出世代代数
     * @param Object 目标对象实例
     * @return 当前代数，未注册返回 0
     */
    [[nodiscard]] uint32 FindGeneration(const UObject* Object) const {
        if (Object == nullptr) {
            return 0u;
        }
        const uint32* FoundGen = Generations.Find(FWeakObjectPtr(Object));
        return FoundGen != nullptr ? *FoundGen : 0u;
    }

    /**
     * @brief 直接基于 FWeakObjectPtr 校验句柄世代代数是否仍匹配（零额外 GUObjectArray 反向查表）
     * @param ObjectWeak 目标对象弱引用
     * @param InGeneration 待比对世代代数
     * @return 弱引用有效且代数一致返回 true
     */
    [[nodiscard]] bool ValidateGeneration(const FWeakObjectPtr& ObjectWeak, uint32 InGeneration) const {
        if (InGeneration == 0u || !ObjectWeak.IsValid()) {
            return false;
        }
        const uint32* FoundGen = Generations.Find(ObjectWeak);
        return FoundGen != nullptr && *FoundGen == InGeneration;
    }

    /**
     * @brief 使指定的对象世代版本失效（递增代数，导致所有历史句柄立即失效）
     * @param Object 目标对象实例
     */
    void InvalidateGeneration(const UObject* Object) {
        if (Object == nullptr) {
            return;
        }
        if (uint32* Gen = Generations.Find(FWeakObjectPtr(Object))) {
            AdvanceGeneration(*Gen);
        }
    }

    /**
     * @brief 彻底移除指定对象的世代记录（在对象销毁时调用，防止死键堆积）
     * @param Object 目标对象实例
     */
    void RemoveGeneration(const UObject* Object) {
        if (Object == nullptr) {
            return;
        }
        Generations.Remove(FWeakObjectPtr(Object));
    }

    /**
     * @brief 清理映射表中已失效的弱引用死键，抑制长期运行哈希表退化
     * @return 本次移除的死键数量
     */
    int32 CompactStaleEntries() {
        int32 RemovedCount = 0;
        for (auto It = Generations.CreateIterator(); It; ++It) {
            if (!It.Key().IsValid()) {
                It.RemoveCurrent();
                ++RemovedCount;
            }
        }
        return RemovedCount;
    }

    /**
     * @brief 清除全部代数映射记录
     */
    void Reset() {
        Generations.Empty();
    }

private:
    static uint32 AdvanceGeneration(uint32& Gen) noexcept {
        ++Gen;
        if (Gen == 0u) {
            Gen = 1u;
        }
        return Gen;
    }

private:
    TMap<FWeakObjectPtr, uint32> Generations;
};
