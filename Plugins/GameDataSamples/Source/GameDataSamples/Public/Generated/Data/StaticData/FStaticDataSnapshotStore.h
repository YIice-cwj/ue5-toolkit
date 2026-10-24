// Automatically generated static data snapshot RCU store. Do not edit manually.
#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeRWLock.h"

#include <atomic>

#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief 静态数据快照 RCU 存储（禁止拷贝与移动）
 */
class GAMEDATASAMPLES_API FStaticDataSnapshotStore {
public:
    FStaticDataSnapshotStore() = default;
    ~FStaticDataSnapshotStore() = default;
    FStaticDataSnapshotStore(const FStaticDataSnapshotStore&) = delete;
    FStaticDataSnapshotStore& operator=(const FStaticDataSnapshotStore&) = delete;
    FStaticDataSnapshotStore(FStaticDataSnapshotStore&&) = delete;
    FStaticDataSnapshotStore& operator=(FStaticDataSnapshotStore&&) = delete;

    /**
     * @brief 瞬态读锁拷贝在线快照指针后出锁
     * @return 当前在线快照共享指针，尚未发布过返回空
     */
    [[nodiscard]] TSharedPtr<const FStaticDataSnapshot> Acquire() const;

    /**
     * @brief 取当前数据版本号（经 Acquire 读快照内字段）
     * @return 版本号，尚未发布过返回 0
     */
    [[nodiscard]] uint32 GetVersion() const;

    /**
     * @brief 原子发布新快照
     * @param NextSnapshot 新快照共享指针（所有权转移给存储）
     */
    void Publish(TSharedPtr<const FStaticDataSnapshot> NextSnapshot);

    /**
     * @brief CAS 抢占重载门闩
     * @return 抢占成功返回 true，已有重载进行中返回 false
     */
    [[nodiscard]] bool TryBeginReload();

    /**
     * @brief 释放重载门闩
     */
    void EndReload();

private:
    mutable FRWLock SnapshotLock;
    TSharedPtr<const FStaticDataSnapshot> OnlineSnapshot;
    std::atomic<bool> bReloadInProgress{false};
};
