/**
 * @file FScopedFrameScratchpad.cpp
 * @brief 作用域级单帧刮擦内存分配器实现
 */

#include "Memory/FScopedFrameScratchpad.h"

namespace {
    static constexpr size_t GDefaultFrameArenaChunkSize = 256 * 1024;

    thread_local FLinearArena GTlsFrameArena(GDefaultFrameArenaChunkSize);
}

/**
 * @brief 获取当前线程专属的单帧线性分配器单例
 * @return 线程局部分配器引用
 */
FLinearArena& FScopedFrameScratchpad::GetThreadLocalArena() {
    return GTlsFrameArena;
}

/**
 * @brief 强制重置当前线程的分配器
 */
void FScopedFrameScratchpad::ResetThreadLocalArena() {
    GTlsFrameArena.Reset();
}

/**
 * @brief 构造函数：默认绑定当前线程的单帧内存池并设置 RAII 回退点
 */
FScopedFrameScratchpad::FScopedFrameScratchpad()
    : ArenaRef(GetThreadLocalArena())
    , ScopeMarker(ArenaRef.CreateScopeMarker()) {
}

/**
 * @brief 构造函数：指定自定义分配器并设置 RAII 回退点
 * @param InArena 自定义分配器引用
 */
FScopedFrameScratchpad::FScopedFrameScratchpad(FLinearArena& InArena)
    : ArenaRef(InArena)
    , ScopeMarker(ArenaRef.CreateScopeMarker()) {
}

/**
 * @brief 分配指定大小与对齐的原生连续内存
 * @param InSize 期望字节数
 * @param InAlignment 内存对齐字节数
 * @return 内存块首地址指针
 */
void* FScopedFrameScratchpad::Allocate(size_t InSize, size_t InAlignment) {
    return ArenaRef.Allocate(InSize, InAlignment);
}
