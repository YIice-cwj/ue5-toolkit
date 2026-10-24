// Automatically generated static data snapshot RCU store implementation. Do not edit manually.
#include "Generated/Data/StaticData/FStaticDataSnapshotStore.h"

#include "Generated/Data/StaticData/FStaticDataSnapshot.h"

/**
 * @brief 瞬态读锁拷贝在线快照指针后出锁
 * @return 当前在线快照共享指针，尚未发布过返回空
 */
TSharedPtr<const FStaticDataSnapshot> FStaticDataSnapshotStore::Acquire() const {
    FReadScopeLock ReadLock(SnapshotLock);
    return OnlineSnapshot;
}

/**
 * @brief 取当前数据版本号（经 Acquire 读快照内字段）
 * @return 版本号，尚未发布过返回 0
 */
uint32 FStaticDataSnapshotStore::GetVersion() const {
    const TSharedPtr<const FStaticDataSnapshot> Snapshot = Acquire();
    return Snapshot.IsValid() ? Snapshot->DataVersion : 0;
}

/**
 * @brief 原子发布新快照
 * @param NextSnapshot 新快照共享指针（所有权转移给存储）
 *
 * 实现思路：写锁内仅两次指针搬移，旧快照先腾挪到局部变量，出锁后随局部变量自然析构
 */
void FStaticDataSnapshotStore::Publish(TSharedPtr<const FStaticDataSnapshot> NextSnapshot) {
    TSharedPtr<const FStaticDataSnapshot> RetiredSnapshot;
    {
        FWriteScopeLock WriteLock(SnapshotLock);
        RetiredSnapshot = MoveTemp(OnlineSnapshot);
        OnlineSnapshot = MoveTemp(NextSnapshot);
    }
}

/**
 * @brief CAS 抢占重载门闩
 * @return 抢占成功返回 true，已有重载进行中返回 false
 */
bool FStaticDataSnapshotStore::TryBeginReload() {
    bool bExpected = false;
    return bReloadInProgress.compare_exchange_strong(bExpected, true);
}

/**
 * @brief 释放重载门闩
 */
void FStaticDataSnapshotStore::EndReload() {
    bReloadInProgress.store(false);
}
