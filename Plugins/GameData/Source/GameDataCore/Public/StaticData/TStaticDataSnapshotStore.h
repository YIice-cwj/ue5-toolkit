/**
 * @file TStaticDataSnapshotStore.h
 * @brief 域无关泛型静态数据快照 RCU 存储器模板（无锁版本读 + 读写分离原子发布 + 重载门闩）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/DataErrorCodes.h"
#include "Misc/ScopeRWLock.h"
#include "Types/TGameResult.h"

#include <atomic>
#include <concepts>

/**
 * @brief 约束静态数据快照类型必须暴露可转为 uint32 的 DataVersion 字段
 * @tparam TSnapshot 快照载体类型
 */
template <typename TSnapshot>
concept CStaticDataSnapshot = requires(const TSnapshot& Snapshot) {
    { Snapshot.DataVersion } -> std::convertible_to<uint32>;
};

/**
 * @brief 泛型静态数据快照 RCU 存储器（支持多读并发获取与单写者原子发布，禁止拷贝与移动）
 * @tparam TSnapshot 满足 CStaticDataSnapshot 约束的不可变快照类型
 */
template <CStaticDataSnapshot TSnapshot>
class TStaticDataSnapshotStore {
public:
    /**
     * @brief 跨异步线程安全流转的重载门闩释放票据（绑定当轮重载代次 Token，支持显式提前释放与析构兜底幂等释放，免疫 ABA 重入误释放）
     */
    class FReloadLatchTicket {
    public:
        /**
         * @brief 绑定底层原子门闩与当轮重载代次 Token 构造票据
         * @param InLatch 共享原子门闩代次引用
         * @param InToken 本票据对应的重载代次 Token（非 0）
         */
        FReloadLatchTicket(TSharedRef<std::atomic<uint64>, ESPMode::ThreadSafe> InLatch, uint64 InToken) noexcept
            : Latch(MoveTemp(InLatch))
            , Token(InToken) {
        }

        /**
         * @brief 析构时若尚未显式释放则自动归还重载门闩（防止异步任务被丢弃导致门闩死锁）
         */
        ~FReloadLatchTicket() {
            Release();
        }

        FReloadLatchTicket(const FReloadLatchTicket&) = delete;
        FReloadLatchTicket& operator=(const FReloadLatchTicket&) = delete;

        /**
         * @brief 按代次 Token 幂等释放重载门闩（仅当底层门闩仍持有本轮 Token 时才 CAS 归零，绝不误清重入的新一轮重载门闩）
         */
        void Release() noexcept {
            if (!bReleased.exchange(true, std::memory_order_acq_rel) && Token != 0) {
                uint64 Expected = Token;
                Latch->compare_exchange_strong(Expected, 0, std::memory_order_acq_rel, std::memory_order_acquire);
            }
        }

    private:
        TSharedRef<std::atomic<uint64>, ESPMode::ThreadSafe> Latch;
        uint64 Token{0};
        std::atomic<bool> bReleased{false};
    };

    TStaticDataSnapshotStore() = default;
    ~TStaticDataSnapshotStore() = default;
    TStaticDataSnapshotStore(const TStaticDataSnapshotStore&) = delete;
    TStaticDataSnapshotStore& operator=(const TStaticDataSnapshotStore&) = delete;
    TStaticDataSnapshotStore(TStaticDataSnapshotStore&&) = delete;
    TStaticDataSnapshotStore& operator=(TStaticDataSnapshotStore&&) = delete;

    /**
     * @brief 瞬态读锁拷贝在线快照共享指针后立即出锁
     * @return 当前在线快照只读共享指针，尚未发布过时返回空指针
     */
    [[nodiscard]] TSharedPtr<const TSnapshot> Acquire() const;

    /**
     * @brief 无锁获取当前在线快照版本号（避免加读锁与共享指针原子引用计数开销）
     * @return 当前数据版本号，尚未发布过时返回 0
     */
    [[nodiscard]] uint32 GetVersion() const noexcept;

    /**
     * @brief 原子发布新快照并在写锁外析构旧快照（消除锁内析构延迟）
     * @param NextSnapshot 待发布的新快照共享指针
     */
    void Publish(TSharedPtr<const TSnapshot> NextSnapshot);

    /**
     * @brief 通过 CAS 抢占重载门闩，保证同一时刻仅一个写者进入提交流程
     * @return 抢占成功返回 Success，已有重载进行中返回 ReloadInProgress 失败
     */
    [[nodiscard]] TGameStatusResult<void> TryBeginReload();

    /**
     * @brief 创建绑定当前重载门闩的线程安全 RAII 释放票据（供异步预热链路捕获）
     * @return 线程安全票据共享引用
     */
    [[nodiscard]] TSharedRef<FReloadLatchTicket, ESPMode::ThreadSafe> MakeReloadLatchTicket() const;

    /**
     * @brief 释放重载门闩，允许后续提交流程进入
     */
    void EndReload() noexcept;

    /**
     * @brief 查询当前是否正处于重载门闩占用状态
     * @return 正在重载返回 true
     */
    [[nodiscard]] bool IsReloadInProgress() const noexcept;

private:
    mutable FRWLock SnapshotLock;
    TSharedPtr<const TSnapshot> OnlineSnapshot;
    std::atomic<uint32> CachedVersion{0};
    std::atomic<uint64> NextReloadToken{0};
    TSharedRef<std::atomic<uint64>, ESPMode::ThreadSafe> ReloadLatch =
        MakeShared<std::atomic<uint64>, ESPMode::ThreadSafe>(0);
};

template <CStaticDataSnapshot TSnapshot>
TSharedPtr<const TSnapshot> TStaticDataSnapshotStore<TSnapshot>::Acquire() const {
    FReadScopeLock ReadLock(SnapshotLock);
    return OnlineSnapshot;
}

template <CStaticDataSnapshot TSnapshot>
uint32 TStaticDataSnapshotStore<TSnapshot>::GetVersion() const noexcept {
    return CachedVersion.load(std::memory_order_acquire);
}

template <CStaticDataSnapshot TSnapshot>
void TStaticDataSnapshotStore<TSnapshot>::Publish(TSharedPtr<const TSnapshot> NextSnapshot) {
    const uint32 NextVersion = NextSnapshot.IsValid() ? static_cast<uint32>(NextSnapshot->DataVersion) : 0u;
    TSharedPtr<const TSnapshot> RetiredSnapshot;
    {
        FWriteScopeLock WriteLock(SnapshotLock);
        RetiredSnapshot = MoveTemp(OnlineSnapshot);
        OnlineSnapshot = MoveTemp(NextSnapshot);
        CachedVersion.store(NextVersion, std::memory_order_release);
    }
}

template <CStaticDataSnapshot TSnapshot>
TGameStatusResult<void> TStaticDataSnapshotStore<TSnapshot>::TryBeginReload() {
    uint64 Expected = 0;
    uint64 Token = NextReloadToken.fetch_add(1, std::memory_order_relaxed) + 1;
    if (Token == 0) {
        Token = NextReloadToken.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    if (!ReloadLatch->compare_exchange_strong(Expected, Token, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return TGameStatusResult<void>::Failure(
            Game::ModuleId::Data,
            Game::DataErrorCodes::ReloadInProgress,
            TEXT("已有重载进行中")
        );
    }
    return TGameStatusResult<void>::Success();
}

template <CStaticDataSnapshot TSnapshot>
TSharedRef<typename TStaticDataSnapshotStore<TSnapshot>::FReloadLatchTicket, ESPMode::ThreadSafe>
TStaticDataSnapshotStore<TSnapshot>::MakeReloadLatchTicket() const {
    return MakeShared<FReloadLatchTicket, ESPMode::ThreadSafe>(
        ReloadLatch,
        ReloadLatch->load(std::memory_order_acquire)
    );
}

template <CStaticDataSnapshot TSnapshot>
void TStaticDataSnapshotStore<TSnapshot>::EndReload() noexcept {
    ReloadLatch->store(0, std::memory_order_release);
}

template <CStaticDataSnapshot TSnapshot>
bool TStaticDataSnapshotStore<TSnapshot>::IsReloadInProgress() const noexcept {
    return ReloadLatch->load(std::memory_order_acquire) != 0;
}
