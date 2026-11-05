# GameNetService 插件

**GameNetService** 是 `GameNet` 套件的常驻业务长连接服务插件（Layer 2）。它遵循单文件单类与薄门面编排原则：由跨关卡存活的 `UGameNetServiceSubsystem` 统一编排传输工厂（`FNetTransportFactory`）、WebSocket 分片重组与跨线程驱动（`FWebSocketFragmentAssembler` + `FWebSocketNetTransport`）、Full-Jitter 指数退避重连（`FNetReconnectPolicy`）、双向心跳与 RTT 测算（`FNetHeartbeatKeeper`）、多播消息分发器（`FNetMessageDispatcher`）、异步 RPC 挂起追踪器（`FNetPendingRequestTracker`）、消息总线双向桥接器（`FNetMessageBusBridge`）以及**远程日志自动转发器**。

---

## 核心特性一览

| 模块 / 机制 | 关键技术亮点 | 核心文件 / 接口 |
| :--- | :--- | :--- |
| **薄门面常驻子系统** | `UGameInstanceSubsystem` 跨地图 Seamless Travel 存活；仅负责编排子组件，不直接耦合底层具体传输实现 | `System/GameNetServiceSubsystem.h` |
| **传输驱动实例工厂** | 隔离具体驱动构造细节，按需装配 `FMockNetTransport` 或注入自定义工厂的 `FWebSocketNetTransport` | `Transport/NetTransportFactory.h` |
| **WebSocket 分片重组与驱动** | `FWebSocketFragmentAssembler` 独立守护分片拼装与帧上限；`FWebSocketNetTransport` 通过 64 位代际号与 `MPSC` 无锁队列保障主线程消费 | `Transport/WebSocketTransportTypes.h`<br>`Transport/WebSocketFragmentAssembler.h`<br>`Transport/WebSocketNetTransport.h` |
| **重连退避与动态心跳** | `FNetReconnectPolicy` 实现带抖动指数退避与熔断；`FNetHeartbeatKeeper` 独立负责空闲 Ping 触发、毫秒级 RTT 测算与失步判定 | `Policy/NetPolicyTypes.h`<br>`Policy/NetReconnectPolicy.h`<br>`Policy/NetHeartbeatKeeper.h` |
| **多播路由与异步 RPC 追踪** | `FNetMessageDispatcher` 专司快照重入安全多播；`FNetPendingRequestTracker` 专司 `SequenceId` 分配、响应匹配、超时扫描与断线结算 | `Dispatch/NetDispatchTypes.h`<br>`Dispatch/NetMessageDispatcher.h`<br>`Dispatch/NetPendingRequestTracker.h` |
| **零耦合消息总线桥接器** | 自动订阅 `Game.Net.Outbound` 并广播 `Game.Net.Inbound` / `Game.Net.StateChanged`，实现跨插件无感网络通信 | `Bridge/NetMessageBusBridge.h` |
| **远程日志自动转发** | 自动监听 `TAG_Game_Log_Remote` 总线广播，打包为 `NET_MSG_REMOTE_LOG (100)` 结构体并经由长连接外发远端；支持 `SetRemoteLogForwardingEnabled` 动态开关 | `System/GameNetServiceSubsystem.h` |

---

## 插件依赖

- **引擎模块**：`Core`, `CoreUObject`, `Engine`, `GameplayTags`, `GameplayMessageRuntime`, `WebSockets`
- **内部插件**：`GameCore`, `GameLogging`, `GameNetCore`

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

// 3. 强类型 RPC 双向请求示例
NetService.SendStructRequest<FShopBuyReq, FShopBuyRsp>(
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
│       │   └── Tests/         # 1:1 自动化单元测试（含 RemoteLogForwarding 测试）
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
