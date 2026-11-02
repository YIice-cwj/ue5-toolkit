# GameGameplay 审查缺陷修复实施计划

> **任务等级**：L3 复杂修复  
> **状态**：已批准（APPROVED）  
> **关联准则**：B2 计划先行、B3 最小变更、B5 深度验证、B-CMT 注释同步、Expand-Contract

## 1. 背景与核心目标

### 现状与问题

审查确认三个阻断根因：Bootstrap 允许数据通知绕过 ActorInfo 初始化并丢失能力清理句柄；GameplayCue 与 GameFeedbackAdapter 各自表达主通道决策且 Cue 使用错误 Tag；若干函数违反 Doxygen 强制契约。另有属性首值广播时机、近零伤害复制、测试假阳性与越界风险。

### 目标与产出

- 任意 `NotifyDataReady` / `EnsureInitialized` 调用顺序下，初值只按 ASC 生命周期写入，授予能力均能被跟踪和清理。
- GameplayCue 与 GameFeedbackAdapter 共用同一主通道选择契约。
- 保留现有公开函数签名，默认属性映射不再执行无观察者的首次广播。
- 非零伤害经网络序列化后保持数值与通道明细一致。
- 修复测试假阳性、越界及关键回归覆盖，补齐所有触及函数的 Doxygen。

### 非目标

- 不引入异步首次广播、新定时器或新的运行时子系统。
- 不重构 GameFeedback 架构，不修改无关模块和资产。
- 不删除现有工厂参数，不做公开 API 一次性断代。
- 不执行 `git commit` 或 `git push`。

## 2. 方案对比决策矩阵

| 维度 | 方案 A：契约收口 + 兼容修复（推荐） | 方案 B：逐点局部修补 | 方案 C：异步广播与接口重设计 |
|---|---|---|---|
| 核心机制 | Bootstrap 只消费已缓存且完成 ActorInfo 初始化的上下文；主通道选择上移至 `FGameDamageResult`；保留工厂签名并关闭默认无观察者广播 | 在 Cue 内复制最大通道循环，在 Notify 中增加局部判空；其余逐项打补丁 | 工厂返回后下一帧广播，新增初始化状态与异步生命周期 |
| 优势 | 根因修复、单一决策源、公开契约兼容、测试可锁定行为 | 修改文件较少，表面实施快 | 能保留“自动首值广播”语义 |
| 劣势 | 触及 GameGameplayCore 与 GameFeedback 各一处调用 | 主通道逻辑仍重复，容易再次漂移；局部判空不能清晰表达状态机 | 引入 World/Timer/弱引用时序，复杂度和回归面显著增加 |
| 爆炸半径 | 三个直接相关插件的既定函数与测试 | GameGameplay 为主 | GameGameplay 公开 API 与所有属性订阅方 |
| 迁移成本 | 低，Expand-Contract，无签名删除 | 低 | 高，需要协调蓝图与 C++ 调用方 |

**选型结论**：采用方案 A。它用最少的状态约束根治能力句柄问题，并把已经存在于两个模块的“主通道”业务决策归位到伤害结果值对象；不通过异步机制掩盖属性广播的时序问题。

## 3. 文件边界

- 修改 `Plugins/GameGameplay/Source/GameGameplay/Private/Ability/UGameAbilitySystemBootstrap.cpp`：只使用已初始化上下文，移除委托回退授予路径。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/Public/Ability/UGameAbilitySystemBootstrap.h`：同步私有声明和 Doxygen。
- 修改 `Plugins/GameGameplayCore/Source/GameGameplayCore/Public/Damage/FGameDamageResult.h`：新增主实际伤害通道查询。
- 修改 `Plugins/GameFeedback/Source/GameFeedback/Private/Adapter/GameFeedbackAdapter.cpp`：调用统一查询，删除重复循环。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/Private/Bridge/UGameplayCueNotify_GameFeedback.cpp`：从伤害结果写入主通道。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/Private/Damage/FGameGameplayEffectContext.cpp`：精确零值序列化及辅助函数文档。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/{Public,Private}/Attribute/UGameplayAttributeValueMapping.*`：默认广播契约与文档。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/Public/Damage/UGameDamageExecution.h`：补齐执行入口文档。
- 修改 `Plugins/GameGameplay/Source/GameGameplay/Private/Tests/UGameAbilitySystemGlobalsTest.cpp`：回归测试与测试安全修复。

不创建新的生产类或生产文件。

## 4. 实施步骤

### Task 1：Bootstrap 状态机回归与根治

- [ ] 在现有 Bootstrap 测试中增加前置 `NotifyDataReady()` 场景，配置真实 ASC、Avatar、初值回调和默认能力；断言第二次引导不重复写初值、不重复授予，并在 `EndPlay` 后清空本组件授予的能力。
- [ ] 编译并运行 `python3 .scripts/run_automation_tests.py --prefix Game.Gameplay`，确认新增用例在旧实现上因句柄丢失或重复初值失败。
- [ ] 将 `AdvanceAuthoritativeInitialization` 改为只读取 `CurrentASC`，并要求 `CurrentASC`、`CurrentAvatar` 已有效；删除 `ResolveASC` 的委托回退路径，`GrantDefaultAbilities` 同样只消费缓存 ASC。
- [ ] 保留 `EnsureInitialized` 中现有 `InitAbilityActorInfo` 调用顺序；前置数据通知只提前返回，后续正常握手会再次推进，无需额外缓存通知状态。
- [ ] 同步函数文档头并重跑 Bootstrap 与 RetryGate 用例，预期全部通过。

核心约束：

```cpp
UAbilitySystemComponent* AbilitySystemComponent = CurrentASC.Get();
if (AbilitySystemComponent == nullptr || CurrentAvatar.Get() == nullptr) {
    return;
}
```

### Task 2：主通道单一决策源

- [ ] 在 `FGameDamageResult` 增加 `GetDominantActualChannel()`，空容器返回无效 Tag，否则返回 `Magnitude` 最大的实际通道；实现以首个元素初始化比较，避免对负值通道作隐含假设。
- [ ] 增加多通道、空通道和负值通道断言；保留现有 GameFeedbackAdapter 的 Slash 100 / Blunt 50 回归场景。
- [ ] 将 GameFeedbackAdapter 和 GameplayCueNotify 都切换到该查询；Cue 只在伤害上下文有效时覆盖 `MatchedTagName`，无伤害上下文仍保持原回退行为。
- [ ] 编译并运行 Game.Gameplay、Game.GameplayCore 与 GameFeedback 相关测试，预期主通道断言全部通过。

核心实现：

```cpp
const FGameDamageChannel* Dominant = nullptr;
for (const FGameDamageChannel& Channel : ActualChannels) {
    if (Dominant == nullptr || Channel.Magnitude > Dominant->Magnitude) {
        Dominant = &Channel;
    }
}
return Dominant != nullptr ? Dominant->ChannelTag : FGameplayTag();
```

### Task 3：属性首值契约兼容修复

- [ ] 将两个工厂重载的 `bBroadcastImmediately` 默认值改为 `false`，保留参数及 `Bind` 的显式立即广播能力，避免破坏既有 C++/Blueprint 签名。
- [ ] 文档明确：工厂阶段尚未返回对象，订阅方应在创建并绑定委托后调用 `QueryAndBroadcast()`；已持有对象重新 `Bind` 时仍可显式选择立即广播。
- [ ] 测试改为使用默认工厂参数，订阅后只调用一次 `QueryAndBroadcast()`，精确断言初值与广播次数为 1，不再用额外广播掩盖默认行为。

### Task 4：网络序列化边界

- [ ] 在 EffectContext 测试中增加 `0 < abs(TotalActualDamage) < KINDA_SMALL_NUMBER` 且带通道明细的往返用例。
- [ ] 运行旧实现，确认总伤害被错误重置为零。
- [ ] 将存在性条件改为 `DamageResult.TotalActualDamage != 0.0f`，保留零值带宽优化和其余序列化布局。
- [ ] 重跑 EffectContext 与 EffectContextLimit，预期普通值、近零值、零值和 255 条上限全部通过。

### Task 5：测试可信性与安全修复

- [ ] Globals 测试改为通过 `UAbilitySystemGlobals::Get()` 断言实际单例类型，并从该实例分配 EffectContext；类型不符立即失败返回。
- [ ] Registry 测试在每次数量断言后检查 `Bindings.Num()`，不符合预期立即返回，禁止继续索引。
- [ ] 保持真实对象与具体业务值断言，不新增全 Mock 同义反复测试，不使用 sleep。

### Task 6：注释同步与全量验收

- [ ] 补齐审查列出的 Bootstrap、DamageExecution、EffectContext 辅助函数、AttributeMapping 回调文档；所有触及函数逐项核对 `@brief/@param/@return`。
- [ ] 执行 `python3 .scripts/run_automation_tests.py --all`，预期所有目标实际出现且通过，无漏跑。
- [ ] 执行 `./.scripts/ci_local.sh`，预期编译、五前缀自动化与网络冒烟通过。
- [ ] 检查最终 `git diff`，确认只包含本计划文件边界内的代码变更；检查无行内业务注释、无无关格式化、无构建产物或临时探针混入。

## 5. 风险与回滚

- **Bootstrap 风险**：过早通知会改为等待正常握手；若宿主只通知而从不调用 `EnsureInitialized`，初始化将不再发生。验收必须覆盖正常宿主握手顺序，仓库调用点检索作为辅助证据。
- **主通道风险**：空伤害上下文继续使用 Cue Tag，有伤害上下文但无数值通道时得到无效 Tag，由注册表按既有标签/默认规则回退。
- **属性兼容风险**：依赖旧默认自动广播的调用方需要订阅后显式查询；公开签名不变，测试给出标准迁移方式。
- **回滚路径**：按 Task 独立反向应用对应补丁即可恢复旧行为；每个 Task 完成后必须编译并通过相关测试，不累积不可定位的大补丁。

## 6. 客观验收指标

- 新增回归用例在旧实现上可复现失败，在修复后全部通过。
- `--all` 输出所有目标均实际执行，无 `Unknown Automation command`、漏跑或失败。
- CI 编译结果为 Succeeded，网络冒烟结果文件报告成功。
- 审查发现的 3 个 BLOCKER 与 5 个 WARNING 全部有点对点代码和测试证据。
- 最终差异不包含无关文件，不执行提交或推送。
