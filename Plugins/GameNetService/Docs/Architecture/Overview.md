# GameNetService 架构概览与跨线程安全模型

## 1. 单一职责组件协作与跨线程隔离

```mermaid
sequenceDiagram
    autonumber
    participant WS as IWebSocket 底层回调线程
    participant Trans as FWebSocketNetTransport + FWebSocketFragmentAssembler
    participant Sub as UGameNetServiceSubsystem::Tick (GameThread)
    participant Track as FNetPendingRequestTracker / FNetMessageDispatcher
    participant Bridge as FNetMessageBusBridge

    WS->>Trans: Enqueue(Generation, Event) 至有界 TLockFreeRingBuffer [零 UObject 访问 + 超长帧拦截]
    Sub->>Trans: Tick() 校验代际号，单分片走零拷贝直通快路径，多分片经 Assembler + RingBuffer 解帧
    Sub->>Sub: 更新 FNetHeartbeatKeeper 对端活跃时钟、最新 RTT 与滑动平均 RTT
    Sub->>Track: 先匹配 FNetPendingRequestTracker(SequenceId)，再路由 FNetMessageDispatcher(MessageId)
    Sub->>Bridge: HasListener 门控短路后广播 TAG_Game_Net_Inbound / TAG_Game_Net_StateChanged
```

1. **职责边界清晰拆分**：
   - `FWebSocketFragmentAssembler` 仅负责多段二进制分片零外拷贝拼装（返回只读 `TConstArrayView<uint8>`）、最大帧长拦截与超大帧后的容量收缩；
   - `FWebSocketNetTransport` 负责 `IWebSocket` 生命周期、主线程 64 位代际号（`ActiveGeneration`）防串扰、`TLockFreeRingBuffer<FInboundSocketEvent, 1024>` 有界无锁事件通道（带满队过载熔断）、IO 线程超长帧零分配前置拦截，以及单分片整包零拷贝直通快路径；
   - `FNetTransportFactory` 负责具体传输实例创建（支持注入自定义 `FWebSocketFactory` 便于测试与扩展），使 `UGameNetServiceSubsystem` 仅面向抽象 `INetTransport` 编程；
   - `FNetPendingRequestTracker` 与 `FNetMessageDispatcher` 彻底分离“可取消单次异步 RPC 序列追踪（支持 `FCancellationToken` 与栈内局部快照防重入）”与“常驻 `MessageId` 快照多播”；
   - `FNetMessageBusBridge` 独立封装 `GameMessage`（`UGameplayMessageSubsystem`）的监听句柄、无监听零分配短路门控与双向消息广播。
2. **GameThread 唯一消费铁律**：后台 Socket 线程绝不触碰任何 `UObject`、委托或业务容器；全部解帧、反序列化、超时结算与回调触发 100% 运行在主线程。模块日志统一经 `GameLogging` 输出至 `LogGameNetService`（已在远程日志输出设备中纳入防循环黑名单，避免转发死循环）。

---

## 2. 全插件统一网络接入方式

- **业务层插件 / 主工程模块**：直接依赖 `GameNetService`，包含 `System/GameNetServiceSubsystem.h`，调用 `SendStruct` / `SendStructRequest` / `RequestStructAsync` / `RegisterStructHandler` 强类型模板接口；
- **平级通用插件**：无需依赖 `GameNetService` 或 `WebSockets`，仅依赖零物理网络的 `GameNetCore` 并包含 `Message/GameNetworkMessageTypes.h`，即可通过 `TAG_Game_Net_Outbound`、`TAG_Game_Net_Inbound` 与 `TAG_Game_Net_StateChanged` 完成跨插件收发；
- **基础设施日志联动（远程日志）**：`UGameNetServiceSubsystem` 自动监听 `TAG_Game_Log_Remote`，将 `GameLogging` 输出的 `FGameRemoteLogEntry` 打包为 `NET_MSG_REMOTE_LOG (100)` 外发，实现日志基础设施与网络传输的绝对解耦；
- **数据层云端同步联动（云存档）**：`GameData` 依赖 `GameNetService`，通过 `SendStructRequest` 调度 `NET_MSG_CLOUD_SAVE_*` (201~206) 系列消息，完成高阶分段存档的云端上传、下载与热恢复。

---

## 3. 全插件生态网络选型与边界仲裁矩阵 (Plugin Ecosystem Network Boundary)

在 `UEToolkit` 中，不同物理特性的数据（实时战斗 vs 业务大厅 vs 基础设施）由不同通道负责，严禁“一刀切”强行大一统：

| 插件模块 | 是否接入 GameNet | 网络通信底层驱动 | 架构决策依据与通信模型 |
| :--- | :---: | :--- | :--- |
| **`GameCore`** | ❌ 绝对不接 | 零网络（纯本地内存） | Layer 0 纯净底座，保持纯单机 0 平台依赖 |
| **`GameGameplayCore`** | ❌ 绝对不接 | 引擎原生 UDP 属性复制 | 战斗毫秒级数值计算；TCP/WebSocket 会因队头阻塞导致跳字与时钟失步 |
| **`GameGameplay` (GAS)** | ❌ 绝对不接 | 引擎原生 UDP / RPC | GAS 机制天然强绑定引擎底层网络驱动（UNetDriver / Iris） |
| **`GameFeedback`** | ❌ 绝对不接 | 引擎原生紧凑 UDP 派发 | 顿帧闪白走微型载荷（典型 ≤64B）广播，必须与画面渲染帧严格同步 |
| **`GameLogging`** | ✅ 旁路解耦 | `TAG_Game_Log_Remote` ➔ WebSocket | 远程日志通过消息总线无感广播，NetService 自动代理外发，日志模块自身零网络代码 |
| **`GameData`** | ✅ 门面集成 | `UGameNetServiceSubsystem` RPC | 云端存档上传/下载属于典型大厅应用层强类型 Request-Response，天然契合 WebSocket |
| **`GameNetSession`** | ✅ 组合协同 | 局域网 UDP 信标 + OSS 房间 | 会话管理与长连接网关并存，互相独立互补 |
