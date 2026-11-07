/**
 * @file TCancelableScopeGuard.h
 * @brief 可动态取消的 RAII 作用域事务守卫模板（零堆分配，移动安全）
 */

#pragma once

#include "CoreMinimal.h"

#include <type_traits>
#include <utility>

/**
 * @brief 退出作用域时自动执行回滚或清理动作的事务守卫模型（支持动态取消与所有权转移）
 * @tparam TFunc 可调用对象类型
 */
template <typename TFunc>
class [[nodiscard]] TCancelableScopeGuard {
public:
    /**
     * @brief 构造作用域守卫并接管清理回调
     * @param InFunc 待执行的清理动作
     */
    explicit TCancelableScopeGuard(TFunc InFunc) noexcept
        : Func(MoveTemp(InFunc))
        , bActive(true) {}

    /**
     * @brief 析构时若未被主动取消则触发已注册的回调
     */
    ~TCancelableScopeGuard() {
        if (bActive) {
            Func();
        }
    }

    TCancelableScopeGuard(const TCancelableScopeGuard&) = delete;
    TCancelableScopeGuard& operator=(const TCancelableScopeGuard&) = delete;

    /**
     * @brief 移动构造函数（转移回调并解除源对象激活状态）
     * @param Other 源作用域守卫
     */
    TCancelableScopeGuard(TCancelableScopeGuard&& Other) noexcept
        : Func(MoveTemp(Other.Func))
        , bActive(Other.bActive) {
        Other.bActive = false;
    }

    /**
     * @brief 移动赋值运算符（若自身处于激活态则先执行自身回调，再接管源对象状态）
     * @param Other 源作用域守卫
     * @return 自身引用
     */
    TCancelableScopeGuard& operator=(TCancelableScopeGuard&& Other) noexcept {
        if (this != &Other) {
            if (bActive) {
                Func();
            }
            Func = MoveTemp(Other.Func);
            bActive = Other.bActive;
            Other.bActive = false;
        }
        return *this;
    }

    /**
     * @brief 主动取消守卫回调（标记为失效，析构时不再触发）
     */
    void Dismiss() noexcept {
        bActive = false;
    }

    /**
     * @brief 查询当前守卫是否处于激活状态
     * @return 激活返回 true，已取消或已转移返回 false
     */
    [[nodiscard]] bool IsActive() const noexcept {
        return bActive;
    }

private:
    TFunc Func;
    bool bActive = false;
};

/**
 * @brief 便捷创建可取消作用域守卫实例的辅助工厂函数
 * @tparam TFunc 可调用对象类型
 * @param Func 待执行的清理动作
 * @return 构造出的作用域守卫实例
 */
template <typename TFunc>
[[nodiscard]] auto MakeCancelableScopeGuard(TFunc&& Func) {
    using DecayedFunc = std::decay_t<TFunc>;
    return TCancelableScopeGuard<DecayedFunc>(Forward<TFunc>(Func));
}
