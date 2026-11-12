/**
 * @file TLinearArena.h
 * @brief 单帧/局部单调线性内存分配器（支持指针稳定化、RAII 作用域标记回退与自动析构链）
 */

#pragma once

#include "CoreMinimal.h"

#include <new>
#include <type_traits>

/**
 * @brief 线性内存块分配器
 *
 * 专为高频临时计算设计的单调线性分配器：
 * 1. 连续块内递增游标分配，零内存碎片与纳秒级分配速度；
 * 2. 块链表机制确保已分配内存指针 100% 稳定，扩容不发生数据搬移；
 * 3. 支持 TScopeMarker RAII 作用域标记，离开作用域自动回退游标并按逆序析构非平凡对象；
 * 4. Reset 仅重置游标并保留已申请的大块内存，供下一帧零 Malloc 复用。
 */
class GAMECORE_API FLinearArena {
public:
    static constexpr size_t DEFAULT_CHUNK_SIZE = 64 * 1024;
    static constexpr size_t MIN_CHUNK_SIZE = 1024;

    /**
     * @brief 作用域回退标记（RAII 守卫，严格遵循 LIFO 嵌套回退）
     */
    class FScopeMarker;

private:
    /**
     * @brief 非平凡对象析构链表节点
     */
    struct FDestructorNode {
        void (*DestructorFunc)(void*);
        void* ObjectPtr;
        FDestructorNode* NextNode;
    };

    /**
     * @brief 线性内存块
     */
    struct FMemoryChunk {
        uint8* Buffer = nullptr;
        size_t Capacity = 0;
        size_t Cursor = 0;
        FMemoryChunk* NextChunk = nullptr;

        FMemoryChunk(size_t InCapacity, FMemoryChunk* InNext, size_t InAlignment = alignof(std::max_align_t))
            : Buffer(static_cast<uint8*>(FMemory::Malloc(
                  InCapacity,
                  static_cast<uint32>(FMath::Max<size_t>(InAlignment, alignof(std::max_align_t)))
              )))
            , Capacity(InCapacity)
            , Cursor(0)
            , NextChunk(InNext) {
        }

        ~FMemoryChunk() {
            if (Buffer != nullptr) {
                FMemory::Free(Buffer);
                Buffer = nullptr;
            }
        }

        FMemoryChunk(const FMemoryChunk&) = delete;
        FMemoryChunk& operator=(const FMemoryChunk&) = delete;
        FMemoryChunk(FMemoryChunk&&) = delete;
        FMemoryChunk& operator=(FMemoryChunk&&) = delete;
    };

public:
    /**
     * @brief 构造函数：预分配指定大小的初始内存块（下限钳制为 MIN_CHUNK_SIZE = 1024 字节）
     * @param InDefaultChunkSize 默认内存块大小（字节，默认 64KB）
     */
    explicit FLinearArena(size_t InDefaultChunkSize = DEFAULT_CHUNK_SIZE);

    /**
     * @brief 析构函数：执行所有析构链并释放所有内存块
     */
    ~FLinearArena();

    FLinearArena(const FLinearArena&) = delete;
    FLinearArena& operator=(const FLinearArena&) = delete;
    FLinearArena(FLinearArena&&) noexcept;
    FLinearArena& operator=(FLinearArena&&) noexcept;

    /**
     * @brief 分配指定大小与对齐的连续原生内存
     * @param InSize 期望申请的字节数
     * @param InAlignment 对齐字节数（必须为 2 的幂）
     * @return 内存块起始地址，入参非法或容量为 0 时返回 nullptr
     */
    [[nodiscard]] void* Allocate(size_t InSize, size_t InAlignment = alignof(std::max_align_t));

    /**
     * @brief 在分配器内构造对象实例
     * @tparam T 目标对象类型
     * @tparam TArgs 构造参数类型包
     * @param Args 传递给构造函数的参数
     * @return 构造完成的对象指针
     */
    template <typename T, typename... TArgs>
    [[nodiscard]] T* New(TArgs&&... Args) {
        void* RawMemory = Allocate(sizeof(T), alignof(T));
        if (RawMemory == nullptr) {
            return nullptr;
        }

        T* ObjectInstance = new (RawMemory) T(Forward<TArgs>(Args)...);

        if constexpr (!std::is_trivially_destructible_v<T>) {
            RegisterDestructor(
                [](void* InTarget) {
                    static_cast<T*>(InTarget)->~T();
                },
                ObjectInstance
            );
        }

        return ObjectInstance;
    }

    /**
     * @brief 分配指定数量的未初始化连续元素内存（不调用构造与析构函数，适用于平凡类型或手动管理生命周期缓冲区）
     * @tparam T 元素类型
     * @param InCount 数组元素总数
     * @return 连续数组首地址指针
     */
    template <typename T>
    [[nodiscard]] T* AllocateArray(size_t InCount) {
        if (InCount == 0 || InCount > (SIZE_MAX / sizeof(T))) {
            return nullptr;
        }
        return static_cast<T*>(Allocate(sizeof(T) * InCount, alignof(T)));
    }

    /**
     * @brief 分配并构造指定数量的连续元素数组（非平凡析构类型自动注册逆序析构回调）
     * @tparam T 元素类型
     * @tparam Args 元素构造参数类型包
     * @param InCount 数组元素总数
     * @param InArgs 转发给每个元素构造函数的参数
     * @return 已构造的连续数组首地址指针
     */
    template <typename T, typename... Args>
    [[nodiscard]] T* NewArray(size_t InCount, Args&&... InArgs) {
        T* ArrayPtr = AllocateArray<T>(InCount);
        if (ArrayPtr == nullptr) {
            return nullptr;
        }
        for (size_t Index = 0; Index < InCount; ++Index) {
            new (&ArrayPtr[Index]) T(InArgs...);
        }
        if constexpr (!std::is_trivially_destructible_v<T>) {
            struct FArrayDestructorPayload {
                T* Data;
                size_t Count;
            };
            FArrayDestructorPayload* Payload = static_cast<FArrayDestructorPayload*>(
                Allocate(sizeof(FArrayDestructorPayload), alignof(FArrayDestructorPayload))
            );
            if (Payload != nullptr) {
                Payload->Data = ArrayPtr;
                Payload->Count = InCount;
                RegisterDestructor(
                    [](void* InTarget) {
                        FArrayDestructorPayload* P = static_cast<FArrayDestructorPayload*>(InTarget);
                        for (size_t Index = P->Count; Index > 0; --Index) {
                            P->Data[Index - 1].~T();
                        }
                    },
                    Payload
                );
            }
        }
        return ArrayPtr;
    }

    /**
     * @brief 重置整个分配器状态（执行所有非平凡析构并重置游标，同时使此前未销毁的 FScopeMarker 安全失效）
     */
    void Reset();

    /**
     * @brief 创建当前游标位置的 RAII 作用域标记（须遵循 LIFO 顺序析构；若父级标记或 Reset 先行回退，子标记析构将安全退化为空操作）
     * @return 作用域标记实例
     */
    [[nodiscard]] FScopeMarker CreateScopeMarker();

    /**
     * @brief 获取当前处于活跃状态的作用域标记深度
     * @return 活跃 FScopeMarker 数量
     */
    [[nodiscard]] int32 GetActiveScopeCount() const noexcept {
        return ActiveMarkerStack.Num();
    }

    /**
     * @brief 获取当前已分配总内存大小（字节）
     * @return 已使用的字节数
     */
    [[nodiscard]] size_t GetTotalAllocatedBytes() const noexcept;

    /**
     * @brief 获取当前保留的内存总容量（字节）
     * @return 容量字节数
     */
    [[nodiscard]] size_t GetTotalCapacityBytes() const noexcept;

private:
    /**
     * @brief 注册非平凡析构回调节点
     * @param InDestructorFunc 析构函数指针
     * @param InObjectPtr 目标对象指针
     */
    void RegisterDestructor(void (*InDestructorFunc)(void*), void* InObjectPtr);

    /**
     * @brief 回退游标并析构指定节点之后的对象
     * @param TargetChunk 目标块指针
     * @param TargetCursor 目标游标偏移
     * @param TargetDestructorNode 目标析构链末端节点
     */
    void RewindTo(FMemoryChunk* TargetChunk, size_t TargetCursor, FDestructorNode* TargetDestructorNode);

    /**
     * @brief 释放指定标记 ID 及建立在其后的内层标记并执行回退（若标记已因 Reset 或外层回退失效则忽略）
     * @param MarkerId 标记唯一递增 ID
     * @param TargetChunk 目标块指针
     * @param TargetCursor 目标游标偏移
     * @param TargetDestructorNode 目标析构链末端节点
     */
    void PopAndRewindMarker(
        uint64 MarkerId,
        FMemoryChunk* TargetChunk,
        size_t TargetCursor,
        FDestructorNode* TargetDestructorNode
    );

private:
    size_t DefaultChunkSize;
    FMemoryChunk* FirstChunk = nullptr;
    FMemoryChunk* CurrentChunk = nullptr;
    FDestructorNode* DestructorHead = nullptr;
    uint64 NextMarkerId = 0;
    TArray<uint64, TInlineAllocator<16>> ActiveMarkerStack;
};

/**
 * @brief RAII 作用域标记守卫
 */
class GAMECORE_API FLinearArena::FScopeMarker {
public:
    /**
     * @brief 构造函数：捕获当前分配器状态并压入活跃标记栈
     * @param InArena 所属分配器引用
     */
    explicit FScopeMarker(FLinearArena& InArena);

    /**
     * @brief 析构函数：回退分配器至标记捕获点
     */
    ~FScopeMarker();

    FScopeMarker(const FScopeMarker&) = delete;
    FScopeMarker& operator=(const FScopeMarker&) = delete;
    FScopeMarker(FScopeMarker&& Other) noexcept;
    FScopeMarker& operator=(FScopeMarker&& Other) noexcept;

private:
    FLinearArena* ArenaPtr;
    FMemoryChunk* SavedChunk;
    size_t SavedCursor;
    FDestructorNode* SavedDestructorNode;
    uint64 MarkerId = 0;
};
