/**
 * @file FPinnedAssetStorage.h
 * @brief FGameAssetPinner 常驻保活条目与 FGCObject 强引用收集器内部存储定义
 */

#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Engine/StreamableManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeLock.h"
#include "Templates/UniquePtr.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#include "UObject/SoftObjectPath.h"

namespace Game::Asset::Private {

/**
 * @brief 单项常驻资产条目与内部存储容器（内含锁外构析的 FGCObject 强引用收集器）
 */
class FPinnedAssetStorage {
public:
    struct FEntry {
        TObjectPtr<UObject> Asset = nullptr;
        TSharedPtr<FStreamableHandle> Handle;
        int32 RefCount = 0;
    };

private:
    class FGCReferencer final : public FGCObject {
    public:
        explicit FGCReferencer(FPinnedAssetStorage& InOwner) noexcept
            : Owner(InOwner) {
        }

        void AddReferencedObjects(FReferenceCollector& Collector) override {
            FScopeLock ScopeLock(&Owner.Lock);
            for (TPair<FSoftObjectPath, FEntry>& Pair : Owner.PinnedAssets) {
                Collector.AddReferencedObject(Pair.Value.Asset);
            }
        }

        FString GetReferencerName() const override {
            return TEXT("FGameAssetPinner");
        }

    private:
        FPinnedAssetStorage& Owner;
    };

public:
    /**
     * @brief 在不持有 Storage.Lock 的前提下确保游戏线程唯一 GC 引用收集器已就绪（杜绝 AB-BA 锁序反转）
     */
    void EnsureGCReferencer() {
        if (bHasGCReferencer.load(std::memory_order_acquire) || !IsInGameThread() || !UObjectInitialized()) {
            return;
        }
        TUniquePtr<FGCReferencer> Candidate = MakeUnique<FGCReferencer>(*this);
        {
            FScopeLock ScopeLock(&Lock);
            if (!GCReferencer.IsValid()) {
                GCReferencer = MoveTemp(Candidate);
                bHasGCReferencer.store(true, std::memory_order_release);
            }
        }
    }

    /**
     * @brief 短锁分离并在锁外销毁 GC 引用收集器（仅在模块卸载阶段调用，杜绝 AB-BA 锁序反转）
     */
    void ResetGCReferencer() {
        TUniquePtr<FGCReferencer> DetachedReferencer;
        {
            FScopeLock ScopeLock(&Lock);
            DetachedReferencer = MoveTemp(GCReferencer);
            bHasGCReferencer.store(false, std::memory_order_release);
        }
    }

public:
    FCriticalSection Lock;
    TMap<FSoftObjectPath, FEntry> PinnedAssets;

private:
    TUniquePtr<FGCReferencer> GCReferencer;
    std::atomic<bool> bHasGCReferencer{false};
};

}  // namespace Game::Asset::Private
