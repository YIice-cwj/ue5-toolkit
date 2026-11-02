# [L3 任务计划]：GameFeedback 消息总线 + 组件化重构（C 方案）

> **关联准则**：[B2 计划先行](../../guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [R-ARCH-CLASS 职责边界与文件组织](../../guidelines/50-审查规范/01-代码审查规范.md)
> **任务等级**：[ L3 复杂重构 ]
> **状态**：已批准 (APPROVED) —— 用户于 2026-10-30 明确选择「C 方案：消息总线/组件化，完美解耦而不是上帝类」

---

## 1. 背景与核心目标

- **现状**：A 方案虽已把状态拆为追踪器，但 `UGameFeedbackSubsystem` 仍同时承担生命周期、资产解析、视角判定、顿帧、闪白、震动、VFX、音效与网络派发，是编排上帝类；调用方与能力实现仍通过同一个门面耦合。
- **目标**：以 GameCore `UGameplayMessageSubsystem` 为唯一总线，把每种表现能力下沉为独立组件 / 独立服务；组件只订阅消息、只处理自身领域，任何类都不持有全部能力。
- **非目标**：不改动伤害契约、TimeDilation 契约与 Registry/Definition 资产结构；不生成二进制示例资产。

---

## 2. 目标拓扑（无中心上帝类）

```text
调用方 / GAS Cue / 适配器
        │  BroadcastFeedback(Signal)
        ▼
UGameplayMessageSubsystem  ── Feedback.Channel.Play
        │
        ├── UGameFeedbackWorldComponent(GameState)      闪白 / VFX / 空间音
        ├── UGameFeedbackPlayerComponent(PlayerController) 镜头震动 / 触觉 / 确认音 / 预测记录
        ├── UGameFeedbackHitStopService(WorldSubsystem) 顿帧限速 / 累积预算 / 全局定格
        └── UGameFeedbackNetworkDispatcherComponent(GameState) 权威端派发

客户端收到 RPC
        ▼
UGameFeedbackReceiverComponent(PlayerController, 服务端复制)
        │  BroadcastNetworkFeedback(Signal, bSuppressed)
        ▼
Feedback.Channel.Network ── 上述监听者（抑制时整段跳过）
```

- **总线契约**：`GameFeedbackChannels.h`（Play / Network / Clear）与 `GameFeedbackMessages.h`。
- **公共入口**：`UGameFeedbackBlueprintLibrary` 静态广播；调用方不再依赖任何子系统单例。
- **资产解析**：`UGameFeedbackDefinitionProvider`（WorldSubsystem）唯一负责 Registry 加载与定义解析/预热。
- **组件装配**：`UGameFeedbackRuntimeSubsystem`（WorldSubsystem）仅做接线：为 GameState 与本地 PlayerController 惰性添加组件，不含业务逻辑。

---

## 3. 实施步骤（分步可编译）

- [x] **Step 1：总线契约与公共入口**：Channels / Messages / BlueprintLibrary，删除旧 `UGameFeedbackSubsystem` 公共入口。✅
- [x] **Step 2：共享服务**：DefinitionProvider（资产解析）、HitStopService（顿帧全责）。✅
- [x] **Step 3：能力组件**：WorldComponent、PlayerComponent、DispatcherComponent，改写 Receiver 为「RPC → 总线」。✅
- [x] **Step 4：装配与迁移**：RuntimeSubsystem 接线；GAS Cue 桥接改调广播库；删除旧子系统与旧派发器。✅
- [x] **Step 5：验证与文档**：编译 `Result: Succeeded`；README/插件矩阵同步；自动化测试因本机 DDC 环境崩溃未取证（如实记录）。✅

---

## 4. 风险与回滚

- **风险**：消息监听时序（Play/Network 双通道）、复制组件与本地组件并存、客户端预测抑制的跨 PC 边界。
- **回滚**：组件均为独立新增文件；保留旧子系统文件于 Git 历史可恢复。
- **验收**：`Build.sh UEToolkitEditor Mac Development` → `Result: Succeeded`；PlayerComponent/WorldComponent/Dispatcher 各自无跨域职责；旧子系统与旧派发器零残留。
