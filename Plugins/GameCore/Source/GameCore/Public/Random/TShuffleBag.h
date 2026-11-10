/**
 * @file TShuffleBag.h
 * @brief 泛型抽取不放回洗牌袋（就地随机交换 O(1) 抽取，耗尽自动重洗）
 */

#pragma once

#include "CoreMinimal.h"

#include "Random/FGameRandomStream.h"

/**
 * @brief 不放回均匀抽取洗牌袋容器模板
 * @tparam TValue 袋内元素值类型
 */
template <typename TValue>
class TShuffleBag {
public:
    using ValueType = TValue;
    using SizeType = int32;

public:
    TShuffleBag() = default;

    /**
     * @brief 预分配底层连续数组容量
     * @param Capacity 预期总槽位数
     */
    void Reserve(SizeType Capacity);

    /**
     * @brief 向袋内追加指定份数的元素（追加项立即纳入当前轮可抽取池，不回退本轮已抽出的元素）
     * @param Item 待放入元素
     * @param Count 放入份数（<=0 时忽略）
     */
    void Add(const ValueType& Item, SizeType Count = 1);

    /**
     * @brief 向袋内移动追加单个元素（追加项立即纳入当前轮可抽取池，不回退本轮已抽出的元素）
     * @param Item 待移动放入元素
     */
    void Add(ValueType&& Item);

    /**
     * @brief 从当前轮次剩余元素中抽取一项（若当前轮已抽空则自动开启新一轮；空袋时触发断言，不确定非空请用 TryDraw）
     * @param Stream 驱动抽取的隔离随机流
     * @return 抽中元素的常量引用（调用前袋不可为空）
     */
    [[nodiscard]] const ValueType& Draw(FGameRandomStream& Stream);

    /**
     * @brief 安全抽取指针接口（袋为空时返回 nullptr）
     * @param Stream 驱动抽取的隔离随机流
     * @return 抽中元素常量指针，空袋返回 nullptr
     */
    [[nodiscard]] const ValueType* TryDraw(FGameRandomStream& Stream);

    /**
     * @brief 将所有已抽出元素重新归入可抽取池，开始新一轮洗牌周期
     */
    void Reshuffle() noexcept {
        RemainingCount = Items.Num();
    }

    /**
     * @brief 清空袋中所有元素
     */
    void Clear();

    /**
     * @brief 获取袋内总元素容量（含已抽出与未抽出）
     * @return 元素总数
     */
    [[nodiscard]] SizeType Num() const noexcept {
        return Items.Num();
    }

    /**
     * @brief 获取当前轮次尚未被抽出的剩余元素数量
     * @return 当前轮剩余数量
     */
    [[nodiscard]] SizeType Remaining() const noexcept {
        return RemainingCount;
    }

    /**
     * @brief 检查洗牌袋是否为空
     * @return 无任何元素返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Items.IsEmpty();
    }

private:
    TArray<ValueType> Items;
    SizeType RemainingCount = 0;
};

template <typename TValue>
void TShuffleBag<TValue>::Reserve(SizeType Capacity) {
    if (Capacity > 0) {
        Items.Reserve(Capacity);
    }
}

template <typename TValue>
void TShuffleBag<TValue>::Add(const ValueType& Item, SizeType Count) {
    if (Count <= 0) {
        return;
    }
    Items.Reserve(Items.Num() + Count);
    for (SizeType Index = 0; Index < Count; ++Index) {
        const SizeType AddedIndex = Items.Add(Item);
        if (RemainingCount < AddedIndex) {
            Items.Swap(RemainingCount, AddedIndex);
        }
        ++RemainingCount;
    }
}

template <typename TValue>
void TShuffleBag<TValue>::Add(ValueType&& Item) {
    const SizeType AddedIndex = Items.Add(MoveTemp(Item));
    if (RemainingCount < AddedIndex) {
        Items.Swap(RemainingCount, AddedIndex);
    }
    ++RemainingCount;
}

template <typename TValue>
const typename TShuffleBag<TValue>::ValueType& TShuffleBag<TValue>::Draw(FGameRandomStream& Stream) {
    const ValueType* Picked = TryDraw(Stream);
    checkf(Picked != nullptr, TEXT("Attempted Draw() on an empty TShuffleBag"));
    return *Picked;
}

template <typename TValue>
const typename TShuffleBag<TValue>::ValueType* TShuffleBag<TValue>::TryDraw(FGameRandomStream& Stream) {
    if (Items.IsEmpty()) {
        return nullptr;
    }
    if (RemainingCount <= 0) {
        RemainingCount = Items.Num();
    }

    const int32 PickIndex = Stream.RandRange(0, RemainingCount - 1);
    const int32 TailIndex = RemainingCount - 1;
    if (PickIndex != TailIndex) {
        Items.Swap(PickIndex, TailIndex);
    }
    --RemainingCount;
    return &Items[RemainingCount];
}

template <typename TValue>
void TShuffleBag<TValue>::Clear() {
    Items.Reset();
    RemainingCount = 0;
}
