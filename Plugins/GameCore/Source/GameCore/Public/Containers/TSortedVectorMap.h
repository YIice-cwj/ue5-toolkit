/**
 * @file TSortedVectorMap.h
 * @brief 基于连续内存平铺排序的小容量高效字典（零堆碎片 · L1 Cache 极度亲和 · 区间查询 · 批量合并）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>

/**
 * @brief 连续内存扁平排序字典容器
 *
 * 针对 <= 16 项的小集合（如属性修饰项、信道映射、小状态表）设计：
 * 1. 元素在连续内存数组内平铺存放，完全消除 TMap 哈希桶分配与离散指针寻址开销；
 * 2. 默认使用 TInlineAllocator 预留小栈容量，小数据量下保证绝对零堆分配；
 * 3. 内部元素按 Key 严格升序排列，通过二分查找实现 O(log N) 快速检索；
 * 4. 连续遍历极快，具备最高等级的 CPU 数据预取优势；
 * 5. 支持 O(log N + K) 高速区间范围查询与批量去重合并。
 *
 * @tparam KeyType 键类型（必须支持 operator< 与 operator==）
 * @tparam ValueType 值类型
 * @tparam InlineCapacity 栈上内联预留容量（默认 16 项）
 */
template <typename KeyType, typename ValueType, int32 InlineCapacity = 16>
class TSortedVectorMap {
public:
    /**
     * @brief 扁平键值对存储条目
     */
    struct FEntry {
        KeyType Key;
        ValueType Value;

        FEntry() = default;

        /**
         * @brief 完美转发构造键值对
         * @tparam K 键转发类型
         * @tparam V 值转发类型
         * @param InKey 键
         * @param InValue 值
         */
        template <typename K, typename V>
        FEntry(K&& InKey, V&& InValue)
            : Key(Forward<K>(InKey))
            , Value(Forward<V>(InValue)) {
        }

        /**
         * @brief 基于键的相等性比较
         * @param Other 待比较条目
         * @return 键相等返回 true
         */
        [[nodiscard]] bool operator==(const FEntry& Other) const {
            return Key == Other.Key;
        }
    };

    TSortedVectorMap() = default;

    /**
     * @brief 预留连续存储容量
     * @param Capacity 期望容量
     */
    void Reserve(int32 Capacity) {
        Entries.Reserve(Capacity);
    }

    /**
     * @brief 查找键对应的元素位置
     * @param Key 目标查找键
     * @return 存在返回元素有效指针，未找到返回 nullptr
     */
    [[nodiscard]] ValueType* Find(const KeyType& Key) noexcept {
        const int32 Index = FindIndex(Key);
        return (Index != INDEX_NONE) ? &Entries[Index].Value : nullptr;
    }

    /**
     * @brief 只读查找键对应的元素位置
     * @param Key 目标查找键
     * @return 存在返回元素只读指针，未找到返回 nullptr
     */
    [[nodiscard]] const ValueType* Find(const KeyType& Key) const noexcept {
        const int32 Index = FindIndex(Key);
        return (Index != INDEX_NONE) ? &Entries[Index].Value : nullptr;
    }

    /**
     * @brief 检查是否包含目标键
     * @param Key 目标键
     * @return 存在返回 true
     */
    [[nodiscard]] bool Contains(const KeyType& Key) const noexcept {
        return FindIndex(Key) != INDEX_NONE;
    }

    /**
     * @brief 插入或更新键值对（保持有序）
     * @param Key 目标键
     * @param Value 待设置数值
     * @return 若为新插入返回 true，若为更新旧值返回 false
     */
    bool Add(const KeyType& Key, const ValueType& Value) {
        int32 InsertIndex = 0;
        const int32 ExistingIndex = LowerBoundInternal(Key, InsertIndex);
        if (ExistingIndex != INDEX_NONE) {
            Entries[ExistingIndex].Value = Value;
            return false;
        }
        Entries.Insert(FEntry(Key, Value), InsertIndex);
        return true;
    }

    /**
     * @brief 插入或更新键值对（移动语义）
     * @param Key 目标键
     * @param Value 待移动设置数值
     * @return 若为新插入返回 true，若为更新旧值返回 false
     */
    bool Add(KeyType&& Key, ValueType&& Value) {
        int32 InsertIndex = 0;
        const int32 ExistingIndex = LowerBoundInternal(Key, InsertIndex);
        if (ExistingIndex != INDEX_NONE) {
            Entries[ExistingIndex].Value = MoveTemp(Value);
            return false;
        }
        Entries.Insert(FEntry(MoveTemp(Key), MoveTemp(Value)), InsertIndex);
        return true;
    }

    /**
     * @brief 获取已有值或插入默认构造的新项
     * @param Key 目标键
     * @return 对应的值引用
     */
    ValueType& FindOrAdd(const KeyType& Key) {
        int32 InsertIndex = 0;
        const int32 ExistingIndex = LowerBoundInternal(Key, InsertIndex);
        if (ExistingIndex != INDEX_NONE) {
            return Entries[ExistingIndex].Value;
        }
        Entries.Insert(FEntry(Key, ValueType{}), InsertIndex);
        return Entries[InsertIndex].Value;
    }

    /**
     * @brief 下标访问运算符（存在则返回，不存在则默认构造插入）
     * @param Key 目标键
     * @return 对应的值引用
     */
    ValueType& operator[](const KeyType& Key) {
        return FindOrAdd(Key);
    }

    /**
     * @brief 只读下标访问运算符（键不存在触发断言）
     * @param Key 目标键
     * @return 对应的值只读引用
     */
    const ValueType& operator[](const KeyType& Key) const {
        const ValueType* Found = Find(Key);
        checkf(Found != nullptr, TEXT("TSortedVectorMap::operator[] const key not found"));
        return *Found;
    }

    /**
     * @brief 批量合并追加键值对（单趟时序稳定排序与相邻去重，大幅优化多次 Add 开销）
     * @param NewEntries 待批量合并的新键值对数组
     */
    void AppendOrAssign(TConstArrayView<FEntry> NewEntries) {
        if (NewEntries.IsEmpty()) {
            return;
        }
        struct FTaggedEntry {
            KeyType Key;
            ValueType Value;
            int32 Order = 0;
        };

        TArray<FTaggedEntry, TInlineAllocator<InlineCapacity * 2>> Tagged;
        Tagged.Reserve(Entries.Num() + NewEntries.Num());
        int32 OrderCounter = 0;

        for (FEntry& Old : Entries) {
            Tagged.Add(FTaggedEntry{ MoveTemp(Old.Key), MoveTemp(Old.Value), OrderCounter++ });
        }
        for (const FEntry& Item : NewEntries) {
            Tagged.Add(FTaggedEntry{ Item.Key, Item.Value, OrderCounter++ });
        }

        Tagged.Sort([](const FTaggedEntry& A, const FTaggedEntry& B) {
            if (A.Key < B.Key) {
                return true;
            }
            if (B.Key < A.Key) {
                return false;
            }
            return A.Order < B.Order;
        });

        Entries.Reset();
        for (FTaggedEntry& Item : Tagged) {
            if (Entries.IsEmpty() || Entries.Last().Key < Item.Key) {
                Entries.Emplace(MoveTemp(Item.Key), MoveTemp(Item.Value));
            } else {
                Entries.Last().Value = MoveTemp(Item.Value);
            }
        }
    }

    /**
     * @brief 移除指定键对应的元素
     * @param Key 目标键
     * @return 成功移除返回 true，未找到返回 false
     */
    bool Remove(const KeyType& Key) {
        const int32 Index = FindIndex(Key);
        if (Index != INDEX_NONE) {
            Entries.RemoveAt(Index);
            return true;
        }
        return false;
    }

    /**
     * @brief 查找第一个不小于给定键的元素位置（二分下界）
     * @param Key 目标基准键
     * @return 第一个 >= Key 的条目索引；若所有键均小于目标则返回 Num()
     */
    [[nodiscard]] int32 LowerBoundIndex(const KeyType& Key) const noexcept {
        int32 Low = 0;
        int32 High = Entries.Num();
        while (Low < High) {
            const int32 Mid = Low + ((High - Low) >> 1);
            if (Entries[Mid].Key < Key) {
                Low = Mid + 1;
            } else {
                High = Mid;
            }
        }
        return Low;
    }

    /**
     * @brief 查找第一个严格大于给定键的元素位置（二分上界）
     * @param Key 目标基准键
     * @return 第一个 > Key 的条目索引；若所有键均不大于目标则返回 Num()
     */
    [[nodiscard]] int32 UpperBoundIndex(const KeyType& Key) const noexcept {
        int32 Low = 0;
        int32 High = Entries.Num();
        while (Low < High) {
            const int32 Mid = Low + ((High - Low) >> 1);
            if (!(Key < Entries[Mid].Key)) {
                Low = Mid + 1;
            } else {
                High = Mid;
            }
        }
        return Low;
    }

    /**
     * @brief 对键落入 [MinKey, MaxKey] 闭区间的条目进行高效范围遍历
     * @tparam TFunc 遍历闭包 void(const KeyType&, const ValueType&) 或 bool(...)
     * @param MinKey 最小键边界（闭区间）
     * @param MaxKey 最大键边界（闭区间）
     * @param Func 遍历回调闭包
     */
    template <typename TFunc>
    void ForRange(const KeyType& MinKey, const KeyType& MaxKey, TFunc&& Func) const {
        if (MaxKey < MinKey || Entries.IsEmpty()) {
            return;
        }
        const int32 StartIndex = LowerBoundIndex(MinKey);
        const int32 EndIndex = UpperBoundIndex(MaxKey);
        for (int32 Index = StartIndex; Index < EndIndex; ++Index) {
            if constexpr (std::is_invocable_r_v<bool, TFunc, const KeyType&, const ValueType&>) {
                if (!Func(Entries[Index].Key, Entries[Index].Value)) {
                    break;
                }
            } else {
                Func(Entries[Index].Key, Entries[Index].Value);
            }
        }
    }

    /**
     * @brief 获取当前存储元素总数
     * @return 项数
     */
    [[nodiscard]] int32 Num() const noexcept {
        return Entries.Num();
    }

    /**
     * @brief 查询字典是否为空
     * @return 为空返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return Entries.IsEmpty();
    }

    /**
     * @brief 清空所有元素（保留内联栈分配）
     */
    void Reset() {
        Entries.Reset();
    }

    /**
     * @brief 清空并彻底释放可能存在的堆分配
     */
    void Empty() {
        Entries.Empty();
    }

    /**
     * @brief 获取底层物理连续条目数组只读切片
     * @return 只读数组引用
     */
    [[nodiscard]] const TArray<FEntry, TInlineAllocator<InlineCapacity>>& GetEntries() const noexcept {
        return Entries;
    }

    /**
     * @brief 顺序遍历所有键值对
     * @tparam TFunc 遍历可调用对象 void(const KeyType&, ValueType&) 或 bool(const KeyType&, ValueType&)
     * @param Func 遍历闭包
     */
    template <typename TFunc>
    void ForEach(TFunc&& Func) {
        for (FEntry& Entry : Entries) {
            if constexpr (std::is_invocable_r_v<bool, TFunc, const KeyType&, ValueType&>) {
                if (!Func(Entry.Key, Entry.Value)) {
                    break;
                }
            } else {
                Func(Entry.Key, Entry.Value);
            }
        }
    }

    /**
     * @brief 只读顺序遍历所有键值对
     * @tparam TFunc 遍历可调用对象 void(const KeyType&, const ValueType&) 或 bool(const KeyType&, const ValueType&)
     * @param Func 遍历闭包
     */
    template <typename TFunc>
    void ForEach(TFunc&& Func) const {
        for (const FEntry& Entry : Entries) {
            if constexpr (std::is_invocable_r_v<bool, TFunc, const KeyType&, const ValueType&>) {
                if (!Func(Entry.Key, Entry.Value)) {
                    break;
                }
            } else {
                Func(Entry.Key, Entry.Value);
            }
        }
    }

    auto begin() noexcept { return Entries.begin(); }
    auto end() noexcept { return Entries.end(); }
    auto begin() const noexcept { return Entries.begin(); }
    auto end() const noexcept { return Entries.end(); }
    auto cbegin() const noexcept { return Entries.cbegin(); }
    auto cend() const noexcept { return Entries.cend(); }

private:
    [[nodiscard]] int32 FindIndex(const KeyType& Key) const noexcept {
        int32 Dummy = 0;
        return LowerBoundInternal(Key, Dummy);
    }

    [[nodiscard]] int32 LowerBoundInternal(const KeyType& Key, int32& OutInsertIndex) const noexcept {
        const int32 Index = LowerBoundIndex(Key);
        OutInsertIndex = Index;
        if (Index < Entries.Num() && Entries[Index].Key == Key) {
            return Index;
        }
        return INDEX_NONE;
    }

private:
    TArray<FEntry, TInlineAllocator<InlineCapacity>> Entries;
};
