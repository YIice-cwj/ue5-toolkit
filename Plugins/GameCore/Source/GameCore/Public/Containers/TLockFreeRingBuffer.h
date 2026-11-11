/**
 * @file TLockFreeRingBuffer.h
 * @brief 固定容量无锁多生产者多消费者 (MPMC) 环形缓冲区模板（基于 Vyukov 序列号算法与缓存行隔离）
 */

#pragma once

#include "CoreMinimal.h"

#include <atomic>
#include <new>
#include <type_traits>

/**
 * @brief 固定容量泛型无锁环形缓冲区模板
 *
 * 采用 Dmitry Vyukov 的多生产者多消费者 (MPMC) 序列号算法：
 * 1. 生产者与消费者通过原子自增获取操作槽位，不同槽位并发无冲突；
 * 2. 槽位序列号协调生产与消费阶段，杜绝 ABA 问题；
 * 3. 头部与尾部独立排布。
 *
 * @note 本类内部持有定长内联数组 `FSlot Slots[Capacity]`。默认容量（1024）或载荷类型 `T` 较大时，
 *       占用内存可达数十 KB 以上。严禁在局部调用栈上直接分配大容量实例（易引发栈溢出），
 *       强烈推荐将其作为类成员变量、单例或通过 `MakeUnique` 在堆上分配持有。
 *
 * @tparam T 元素类型
 * @tparam Capacity 缓冲区固定容量（必须为大于 1 的 2 的整次幂）
 */
template <typename T, uint32 Capacity = 1024>
class TLockFreeRingBuffer {
    static_assert((Capacity > 1) && ((Capacity & (Capacity - 1)) == 0), "Capacity must be a power of two");

public:
    using ElementType = T;

private:
    static constexpr uint32 INDEX_MASK = Capacity - 1;
    static constexpr size_t SLOT_ALIGNMENT = (alignof(T) > PLATFORM_CACHE_LINE_SIZE) ? alignof(T) : PLATFORM_CACHE_LINE_SIZE;

    struct alignas(SLOT_ALIGNMENT) FSlot {
        alignas(alignof(T)) TTypeCompatibleBytes<T> Storage;
        std::atomic<uint32> Sequence{0};
    };

public:
    /**
     * @brief 默认构造函数：初始化全部槽位的序列号
     */
    TLockFreeRingBuffer() noexcept {
        for (uint32 Index = 0; Index < Capacity; ++Index) {
            Slots[Index].Sequence.store(Index, std::memory_order_relaxed);
        }
    }

    /**
     * @brief 析构函数：弹出并析构队列中所有剩余元素
     */
    ~TLockFreeRingBuffer() {
        Clear();
    }

    TLockFreeRingBuffer(const TLockFreeRingBuffer&) = delete;
    TLockFreeRingBuffer& operator=(const TLockFreeRingBuffer&) = delete;
    TLockFreeRingBuffer(TLockFreeRingBuffer&&) = delete;
    TLockFreeRingBuffer& operator=(TLockFreeRingBuffer&&) = delete;

    /**
     * @brief 尝试以拷贝语义将元素推入队尾（若队列已满则非阻塞立即返回 false）
     * @param Item 待入队的元素常量引用
     * @return 入队成功返回 true，队列已满返回 false
     */
    bool Enqueue(const T& Item) {
        return Emplace(Item);
    }

    /**
     * @brief 尝试以移动语义将元素推入队尾（若队列已满则非阻塞立即返回 false）
     * @param Item 待入队的元素右值引用
     * @return 入队成功返回 true，队列已满返回 false
     */
    bool Enqueue(T&& Item) {
        return Emplace(MoveTemp(Item));
    }

    /**
     * @brief 原地构造元素推入队尾（要求元素构造过程不抛出异常）
     * @tparam Args 构造参数类型包
     * @param InArgs 构造参数实例
     * @return 入队成功返回 true，队列已满返回 false
     */
    template <typename... Args>
    bool Emplace(Args&&... InArgs) {
        FSlot* TargetSlot = nullptr;
        uint32 Pos = Tail.load(std::memory_order_relaxed);

        for (;;) {
            TargetSlot = &Slots[Pos & INDEX_MASK];
            const uint32 Seq = TargetSlot->Sequence.load(std::memory_order_acquire);
            const int32 Diff = static_cast<int32>(Seq - Pos);

            if (Diff < 0) {
                return false;
            }
            if (Diff == 0 && Tail.compare_exchange_weak(Pos, Pos + 1, std::memory_order_relaxed)) {
                break;
            }
            if (Diff > 0) {
                Pos = Tail.load(std::memory_order_relaxed);
            }
        }

        new (TargetSlot->Storage.GetTypedPtr()) T(Forward<Args>(InArgs)...);
        TargetSlot->Sequence.store(Pos + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief 尝试从队头弹出一个元素（若队列为空则非阻塞立即返回 false）
     * @param OutItem 接收出队元素的引用
     * @return 出队成功返回 true，队列为空返回 false
     */
    bool Dequeue(T& OutItem) {
        FSlot* TargetSlot = nullptr;
        uint32 Pos = Head.load(std::memory_order_relaxed);

        for (;;) {
            TargetSlot = &Slots[Pos & INDEX_MASK];
            const uint32 Seq = TargetSlot->Sequence.load(std::memory_order_acquire);
            const int32 Diff = static_cast<int32>(Seq - (Pos + 1));

            if (Diff < 0) {
                return false;
            }
            if (Diff == 0 && Head.compare_exchange_weak(Pos, Pos + 1, std::memory_order_relaxed)) {
                break;
            }
            if (Diff > 0) {
                Pos = Head.load(std::memory_order_relaxed);
            }
        }

        T* TypedPtr = TargetSlot->Storage.GetTypedPtr();
        OutItem = MoveTemp(*TypedPtr);
        TypedPtr->~T();
        TargetSlot->Sequence.store(Pos + INDEX_MASK + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief 尝试弹出并就地析构队头的一个元素（不进行拷贝与移动赋值）
     * @return 成功弹出并析构元素返回 true，队列为空返回 false
     */
    bool Pop() {
        FSlot* TargetSlot = nullptr;
        uint32 Pos = Head.load(std::memory_order_relaxed);

        for (;;) {
            TargetSlot = &Slots[Pos & INDEX_MASK];
            const uint32 Seq = TargetSlot->Sequence.load(std::memory_order_acquire);
            const int32 Diff = static_cast<int32>(Seq - (Pos + 1));

            if (Diff < 0) {
                return false;
            }
            if (Diff == 0 && Head.compare_exchange_weak(Pos, Pos + 1, std::memory_order_relaxed)) {
                break;
            }
            if (Diff > 0) {
                Pos = Head.load(std::memory_order_relaxed);
            }
        }

        T* TypedPtr = TargetSlot->Storage.GetTypedPtr();
        TypedPtr->~T();
        TargetSlot->Sequence.store(Pos + INDEX_MASK + 1, std::memory_order_release);
        return true;
    }

    /**
     * @brief 清空队列中所有现存元素并执行析构（建议在无并发生产/消费的静默期或析构期调用）
     */
    void Clear() noexcept {
        while (Pop()) {
        }
    }

    /**
     * @brief 判定队列当前是否为空（弱一致性检查，仅比较 Head 与 Tail 游标，不含正在构造或析构中的瞬态槽位）
     * @return 队列为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Head.load(std::memory_order_relaxed) == Tail.load(std::memory_order_relaxed);
    }

    /**
     * @brief 查询队列当前近似容纳的元素数量（弱一致性读，带并发反转下界钳制保护）
     * @return [0, Capacity] 范围内的近似元素计数
     */
    [[nodiscard]] uint32 Count() const noexcept {
        const uint32 CurrentHead = Head.load(std::memory_order_relaxed);
        const uint32 CurrentTail = Tail.load(std::memory_order_relaxed);
        const int32 Delta = static_cast<int32>(CurrentTail - CurrentHead);
        return Delta > 0 ? FMath::Min<uint32>(static_cast<uint32>(Delta), Capacity) : 0U;
    }

    /**
     * @brief 查询当前环形缓冲区的固定最大容量
     * @return 缓冲区容量
     */
    [[nodiscard]] static constexpr uint32 GetCapacity() noexcept {
        return Capacity;
    }

private:
    FSlot Slots[Capacity];
    alignas(PLATFORM_CACHE_LINE_SIZE) std::atomic<uint32> Tail{0};
    alignas(PLATFORM_CACHE_LINE_SIZE) std::atomic<uint32> Head{0};
};
