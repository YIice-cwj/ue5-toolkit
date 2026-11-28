# GameNetService

> **一句话**：跨地图常驻的 WebSocket 长连接门面，统一编排传输、心跳、退避重连、异步 RPC、消息总线桥接与远程日志转发。
> **版本** v1.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 2 · **直接依赖** GameNetCore, GameCore, GameMessage, GameLogging

## 30 秒速览

- **解决什么问题**：把登录、大厅、商城、背包等业务长连接的连接管理、心跳保活、断线重连和 Request-Response 请求收进一个常驻子系统，业务代码只面向强类型结构体。
- **核心能力**：
  - `UGameNetServiceSubsystem` 跨 Seamless Travel 存活，默认装配内存回环驱动与 JSON 序列化器；
  - WebSocket 分片重组、有界无锁跨线程驱动与超长帧拦截；
  - 指数退避重连（双向随机抖动）与周期心跳 / 滑动平均 RTT；
  - `SendStruct` / `SendStructRequest` / `RequestStructAsync` 强类型发送与可取消异步 RPC；
  - 消息总线桥接与 `TAG_Game_Log_Remote` 远程日志自动转发。
- **什么时候用**：需要与网关保持长连接、做业务请求响应或把远程日志送往后端分析平台。
- **什么时候不用**：局内高频位移与属性同步交给引擎 `UNetDriver` / Iris；只需要线格式契约时直接依赖 `GameNetCore`；局域网房间发现用 `GameNetSession`。

## 快速开始

```cpp
#include "System/GameNetServiceSubsystem.h"
#include "Transport/NetTransportFactory.h"

UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(this);

NetService.SetTransport(FNetTransportFactory::CreateWebSocketTransport());
NetService.Connect(TEXT("wss://gateway.mygame.com/ws"));

NetService.SetRemoteLogForwardingEnabled(true);

const TGameStatusResult<uint32> SendResult = NetService.SendStructRequest<FShopBuyReq, FShopBuyRsp>(
    5001,
    5002,
    BuyReq,
    5.0f,
    [](ENetRequestResult Result, const FShopBuyRsp* Rsp) {
        if (Result == ENetRequestResult::Success && Rsp != nullptr) {
            // 回调保证在 GameThread 执行，可直接刷新 UI 或修改 UObject
        }
    }
);
```

## 核心能力

### 常驻门面子系统

- `UGameNetServiceSubsystem` 继承 `UGameInstanceSubsystem`，跨地图与 Seamless Travel 存活。
- 默认装配 `FMockNetTransport` 与 JSON 序列化器；联机时通过 `SetTransport` 注入 WebSocket 驱动。
- `FScopedSendBuffer` 在重入发送时自动回退栈局部缓冲，避免共享缓冲被覆写。
- `GetPtr` 支持 `UWorld`、`UGameInstance`、`UGameInstanceSubsystem` 与场景对象寻址。

### WebSocket 分片与跨线程驱动

- `FWebSocketFragmentAssembler` 以只读视图拼装多分片，返回 `TConstArrayView<uint8>`，超大帧后收缩容量。
- `FWebSocketNetTransport` 使用 `TLockFreeRingBuffer<FInboundSocketEvent, 1024>` 有界无锁事件通道，满队时熔断。
- 主线程用 64 位代际号 `ActiveGeneration` 防止旧连接事件串扰；IO 线程对超长帧零分配前置拦截。
- 单分片整包走零拷贝直通快路径，多分片才进入 Assembler 与环缓冲解帧。

### 重连与心跳

- `FNetReconnectConfig` 默认：基础等待 1 s、上限 30 s、最多 5 次、抖动比例 0.25。
- 默认使用运行时熵种子生成双向随机抖动，避免同步重连雪崩；测试可注入确定性种子复现。
- `FNetHeartbeatConfig` 默认：间隔 15 s、静默超时 45 s，Ping / Pong 使用预留 ID `1` / `2`。
- `FMovingAverageSampler<8>` 维护滑动平均 RTT，超时失步触发断线结算。

### 请求追踪与消息分发

- `FNetPendingRequestTracker` 负责非零 `SequenceId` 分配、单次查找配对、超时扫描与断线结算。
- 支持 `FCancellationToken` 主动取消；`CancelPendingRequest(SequenceId)` 可立即触发 `Cancelled` 回调。
- `FNetMessageDispatcher` 在监听器 ≤4 时使用栈内零分配快照多播，超限回退堆分配。
- 收包顺序为先匹配挂起请求，再按 `MessageId` 路由常驻处理器。

### 协程 RPC

- `RequestStructAsync<TReq, TRsp>` 返回 `TNetRequestStructAwaiter`，协程恢复后交付 `TGameResult`。
- 等待器只保留单次结果槽位，避免额外状态堆分配与响应结构体二次拷贝。
- 支持生命周期守卫与 `FCancellationToken` 联动，取消或断线时以失败结果恢复。

### 消息总线桥接与远程日志

- `FNetMessageBusBridge` 基于 `UGameplayMessageSubsystem` 订阅 `Game.Net.Outbound`，广播 `Game.Net.Inbound` / `Game.Net.StateChanged`。
- 入站热路径通过 `HasListener` 做无订阅者零分配短路。
- 远程日志默认监听 `TAG_Game_Log_Remote`，打包为 `NET_MSG_REMOTE_LOG (100)` 外发，可由 `SetRemoteLogForwardingEnabled` 开关。

## 架构与依赖

| 项目 | 内容 |
| :--- | :--- |
| 模块名 | `GameNetService`（`GAMENETSERVICE_API`） |
| 公有引擎依赖 | Core, CoreUObject, Engine, GameplayTags |
| 私有引擎依赖 | WebSockets |
| 公有插件依赖 | GameCore, GameMessage, GameNetCore |
| 私有插件依赖 | GameLogging |

- 门面：`System/GameNetServiceSubsystem.h`
- 传输：`Transport/NetTransportFactory.h`、`WebSocketNetTransport.h`、`WebSocketFragmentAssembler.h`
- 策略：`Policy/NetReconnectPolicy.h`、`Policy/NetHeartbeatKeeper.h`
- 分发：`Dispatch/NetMessageDispatcher.h`、`Dispatch/NetPendingRequestTracker.h`
- 协程与桥接：`Async/TNetRequestStructAwaiter.h`、`Bridge/NetMessageBusBridge.h`

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameNetService/
├── Source/
│   └── GameNetService/
│       ├── Public/
│       │   ├── GameNetService.h
│       │   ├── Async/         # TNetRequestStructAwaiter C++20 协程 RPC 等待器
│       │   ├── Bridge/        # FNetMessageBusBridge 消息总线双向桥接器
│       │   ├── Dispatch/      # NetDispatchTypes、FNetMessageDispatcher、FNetPendingRequestTracker
│       │   ├── Policy/        # NetPolicyTypes、FNetReconnectPolicy、FNetHeartbeatKeeper
│       │   ├── System/        # UGameNetServiceSubsystem 薄门面常驻子系统与日志转发
│       │   └── Transport/     # WebSocketTransportTypes、FWebSocketFragmentAssembler、FWebSocketNetTransport、FNetTransportFactory
│       ├── Private/
│       │   ├── Bridge/
│       │   ├── Dispatch/
│       │   ├── Policy/
│       │   ├── System/
│       │   ├── Transport/
│       │   └── Tests/         # 单元测试与多轮统计性能基准（GameNetServicePerformanceTest）
│       └── GameNetService.Build.cs
├── Docs/
│   └── Architecture/
│       └── Overview.md
├── GameNetService.uplugin
└── README.md
```

</details>

## 性能与边界

- **线程**：后台 Socket 线程只入队，解帧、反序列化、超时结算与回调全部在主线程。
- **连接默认值**：未注入驱动时使用 `FMockNetTransport`；真实联机需 `SetTransport(CreateWebSocketTransport())`。
- **请求结算**：断线、超时、主动取消或子系统 `Deinitialize` 都会结算全部挂起请求并回调。
- **回调线程**：`SendStructRequest` 回调保证在 GameThread 执行，可直接操作 `UObject`。
- **日志防循环**：`LogGameNetService` 已纳入远程日志输出设备黑名单，避免转发死循环。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

下表为自动化测试中的中位耗时预算上限，不是仓库固化的实测值；实际数值随机器与构建配置变化。

| 基准用例 | 场景 | 预算 |
| :--- | :--- | :--- |
| `Game.Net.Service.Perf.ReconnectPolicy` | 重连退避计算 | < 200 ns |
| `Game.Net.Service.Perf.HeartbeatKeeper` | 心跳与平滑 RTT 更新 | < 120 ns |
| `Game.Net.Service.Perf.RequestTracker` | 异步请求配对 | < 800 ns |
| `Game.Net.Service.Perf.FragmentAssembler` | 两分片零外拷贝拼装 | < 1500 ns |
| `Game.Net.Service.Perf.WebSocketRxZeroCopy` | WebSocket 收包单帧 | < 1500 ns |
| `Game.Net.Service.Perf.MessageDispatcher` | 多槽位派发 | < 300 ns |
| `Game.Net.Service.Perf.MessageBusBridgeNoListener` | 无监听零分配短路 | < 250 ns |
| `Game.Net.Service.Perf.SubsystemEndToEndRpc` | 子系统端到端 Raw RPC | < 5000 ns |

来源：`Source/GameNetService/Private/Tests/GameNetServicePerformanceTest.cpp`。

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概览与跨线程派发时序](Docs/Architecture/Overview.md) | 组件职责、GameThread 消费模型与网络选型矩阵 |
| [GameNetCore 插件](../GameNetCore/README.md) | 线格式、传输与序列化契约 |
| [GameData 云存档 API](../GameData/Docs/API/GameCloudSaveSubsystem.md) | 使用本插件 RPC 的云存档门面 |
| [根 README](../../README.md) | 插件分层与安装方式 |
