# GameCore 性能基准与预算

本文件记录 `Game.Core.Perf.*` 与 `Game.GameplayCore.Perf.*` 自动化基线的实测结果、预算阈值与复现方式。预算刻意放宽，只拦截数量级退化，不把机器抖动当作失败。

---

## 1. 基准清单

| 测试 | 覆盖路径 | 规模 | 预算 |
| :--- | :--- | :--- | :--- |
| `Game.Core.Perf.VersionedCacheFind` | `FVersionedCache::FindShared`（Eager） | 4096 键 / 200,000 次 | < 2000 ns/op |
| `Game.Core.Perf.ListenerHubThroughput` | `FListenerHub::Emit` + 单订阅回调 | 100,000 次 | < 5000 ns/op |
| `Game.GameplayCore.Perf.StatLookup` | `UGameStatComponent::GetStatCurrent` | 64 属性 / 200,000 次 | < 2000 ns/op |

---

## 2. 实测结果

环境：macOS（Apple Silicon），UE 5.8.1，Development，`-nullrhi -unattended -DDC-ForceMemoryCache`。

| 测试 | 实测 | 预算占比 |
| :--- | :--- | :--- |
| `VersionedCacheFind` | **10.5 ns/op** | 0.5% |
| `ListenerHubThroughput` | **17.2 ns/op** | 0.3% |
| `StatLookup` | **2.2 ns/op** | 0.1% |

日志：`.memory/baseline/p5_perf_test.log`、`.memory/baseline/p5_perf_test_stat.log`。

> 数值为单次自动化运行结果，用于数量级回归；不同机器与后台负载下会有波动。

---

## 3. 复现方式

```bash
UnrealEditor-Cmd <Project.uproject> \
  -ExecCmds="Automation RunTests Game.Core.Perf; Quit" \
  -unattended -nullrhi -nosplash -log -stdout -FullStdOutLogOutput \
  -DDC-ForceMemoryCache
```

```bash
UnrealEditor-Cmd <Project.uproject> \
  -ExecCmds="Automation RunTests Game.GameplayCore.Perf; Quit" \
  -unattended -nullrhi -nosplash -log -stdout -FullStdOutLogOutput \
  -DDC-ForceMemoryCache
```

- 运行前清理残留 `UnrealEditor` / `UnrealTraceServer` 进程。
- 每个父路径单独运行，避免自动化测试父子路径遮蔽。
- 结果中的 `PerfCacheFind:` / `ListenerHubThroughput:` / `StatLookup:` 行即实测值。

---

## 4. 回归处理

- 实测超过预算：先确认是否有后台编译、调试器或性能采样工具干扰。
- 排除环境因素后仍超预算：视为数量级退化，按 B7 溯源到具体提交与调用路径。
- 不要为了让测试通过而上调预算；预算变更需在发布说明中记录原因。
