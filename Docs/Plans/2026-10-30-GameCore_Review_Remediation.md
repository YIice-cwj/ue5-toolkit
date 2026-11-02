# [L3 任务计划]：GameCore 审查问题根因修复

> **关联准则**：[B2 计划先行](../../../../ai-guidelines/guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B5 深度验证](../../../../ai-guidelines/guidelines/00-行为规范/01-核心铁律与五阶执行模型.md) · [B7 根因修复](../../../../ai-guidelines/guidelines/00-行为规范/04-Bug根因诊断与全局修复.md)
> **任务等级**：L3 复杂缺陷修复（对象池 / 并发生命周期 / 响应式通知 / 时间状态机）
> **状态**：已批准 (APPROVED，用户明确选择方案 C)

---

## 1. 背景与核心目标

- **现状**：上一轮审查确认 5 个阻断项与 1 个警告项。根因分别是对象池缺少活跃归属且跨外部回调保存可失效容器引用、订阅路径共享所有权断裂、值提交与通知派发缺乏顺序契约，以及时间状态切换丢弃剩余 Delta。
- **当前基线**：工作区已有未提交的 Time 重构、测试和 README 修改。实施时只在当前文件上追加本任务所需 hunk，禁止恢复、覆盖或格式化用户既有修改。
- **目标**：保持公开 API 兼容，消除对象池重入与外部归还破坏、Subscribe/Unregister 竞态 UAF、同键通知倒序与回放竞态；统一暂停文档契约；修正跨阶段时间推进，并补齐可复现回归测试。
- **非目标**：
  - 除统一通知调度器外，不新增其他框架、插件、依赖或公共 API 断代，不改变现有同步回调的正常单线程语义。
  - 不调整缓存、加载、消息、IO 等未发现问题模块，不做无关重构与大面积格式化。
  - 不重新生成 Doxygen HTML，不执行 `git commit` 或 `git push`。

---

## 2. 方案对比决策矩阵

| 维度 | 方案 A：归属不变式 + 每键 FIFO | 方案 B：逐点防御修补 | 方案 C：统一全局调度架构 [已批准] |
|---|---|---|---|
| **核心机制** | 对象池显式 Active 集合；订阅持有 Shared；各 Key 独立 FIFO、唯一 drainer；时间余量跨阶段消费 | 增加判空/互斥/重复判断，回调顺序基本保持原实现 | 所有配置与观察者接入统一事件循环和线程调度器 |
| **优势** | 从状态源头根治；同键按提交序且允许重入；无需持锁执行用户回调；公共签名不变 | 初始改动较少 | 可统一更多未来事件类型 |
| **劣势** | 重入及竞争期间写调用可能只入队后即返回；需明确队列生命周期与测试 | 无法同时解决提交倒序、同键递归和容器悬垂，容易遗留镜像缺陷 | 爆炸半径大、引入当前无需求的线程与队列复杂度 |
| **爆炸半径** | Pool、Listener、Config、Time 及对应测试 | 同左但分散于分支 | 整个响应式与任务系统 |
| **迁移成本** | 中，分模块步步编译验证 | 低，但维护债务保留 | 高 |

- **选型结论**：按用户明确批准采用方案 C。Config 与 ValueObserver 统一接入单例全局 FIFO 通知调度器；值提交、监听器快照捕获和事件入队在源状态锁内完成，唯一 drainer 在锁外执行回调。调度器由调用线程 drain，不创建裸线程，正常无竞争调用仍同步完成；对象池采用活跃归属和短容器引用，与通知架构保持解耦。

---

## 3. 目标设计与实施步骤

### Step 1：补充真实回归测试（Red）

- 对象池：覆盖外部 Actor 归还、重复归还、OnAcquire 内归还/销毁自身、回调内跨类借出触发 `PoolMap` 扩容，以及归还后精确统计。
- Config/ValueObserver：用 `FEvent`、`TFuture` 或 Latent 命令构造确定交错，覆盖通知 1 阻塞时提交 2、同键回调重入、多订阅者顺序和订阅回放；禁止 sleep。
- Registry：覆盖 Subscribe/Unregister/Reset 并发后订阅仍由共享所有权保护，不跨线程访问 UObject。
- Time：覆盖暂停时 HitStop/通道冻结及恢复不突跳；`Advance(1.5)` 跨越 1 秒持续与 1 秒淡出边界时精确校验插值。
- **验证门禁**：新增测试编译；可安全运行的旧实现用例出现预期失败，潜在 UAF 路径以严格因果推导记录，不故意制造进程崩溃。

### Step 2：重建对象池归属与重入不变式

- `FPoolBucket` 增加 `ActiveActorSet`；Acquire 在生命周期回调前登记 Active 并计数，Release 仅接受 Active 命中者并先摘除再回调，OnDestroyed 仅对实际 Active 移除扣减。
- Acquire 回调后确认 Actor 仍为有效 Active 实例；若回调已归还或销毁，则不向调用方返回伪成功结果。
- `Bucket&` 只存在于无外部调用的短作用域；Spawn、Destroy、`IGamePoolable` 回调后重新 `Find`。同步排查 Prewarm、容量裁剪、ClearPool/ClearAllPools 的同类重入路径。
- 保持现有同步生命周期回调和公共签名；更新受影响函数 Doxygen 与接口契约。
- **验证门禁**：Step 1 对象池用例通过，现有借还、预热、容量、清池和外部销毁测试无回归。

### Step 3：封闭订阅生命周期并接入统一全局通知调度器

- `FObservableRegistry::Subscribe` 在注册表锁内查找或创建并取得局部 `TSharedPtr`，出锁后完成订阅；现有 `Register` 公共签名保留。
- 新增单一 `FNotificationDispatcher`，由 Config 与 ValueObserver 共用：
  1. 全局 FIFO 保存类型擦除通知；源状态锁内完成值提交、监听器快照捕获、事件入队和唯一 drainer 选举。
  2. drainer 释放全部源状态锁与队列锁后逐项执行回调；并发或重入写只入队，由当前 drainer 继续派发。
  3. 队列判空与撤销 drainer 在同一队列锁内完成；调用线程 drain，不创建裸线程，正常无竞争调用仍同步完成回调。
  4. 事件捕获提交时监听器快照，保证订阅只接收注册后的通知；回放使用仅含新订阅者的事件快照。
- `SubscribeWithReplay` 的回放进入同一全局顺序机制并只投递给新订阅者，避免“新通知后再回放旧值”。
- `FObserverBase` 以 Config 为开关唯一真源，移除可由并发通知写回的镜像 EnabledFlag；保留 SetEnabled/IsEnabled 公共行为，并以现有性能用例检查额外短读锁开销。
- 明确文档：通知严格按全局入队序派发，重入通知不递归；不同键共享队列并可能产生队头阻塞；任何用户回调均不在数据锁内执行。
- **验证门禁**：全局顺序、并发倒序、同键重入、多监听者、回放、禁用与版本测试通过；现有 Config、ValueObserver、ObservableRegistry、ListenerHub 测试无回归。

### Step 4：修正时间契约与跨阶段推进

- 保留“游戏暂停时 HitStop/通道冻结，仅刷新真实时间基准”的当前实现，修正 `IsTickableWhenPaused` 文档承诺。
- `FGlobalDilationMixer::Advance` 对每个通道用局部剩余 Delta 依次消费持续和淡出阶段；持续结束后的余量立即进入淡出，不传给其他通道。
- 更新受影响函数 Doxygen，不扩展其他时间 API。
- **验证门禁**：暂停/恢复、精确边界、单次大 Delta、多通道独立推进和既有 TimeStateContainer 测试通过。

### Step 5：深度验证与交付

- 编译 `UEToolkitEditor Mac Development`，再运行新增定向测试、`Game.Core` 全前缀和必要的共享 Config/Listener 消费方回归。
- 检查正常流、边界极值、重入、并发、生命周期、禁用状态、队列清空和性能开销；执行 `git diff --check`，确认无业务行内注释、无无关文件变更。
- 仅同步受影响的 API/架构文档；最终提供目标 diff、编译结果、逐项测试统计及六项问题关闭依据，不提交代码。

---

## 4. 风险研判与应急回滚预案

| 风险 | 控制措施 | 回滚路径 |
|---|---|---|
| 全局队头阻塞及竞争期间写调用提前返回 | 用户已接受 C 方案权衡；正常单线程仍同步；文档明确全局 FIFO 与跨键阻塞语义 | 反向移除统一调度器及各接入 hunk，保留回归测试作为复现 |
| 持续生产导致全局待派发事件积压 | 保留逐次通知语义，不静默丢事件；测试完成后确认队列排空和 drainer 复位 | 回退统一调度器接入，不影响对象池与时间模块 |
| 池回调自归还后 Acquire 返回空 | 该路径此前返回已休眠/已销毁 Actor 属伪成功；以 Active 归属为准并补测试 | 仅回退回调后结果校验，保留归属修复 |
| IsEnabled 改为短读 Config 增加开销 | 消除镜像失配与构造竞态；运行现有性能测试和热点调用检查 | 可在保序机制上恢复缓存，但须另补版本同步证明 |
| 覆盖用户已有 Time 修改 | 所有编辑基于当前文件精确替换；禁止整文件恢复和覆盖 | 不使用会回退整个文件的 Git 命令 |

### 客观验收命令

```bash
"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" UEToolkitEditor Mac Development -project="/Users/yiice/code/UE5/UEToolkit/UEToolkit.uproject" -NoUBA -NoTrace -WaitMutex
python3 .scripts/run_automation_tests.py --case Game.Core
python3 .scripts/run_automation_tests.py --all
git diff --check -- Plugins/GameCore
```

- **通过标准**：编译成功且无新增错误/警告；新增回归用例真实执行并通过；`Game.Core` 无 Fail/Error；相关共享模块无新增回归；六项审查发现逐项关闭；工作区既有修改全部保留。
