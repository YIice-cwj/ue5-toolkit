# GameNetService 插件

**GameNetService** 是 `GameNet` 套件的常驻业务长连接服务插件（Layer 2）。它遵循单文件单类与薄门面编排原则：由跨关卡存活的 `UGameNetServiceSubsystem` 统一编排传输工厂（`FNetTransportFactory`）、WebSocket 分片重组与有界无锁跨线程驱动（`FWebSocketFragmentAssembler` + `FWebSocketNetTransport`）、运行时熵 Full-Jitter 指数退避重连（`FNetReconnectPolicy`）、周期心跳与滑动平均 RTT 测算（`FNetHeartbeatKeeper`）、多播消息分发器（`FNetMessageDispatcher`）、可取消异步 RPC 挂起追踪器（`FNetPendingRequestTracker`）、C++20 协程等待器（`TNetRequestStructAwaiter`）、零分配短路消息总线桥接器（`FNetMessageBusBridge`）以及**远程日志自动转发器**。

---

## 核心特性一览

| 模块 / 机制 | 关键技术亮点 | 核心文件 / 接口 |
| :--- | :--- | :--- |
| **薄门面常驻子系统** | `UGameInstanceSubsystem` 跨地图 Seamless Travel 存活；RAII 重入安全发送缓冲、错误回调自动闭环与多上下文 `GetPtr` 支持 | `System/GameNetServiceSubsystem.h` |
| **传输驱动实例工厂** | 隔离具体驱动构造细节，按需装配 `FMockNetTransport` 或支持自定义工厂注入的 `FWebSocketNetTransport` | `Transport/NetTransportFactory.h` |
| **WebSocket 分片重组与驱动** | `FWebSocketFragmentAssembler` 零外拷贝只读视图拼装与大帧后收缩；`FWebSocketNetTransport` 基于 `GameCore::TLockFreeRingBuffer` 有界无锁环 + 满队熔断、IO 线程超长帧零分配拦截及单分片零拷贝快路径 | `Transport/WebSocketTransportTypes.h`<br>`Transport/WebSocketFragmentAssembler.h`<br>`Transport/WebSocketNetTransport.h` |
| **重连退避与周期心跳** | `FNetReconnectPolicy` 默认采用运行时熵种子（支持测试注入确定性种子）实现 Full-Jitter 指数退避；`FNetHeartbeatKeeper` 负责周期 Ping 触发、最新与 `FMovingAverageSampler<8>` 平滑平均 RTT 测算及超时失步判定 | `Policy/NetPolicyTypes.h`<br>`Policy/NetReconnectPolicy.h`<br>`Policy/NetHeartbeatKeeper.h` |
| **多播路由与可取消 RPC 追踪** | `FNetMessageDispatcher` 零分配整数计数多播；`FNetPendingRequestTracker` 支持 `SequenceId` 避冲突分配、单次查找配对、栈内局部缓冲重入安全超时扫描、`FCancellationToken` 主动取消与断线结算 | `Dispatch/NetDispatchTypes.h`<br>`Dispatch/NetMessageDispatcher.h`<br>`Dispatch/NetPendingRequestTracker.h` |
| **轻量协程 RPC 等待器** | `TNetRequestStructAwaiter` 仅保留单次结果槽位分配（移除额外状态堆分配与响应结构体二次拷贝），支持生命周期守卫与 `FCancellationToken` 联动 | `Async/TNetRequestStructAwaiter.h` |
| **零耦合消息总线桥接器** | 基于 `GameMessage`（`UGameplayMessageSubsystem`）自动订阅 `Game.Net.Outbound` 并广播 `Game.Net.Inbound` / `Game.Net.StateChanged`；入站热路径通过 `HasListener` 实现无订阅者零分配短路 | `Bridge/NetMessageBusBridge.h` |
| **远程日志自动转发** | 自动监听 `TAG_Game_Log_Remote` 总线广播，打包为 `NET_MSG_REMOTE_LOG (100)` 结构体并经由长连接外发远端；支持 `SetRemoteLogForwardingEnabled` 动态开关 | `System/GameNetServiceSubsystem.h` |

---

## 插件依赖

- **引擎模块**：`Core`, `CoreUObject`, `Engine`, `GameplayTags`, `WebSockets`
- **内部插件**：`GameCore`, `GameMessage`, `GameLogging`, `GameNetCore`

---

## 快速上手示例

```cpp
#include "System/GameNetServiceSubsystem.h"
#include "Transport/NetTransportFactory.h"

UGameNetServiceSubsystem& NetService = UGameNetServiceSubsystem::Get(this);

// 1. 联机环境注入 WSS 驱动；单机环境保持默认 Mock 驱动即可，以下业务代码无需修改一行
NetService.SetTransport(FNetTransportFactory::CreateWebSocketTransport());
NetService.Connect(TEXT("wss://gateway.mygame.com/ws"));

// 2. 远程日志流转：默认自动监听 TAG_Game_Log_Remote 并经长连接转发远端分析平台
NetService.SetRemoteLogForwardingEnabled(true);

// 3. 强类型 RPC 双向请求示例（支持可选 FCancellationToken 取消）
const uint32 SeqId = NetService.SendStructRequest<FShopBuyReq, FShopBuyRsp>(
    5001,
    5002,
    BuyReq,
    5.0f,
    [](ENetRequestResult Result, const FShopBuyRsp* Rsp) {
        if (Result == ENetRequestResult::Success && Rsp != nullptr) {
            // 保证在 GameThread 安全执行，可直接刷新 UI 或修改 UObject
        }
    }
);
```

---

## 目录结构

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

---

## 文档导航

- [架构概览与跨线程派发时序](Docs/Architecture/Overview.md)
