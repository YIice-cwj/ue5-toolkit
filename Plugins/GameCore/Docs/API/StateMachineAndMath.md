# GameCore 轻量状态机与游戏数学库 API 参考

本文档汇总 `GameCore` 提供的纯 C++ 轻量表驱动状态机与帧率无关游戏数学工具：
- `TGameStateMachine<TStateId, TContext>` (`Public/State/TGameStateMachine.h`)
- `FGameMath` (`Public/Math/FGameMath.h`)

---

## 1. 轻量表驱动状态机 `TGameStateMachine`

头文件：`#include "State/TGameStateMachine.h"`

面向 UI 阶段流转、关卡流程、机关交互、武器装填等无需引入完整 StateTree 重型资产的场景，提供零 UObject 开销的纯 C++ 确定性有限状态机：

- **显式转换表与条件守卫**：支持精确边（`AddTransition`）与任意源状态通配边（`AddAnyTransition`，精确边优先于通配边），每条边可附带布尔守卫谓词。
- **重入安全保护**：若在 `OnEnter` / `OnUpdate` 回调内部请求状态跳转（`TransitionTo` 或 `EvaluateTransitions`），状态机自动将其排入单槽延迟队列并在当前回调栈安全返回后顺序提交，彻底杜绝深层递归与半初始化状态。
- **驻留时长追踪**：内置 `GetStateElapsedSeconds()`，每次状态切换自动归零并在 `Tick(Context, DeltaSeconds)` 中累加。

```cpp
enum class EWeaponState : uint8 { Idle, Firing, Reloading };
struct FWeaponContext { int32 Ammo = 0; };

TGameStateMachine<EWeaponState, FWeaponContext> WeaponFsm;
WeaponFsm.AddState(EWeaponState::Idle);
WeaponFsm.AddState(EWeaponState::Firing,
    [](FWeaponContext& Ctx) { --Ctx.Ammo; });
WeaponFsm.AddState(EWeaponState::Reloading,
    nullptr,
    [](FWeaponContext& Ctx, float Dt) { /* 装填计时 */ },
    [](FWeaponContext& Ctx) { Ctx.Ammo = 30; });

WeaponFsm.AddTransition(EWeaponState::Firing, EWeaponState::Reloading,
    [](const FWeaponContext& Ctx) { return Ctx.Ammo <= 0; });

FWeaponContext Ctx{1};
WeaponFsm.Start(EWeaponState::Firing, Ctx);
WeaponFsm.EvaluateTransitions(Ctx); // 自动转入 Reloading
```

---

## 2. 帧率无关游戏数学库 `FGameMath`

头文件：`#include "Math/FGameMath.h"`

弥补原生 `FMath::FInterpTo` 阻尼在高低帧率（30 FPS vs 240 FPS）下轨迹不一致及普通弹簧过冲震荡问题：

| 接口 | 说明 |
| :--- | :--- |
| `ExpDecayFloat` / `ExpDecayVector` / `ExpDecayQuat` | 基于解析指数衰减 $1 - e^{-\text{HalfLifeRate} \cdot \Delta t}$（以半衰期 `HalfLifeSeconds` 参数化）的精确帧率无关平滑阻尼，任意子步切分结果严格一致 |
| `CriticalSpringDampFloat` / `CriticalSpringDampVector` | 二阶临界阻尼弹簧解析积分器（维护位置 + 速度状态结构体 `FSpringFloatState` / `FSpringVectorState`），最快无过冲收敛至动态移动目标 |
| `SignedHorizontalAngleDegrees` | 计算世界空间从前向向量到目标方向在水平面（XY）上的带符号偏航角 $[-180^\circ, 180^\circ]$（八向受击动画、转向混合常用） |
| `RemapClamped` | 安全区间重映射并钳制（输入区间退化为零长时安全返回输出起点，绝不产生 `NaN` 除零） |
| `WrapAngleDegrees` | 将任意角度归一化折叠至 $(-180^\circ, 180^\circ]$ |
