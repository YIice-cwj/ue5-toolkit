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

### 2.2 响应式监听模式
配置中心支持实时监听。当 GM 工具、热更新或外部脚本修改配置时，业务层自动响应更新：

```cpp
class UWeaponManager : public UObject {
public:
    void Init() {
        // 订阅武器全局暴击率倍率
        CritRateSub = Game::Config::FConfigRegistry::Get().WatchConfig<float>(
            TEXT("Combat.GlobalCritRateMultiplier"),
            [this](float NewMultiplier) {
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

