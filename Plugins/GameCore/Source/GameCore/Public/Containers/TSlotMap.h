/**
 * @file TSlotMap.h
 * @brief 紧凑连续内存代际槽位表（O(1) 插入/删除/查找 + 100% 过期句柄拦截 + 连续数组遍历）
 */

#pragma once

#include "CoreMinimal.h"

#include "Types/TGameHandle.h"

/**
 * @brief 稀疏槽位与密集数组双层代际容器模板
 * @tparam TValue 存储的值类型
 * @tparam TTag 句柄幻影标签类型（默认同 TValue）
 */
template <typename TValue, typename TTag = TValue>
class TSlotMap {
public:
    using ValueType = TValue;
    using HandleType = TGameHandle<TTag>;
    using SizeType = int32;

private:
    struct FSlotEntry {
        uint32 DenseIndex = HandleType::INVALID_INDEX;
        uint32 Generation = 1u;
        uint32 NextFreeSlot = HandleType::INVALID_INDEX;
        bool bOccupied = false;
    };

public:
    TSlotMap() = default;

    /**
     * @brief 预分配槽位与密集元素数组容量
     * @param Capacity 预期元素容量
     */
    void Reserve(SizeType Capacity);

    /**
     * @brief 插入元素并返回唯一下发代际句柄
     * @param InValue 待插入的值（按值接收并移动入槽）
     * @return 新分配的代际句柄
     */
    [[nodiscard]] HandleType Insert(ValueType InValue);

    /**
     * @brief 原地构造元素并返回唯一下发代际句柄
     * @tparam TArgs 构造参数包类型
     * @param Args 转发给元素构造函数的参数
     * @return 新分配的代际句柄
     */
    template <typename... TArgs>
    [[nodiscard]] HandleType Emplace(TArgs&&... Args);

    /**
     * @brief 按句柄移除元素并递增槽位代际号（内部采用尾部交换弹出保持连续性）
     * @param Handle 目标句柄
     * @return 句柄有效且成功移除返回 true；句柄无效或已过期返回 false
     */
    bool Remove(HandleType Handle);

    /**
     * @brief 检查句柄是否指向当前存活的有效元素
     * @param Handle 目标句柄
     * @return 指向活跃有效元素返回 true
     */
    [[nodiscard]] bool IsValidHandle(HandleType Handle) const noexcept;

    /**
     * @brief 按句柄查找元素可变指针
     * @param Handle 目标句柄
     * @return 命中返回元素指针，句柄无效或代际过期返回 nullptr
     * @warning 返回指针在下一次结构变更（如 Insert/Remove/Reset）后失效，严禁跨变更缓存指针！
     */
    [[nodiscard]] ValueType* Find(HandleType Handle) noexcept;

    /**
     * @brief 按句柄查找元素只读指针
     * @param Handle 目标句柄
     * @return 命中返回元素常量指针，句柄无效或代际过期返回 nullptr
     * @warning 返回指针在下一次结构变更（如 Insert/Remove/Reset）后失效，严禁跨变更缓存指针！
     */
    [[nodiscard]] const ValueType* Find(HandleType Handle) const noexcept;

    /**
     * @brief 下标访问运算符（可变版本，若句柄无效或过期则触发断言）
     * @param Handle 目标句柄
     * @return 元素可变引用
     */
    [[nodiscard]] ValueType& operator[](HandleType Handle) {
        ValueType* Found = Find(Handle);
        checkf(Found != nullptr, TEXT("TSlotMap::operator[] Attempted to access with invalid or expired handle"));
        return *Found;
    }

    /**
     * @brief 下标访问运算符（只读版本，若句柄无效或过期则触发断言）
     * @param Handle 目标句柄
     * @return 元素常量引用
     */
    [[nodiscard]] const ValueType& operator[](HandleType Handle) const {
        const ValueType* Found = Find(Handle);
        checkf(Found != nullptr, TEXT("TSlotMap::operator[] const Attempted to access with invalid or expired handle"));
        return *Found;
    }

    /**
     * @brief 密集元素底层数组起始迭代器（支持 C++ 范围 for 循环）
     * @return 连续可变迭代器
     */
    [[nodiscard]] auto begin() noexcept { return DenseValues.begin(); }

    /**
     * @brief 密集元素底层数组结束迭代器
     * @return 连续可变结束迭代器
     */
    [[nodiscard]] auto end() noexcept { return DenseValues.end(); }

    /**
     * @brief 密集元素底层数组起始只读迭代器（支持常量 C++ 范围 for 循环）
     * @return 连续常量迭代器
     */
    [[nodiscard]] auto begin() const noexcept { return DenseValues.begin(); }

    /**
     * @brief 密集元素底层数组结束只读迭代器
     * @return 连续常量结束迭代器
     */
    [[nodiscard]] auto end() const noexcept { return DenseValues.end(); }

    /**
     * @brief 密集元素底层数组常量起始迭代器
     * @return 连续常量迭代器
     */
    [[nodiscard]] auto cbegin() const noexcept { return DenseValues.cbegin(); }

    /**
     * @brief 密集元素底层数组常量结束迭代器
     * @return 连续常量结束迭代器
     */
    [[nodiscard]] auto cend() const noexcept { return DenseValues.cend(); }

    /**
     * @brief 清空全部活跃元素并使此前签发的所有句柄全部代际失效
     */
    void Reset();

    /**
     * @brief 获取当前活跃元素数量
     * @return 活跃元素数
     */
    [[nodiscard]] SizeType Num() const noexcept {
        return DenseValues.Num();
    }

    /**
     * @brief 检查容器是否为空
     * @return 无活跃元素返回 true
     */
    [[nodiscard]] bool IsEmpty() const noexcept {
        return DenseValues.IsEmpty();
    }

    /**
     * @brief 获取底层连续密集元素只读切片（供高性能缓存友好遍历）
     * @return 密集数组只读视图
     */
    [[nodiscard]] TArrayView<const ValueType> GetDenseValues() const noexcept {
        return DenseValues;
    }

    /**
     * @brief 获取底层连续密集元素可变切片
     * @return 密集数组可变视图
     */
    [[nodiscard]] TArrayView<ValueType> GetMutableDenseValues() noexcept {
        return DenseValues;
    }

    /**
     * @brief 由密集数组索引反查对应的唯一代际句柄
     * @param DenseIndex 密集数组索引 [0, Num() - 1]
     * @return 对应的代际句柄；若索引越界返回无效句柄
     */
    [[nodiscard]] HandleType GetHandleForDenseIndex(SizeType DenseIndex) const noexcept;

    /**
     * @brief 遍历所有活跃元素并同时传出唯一代际句柄与元素可变引用
     * @tparam TFunc 可调用闭包类型 void(HandleType, ValueType&) 或 bool(HandleType, ValueType&)
     * @param Func 遍历闭包；若闭包返回 bool，返回 false 可提前终止遍历
     * @warning 遍历回调闭包执行期间禁止调用 Insert/Remove/Reset 对本容器进行结构性修改！
     */
    template <typename TFunc>
    void ForEach(TFunc&& Func);

    /**
     * @brief 只读遍历所有活跃元素并同时传出唯一代际句柄与元素常量引用
     * @tparam TFunc 可调用闭包类型 void(HandleType, const ValueType&) 或 bool(HandleType, const ValueType&)
     * @param Func 遍历闭包；若闭包返回 bool，返回 false 可提前终止遍历
     * @warning 遍历回调闭包执行期间禁止调用 Insert/Remove/Reset 对本容器进行结构性修改！
     */
    template <typename TFunc>
    void ForEach(TFunc&& Func) const;

private:
    TArray<FSlotEntry> Slots;
    TArray<ValueType> DenseValues;
    TArray<uint32> DenseToSlot;
    uint32 FreeHead = HandleType::INVALID_INDEX;
    mutable int32 IteratingDepth = 0;
};

template <typename TValue, typename TTag>
void TSlotMap<TValue, TTag>::Reserve(SizeType Capacity) {
    if (Capacity <= 0) {
        return;
    }
    Slots.Reserve(Capacity);
    DenseValues.Reserve(Capacity);
    DenseToSlot.Reserve(Capacity);
}

template <typename TValue, typename TTag>
typename TSlotMap<TValue, TTag>::HandleType TSlotMap<TValue, TTag>::Insert(ValueType InValue) {
    return Emplace(MoveTemp(InValue));
}

template <typename TValue, typename TTag>
template <typename... TArgs>
typename TSlotMap<TValue, TTag>::HandleType TSlotMap<TValue, TTag>::Emplace(TArgs&&... Args) {
    checkf(IteratingDepth == 0, TEXT("TSlotMap structural modification is prohibited during ForEach iteration"));
    uint32 SlotIdx = HandleType::INVALID_INDEX;
    if (FreeHead != HandleType::INVALID_INDEX) {
        SlotIdx = FreeHead;
        FreeHead = Slots[SlotIdx].NextFreeSlot;
    } else {
        SlotIdx = static_cast<uint32>(Slots.Add(FSlotEntry{}));
    }

    const uint32 DenseIdx = static_cast<uint32>(DenseValues.Emplace(Forward<TArgs>(Args)...));
    DenseToSlot.Add(SlotIdx);

    FSlotEntry& Slot = Slots[SlotIdx];
    Slot.DenseIndex = DenseIdx;
    Slot.NextFreeSlot = HandleType::INVALID_INDEX;
    Slot.bOccupied = true;

    return HandleType(SlotIdx, Slot.Generation);
}

template <typename TValue, typename TTag>
bool TSlotMap<TValue, TTag>::Remove(HandleType Handle) {
    checkf(IteratingDepth == 0, TEXT("TSlotMap structural modification is prohibited during ForEach iteration"));
    if (!IsValidHandle(Handle)) {
        return false;
    }

    const uint32 TargetSlotIdx = Handle.GetIndex();
    FSlotEntry& TargetSlot = Slots[TargetSlotIdx];
    const uint32 RemovedDenseIdx = TargetSlot.DenseIndex;
    const uint32 LastDenseIdx = static_cast<uint32>(DenseValues.Num() - 1);

    if (RemovedDenseIdx != LastDenseIdx) {
        const uint32 MovedSlotIdx = DenseToSlot[LastDenseIdx];
        DenseValues[RemovedDenseIdx] = MoveTemp(DenseValues[LastDenseIdx]);
        DenseToSlot[RemovedDenseIdx] = MovedSlotIdx;
        Slots[MovedSlotIdx].DenseIndex = RemovedDenseIdx;
    }

    DenseValues.Pop(EAllowShrinking::No);
    DenseToSlot.Pop(EAllowShrinking::No);

    TargetSlot.DenseIndex = HandleType::INVALID_INDEX;
    TargetSlot.bOccupied = false;
    if (TargetSlot.Generation < MAX_uint32) {
        ++TargetSlot.Generation;
        TargetSlot.NextFreeSlot = FreeHead;
        FreeHead = TargetSlotIdx;
    } else {
        TargetSlot.Generation = 0u;
        TargetSlot.NextFreeSlot = HandleType::INVALID_INDEX;
    }
    return true;
}

template <typename TValue, typename TTag>
bool TSlotMap<TValue, TTag>::IsValidHandle(HandleType Handle) const noexcept {
    if (!Handle.IsValid()) {
        return false;
    }
    const uint32 SlotIdx = Handle.GetIndex();
    if (SlotIdx >= static_cast<uint32>(Slots.Num())) {
        return false;
    }
    const FSlotEntry& Slot = Slots[SlotIdx];
    return Slot.bOccupied && Slot.Generation == Handle.GetGeneration();
}

template <typename TValue, typename TTag>
typename TSlotMap<TValue, TTag>::ValueType* TSlotMap<TValue, TTag>::Find(HandleType Handle) noexcept {
    if (!IsValidHandle(Handle)) {
        return nullptr;
    }
    return &DenseValues[Slots[Handle.GetIndex()].DenseIndex];
}

template <typename TValue, typename TTag>
const typename TSlotMap<TValue, TTag>::ValueType* TSlotMap<TValue, TTag>::Find(HandleType Handle) const noexcept {
    if (!IsValidHandle(Handle)) {
        return nullptr;
    }
    return &DenseValues[Slots[Handle.GetIndex()].DenseIndex];
}

template <typename TValue, typename TTag>
void TSlotMap<TValue, TTag>::Reset() {
    checkf(IteratingDepth == 0, TEXT("TSlotMap structural modification is prohibited during ForEach iteration"));
    DenseValues.Reset();
    DenseToSlot.Reset();
    FreeHead = HandleType::INVALID_INDEX;

    for (int32 Index = Slots.Num() - 1; Index >= 0; --Index) {
        FSlotEntry& Slot = Slots[Index];
        if (Slot.bOccupied) {
            Slot.bOccupied = false;
            Slot.DenseIndex = HandleType::INVALID_INDEX;
            if (Slot.Generation < MAX_uint32) {
                ++Slot.Generation;
            } else {
                Slot.Generation = 0u;
            }
        }
        if (Slot.Generation != 0u) {
            Slot.NextFreeSlot = FreeHead;
            FreeHead = static_cast<uint32>(Index);
        } else {
            Slot.NextFreeSlot = HandleType::INVALID_INDEX;
        }
    }
}

template <typename TValue, typename TTag>
typename TSlotMap<TValue, TTag>::HandleType TSlotMap<TValue, TTag>::GetHandleForDenseIndex(SizeType DenseIndex) const noexcept {
    if (DenseIndex < 0 || DenseIndex >= DenseValues.Num()) {
        return HandleType{};
    }
    const uint32 SlotIdx = DenseToSlot[DenseIndex];
    return HandleType(SlotIdx, Slots[SlotIdx].Generation);
}

template <typename TValue, typename TTag>
template <typename TFunc>
void TSlotMap<TValue, TTag>::ForEach(TFunc&& Func) {
    ++IteratingDepth;
    const int32 Count = DenseValues.Num();
    for (int32 Index = 0; Index < Count && Index < DenseValues.Num(); ++Index) {
        const uint32 SlotIdx = DenseToSlot[Index];
        const HandleType Handle(SlotIdx, Slots[SlotIdx].Generation);
        if constexpr (std::is_invocable_r_v<bool, TFunc, HandleType, ValueType&>) {
            if (!Func(Handle, DenseValues[Index])) {
                break;
            }
        } else {
            Func(Handle, DenseValues[Index]);
        }
    }
    --IteratingDepth;
}

template <typename TValue, typename TTag>
template <typename TFunc>
void TSlotMap<TValue, TTag>::ForEach(TFunc&& Func) const {
    ++IteratingDepth;
    const int32 Count = DenseValues.Num();
    for (int32 Index = 0; Index < Count && Index < DenseValues.Num(); ++Index) {
        const uint32 SlotIdx = DenseToSlot[Index];
        const HandleType Handle(SlotIdx, Slots[SlotIdx].Generation);
        if constexpr (std::is_invocable_r_v<bool, TFunc, HandleType, const ValueType&>) {
            if (!Func(Handle, DenseValues[Index])) {
                break;
            }
        } else {
            Func(Handle, DenseValues[Index]);
        }
    }
    --IteratingDepth;
}
