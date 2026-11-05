# GameFeedback 快速上手指南

本文档指导开发者在 10 分钟内完成 `GameFeedback` 插件的安装配置，并接入打击感表现。

---

## 1. 启用插件与模块依赖

在工程根目录的 `*.uproject` 中启用插件：
```json
{
    "Plugins": [
        {
            "Name": "GameFeedback",
            "Enabled": true
        }
    ]
}
```

在需要调用表现广播的模块（例如游戏主模块 `MyGame`）的 `MyGame.Build.cs` 中增加私有依赖：
```csharp
PrivateDependencyModuleNames.AddRange(new string[] {
    "GameFeedback",
    "GameplayTags",
    "GameplayMessageRuntime"
});
```

---

## 2. 创建并配置全局注册表 (Registry)

1. 在内容浏览器右键 $\to$ **Miscellaneous** $\to$ **Data Asset**；
2. 选择基类 `GameFeedbackRegistry`，命名为 `DA_GlobalFeedbackRegistry`；
3. 打开编辑：
   - **`DefaultFallbackDefinition`**：指定通用的默认打击资产（如普通轻击）；
   - **`ExactChannelMap`**：添加通道映射（如 `Damage.Channel.Slash` $\to$ `DA_Feedback_Slash`）；
   - **`ExactFeedbackTagMap`**：添加标签映射（如 `Feedback.State.Lethal` $\to$ `DA_Feedback_Lethal`）；
   - **`CompositeRules`**：可按需配置复杂的组合查询条目。
4. 打开 **Project Settings** $\to$ **Game** $\to$ **Game Feedback**：
   - 将 `GlobalRegistry` 指向刚才创建的 `DA_GlobalFeedbackRegistry`。

---

## 3. 创建表现配置资产 (Definition)

1. 在内容浏览器右键 $\to$ **Miscellaneous** $\to$ **Data Asset**；
2. 选择基类 `GameFeedbackDefinition`，命名为 `DA_Feedback_HeavyBlunt`；
3. 打开配置面板，根据需求配置 6 组原子表现：
   - **HitStop**：`VictimDuration = 0.12f`，`AttackerDuration = 0.06f`，`TimeDilation = 0.001f`；
   - **CameraShake**：指定攻击者与受击者的镜头震动蓝图类；
   - **HitFlash**：指定受击闪白叠加材质（`OverlayMaterial`），`Duration = 0.12f`；
   - **VFX**：指定命中 Niagara 特效（软引用，按需加载）；
   - **Audio**：指定空间打击音效与攻击者 2D 命中确认音；
   - **Haptics**：指定手柄力反馈资产。

---

## 4. 广播打击表现信号

### 4.1 C++ 原生直接广播
```cpp
#include "Bus/GameFeedbackBlueprintLibrary.h"
#include "Feedback/GameFeedbackSignal.h"

void AMyWeapon::OnHit(AActor* TargetActor, const FHitResult& HitResult)
{
    FGameFeedbackSignal Signal;
    Signal.Instigator = GetInstigator();
    Signal.DamageCauser = this;
    Signal.Target = TargetActor;
    Signal.HitResult = HitResult;
    Signal.Magnitude = 50.0f;
    Signal.PrimaryChannel = FGameplayTag::RequestGameplayTag(TEXT("Damage.Channel.Blunt"));

    // 统一入口：按注册表自动解析最优表现
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(this, Signal);
}
```

### 4.2 蓝图直接广播
在蓝图中搜索调用节点：**Game Feedback** $\to$ **Broadcast Feedback**：
- 连接 `World Context Object`；
- 构建 `Signal` 结构体引脚并传入；
- `Definition` 引脚默认保持为 `None`（自动路由）。

---

## 5. 结合战斗管线无缝接入

### 5.1 配合 `GameGameplayCore` 伤害管线
```cpp
#include "Adapter/GameFeedbackAdapter.h"
#include "Bus/GameFeedbackBlueprintLibrary.h"

void AMyCharacter::HandleDamageResult(const FGameDamageContext& Context, const FGameDamageResult& Result)
{
    // 一键由伤害结算数据构造标准表现信号
    const FGameFeedbackSignal Signal = UGameFeedbackAdapter::MakeSignalFromDamage(this, Context, Result);
    UGameFeedbackBlueprintLibrary::BroadcastFeedback(this, Signal);
}
```

### 5.2 配合 GAS `GameplayCue`
1. 新建蓝图类，继承自 `GameplayCueNotify_GameFeedback`；
2. 填写对应 Cue 标签（如 `GameplayCue.Combat.Hit`）；
3. 伤害结算 GameplayEffect 触发该 Cue 时，框架自动完成向表现总线的转发与客户端预测去重。

---

## 6. 状态清理与复位

当受击者死亡、进入虚无/无敌状态或场景重置时，通过单行代码即可清除受击闪白 Overlay 与正在执行的物理顿帧：

```cpp
UGameFeedbackBlueprintLibrary::BroadcastClearFeedback(this, DyingCharacter);
```
该操作会向 `Feedback.Channel.Clear` 广播消息，相关监听者立即安全复原 Actor 的原始外观与时钟流速。
