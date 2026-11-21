/**
 * @file IGamePoolable.h
 * @brief 对象池可复用对象生命周期契约接口（支持纯 C++ 零开销直调与蓝图 BlueprintNativeEvent 覆写）
 */

#pragma once

#include "CoreMinimal.h"
#include "Core/FPoolAcquirePayload.h"
#include "UObject/Interface.h"

#include "IGamePoolable.generated.h"

/**
 * @brief 对象池可复用对象反射接口元数据载体
 */
UINTERFACE(BlueprintType, Blueprintable)
class GAMEPOOL_API UGamePoolable : public UInterface {
    GENERATED_BODY()
};

/**
 * @brief 对象池可复用对象生命周期契约接口
 *
 * 回调时序契约：
 * - 普通借出（AcquireActor / AcquireObject）：触发 OnAcquireFromPool；
 * - 带载荷借出（Acquire*WithPayload）：先触发 OnAcquireFromPool 完成通用激活初始化，再触发 OnAcquireFromPoolWithPayload 注入本次借出专属载荷；
 * - 归还入池（ReleaseActor / ReleaseObject）：触发 OnReleaseToPool 执行业务态清理。
 */
class GAMEPOOL_API IGamePoolable {
    GENERATED_BODY()

public:
    /**
     * @brief 从对象池借出并激活时调用（重置通用状态、刷新生命周期）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnAcquireFromPool();
    virtual void OnAcquireFromPool_Implementation() {
    }

    /**
     * @brief 从对象池借出并携带业务载荷时调用（在 OnAcquireFromPool 之后触发，纯 C++ 零堆分配强类型指纹视图，默认无额外操作）
     * @param InPayload 带编译期类型指纹的不可变业务载荷视图
     */
    virtual void OnAcquireFromPoolWithPayload(FPoolAcquirePayload InPayload) {
        (void)InPayload;
    }

    /**
     * @brief 泛型类型安全解包业务载荷静态辅助函数（校验 64 位类型指纹，类型不匹配返回 nullptr）
     * @tparam TPayload 目标载荷类型
     * @param InPayload 载荷视图
     * @return 强类型不可变载荷指针，空载荷或类型不匹配时返回 nullptr
     */
    template <typename TPayload>
    [[nodiscard]] static const TPayload* ExtractPayload(FPoolAcquirePayload InPayload) noexcept {
        return InPayload.Extract<TPayload>();
    }

    /**
     * @brief 归还至对象池休眠时调用（清理引用、重置特效/声音）
     */
    UFUNCTION(BlueprintNativeEvent, Category = "GamePool|ObjectPool")
    void OnReleaseToPool();
    virtual void OnReleaseToPool_Implementation() {
    }
};
