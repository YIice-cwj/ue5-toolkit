# GameCore 编译开关与动态配置指南

本文档介绍如何在工程中正确使用编译期开关与运行时动态配置注册中心。

---

## 1. 编译期开关最佳实践

### 1.1 唯一真源原则
在 `Config/GameCompileSwitches.h` 中，所有宏必须显式定义为数值（0 或 1）：
```cpp
#ifndef GAME_ENABLE_TEST
    #define GAME_ENABLE_TEST 0
#endif

#ifndef GAME_ENABLE_GM
    #define GAME_ENABLE_GM (UE_BUILD_SHIPPING == 0)
#endif
```
随后派生出 `constexpr bool` 常量：
```cpp
namespace Game::Config {
    inline constexpr bool bEnableTest = (GAME_ENABLE_TEST != 0);
    inline constexpr bool bEnableGM   = (GAME_ENABLE_GM != 0);
}
```

### 1.2 为什么必须使用 `if constexpr` 而非 `#if`？
1. **语法与类型检查**：`#if` 内的代码在未激活时完全不被编译器前端解析，容易隐藏语法错误；而 `if constexpr` 保证代码语法和类型必须合法，但未激活分支零指令开销。
2. **可读性**：保留纯粹的 C++ 代码结构，避免满屏 `#ifdef` 破坏 IDE 代码提示和语法高亮。

---

## 2. 动态配置注册中心 (`FConfigRegistry`)

### 2.1 命名空间与只读保护规则
为了避免不同系统配置命名冲突，强烈建议遵循层级命名空间：
- `Game.*`：通用玩法配置
- `Combat.*`：战斗系统参数
- `Env.*`：环境配置（**严格只读**，禁止调用 SetConfig 写入）
- `Dev.*`：开发调试参数（Shipping 环境下自动变为只读）

### 2.2 多源级联桥接 (`FConfigSourceBridge`)
`GameCore` 支持将不同配置来源（如命令行启动参数、INI 配置文件、远端拉取数据）通过 `FConfigSourceBridge` 接入：
- **优先级级联覆盖**：遵循 Commandline > INI > Default 级联生效；
- **PIE 隔离与自动回滚**：编辑器退出 PIE 模式时，自动将游戏实例运行期间的临时修改回滚至快照默认值，防止污染编辑器资产与常驻内存；
- **只读键防篡改**：由 Bridge 声明的只读键，调用 `SetConfig` 时直接失败并记录告警。

### 2.3 响应式监听模式
配置中心支持实时监听。当 GM 工具、热更新或外部脚本修改配置时，业务层自动响应更新：

```cpp
class UWeaponManager : public UObject {
public:
    void Init() {
        // 订阅武器全局暴击率倍率（强类型推导，零变体拆包）
        CritRateSub = FConfigRegistry::Get().WatchConfig<float>(
            TEXT("Combat.GlobalCritRateMultiplier"),
            [this](const float& NewMultiplier) {
                this->CachedCritMultiplier = NewMultiplier;
            }
        );
    }

private:
    float CachedCritMultiplier = 1.0f;
    TSharedPtr<FObserverSubscription> CritRateSub; // RAII 守卫
};
```
当 `UWeaponManager` 被垃圾回收析构时，`CritRateSub` 自动取消注册，杜绝野指针回调。

### 2.4 极速零哈希高频读取模式 (`TCachedConfigHandle`)
对于每帧在 `Tick` 中读取数十上百次的高频系统（如移动速度、重力系数、伤害缩放），手写 `WatchConfig` 和管理局部变量较为繁琐，而直接使用 `GetConfig()` 则每帧都有哈希查找与读写锁开销。  
推荐直接声明 `TCachedConfigHandle<T>` 成员变量：

```cpp
#include "Config/TCachedConfigHandle.h"

class UMovementComponent : public UActorComponent {
public:
    void TickComponent(float DeltaTime, ...) override {
        // 零哈希直接解指针读取，性能等同直接访问局部变量，支持隐式类型转换
        const float CurrentSpeed = BaseSpeed * SpeedMultiplierHandle;
        UpdateVelocity(CurrentSpeed);
    }

private:
    // 自动订阅热更新、自动维护生命周期与 Fallback，消灭模板样板代码
    TCachedConfigHandle<float> SpeedMultiplierHandle{FName(TEXT("Movement.SpeedMultiplier")), 1.0f};
};
```
