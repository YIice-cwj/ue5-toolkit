# GameNetCore

> **一句话**：定义 `GameNet` 套件共享的 16 字节线格式、编解码 / 解帧、传输与序列化契约，并提供单机内存回环。
> **版本** v1.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 1 · **直接依赖** GameCore, GameLogging

## 30 秒速览

- **解决什么问题**：把报文格式、传输驱动和负载序列化抽成与具体 Socket 解耦的契约层，让业务网络、单机模式与自动化测试共用同一套调用链。
- **核心能力**：
  - `FNetPacketHeader` / `FNetPacket` 纯数据报文契约，固定 16 字节小端序线格式；
  - `FNetPacketCodec` 无状态编解码与边界校验，`FNetPacketRingBuffer` 处理粘包 / 半包；
  - `INetTransport` 抽象与零 Socket 的 `FMockNetTransport` 内存回环；
  - `INetSerializer` 可拔插序列化，内置紧凑 JSON 与安全二进制 Archive；
  - `Game::NetErrorCodes`、`ENetConnectionState` 与跨插件消息信封 / 预留 MessageId。
- **什么时候用**：需要自定义二进制协议、跨插件网络消息桥接，或希望单机与联机共享同一业务链路。
- **什么时候不用**：需要现成 WebSocket 长连接、心跳与重连时用 `GameNetService`；局内高频位移与属性同步交给引擎 `UNetDriver` / Iris。

## 快速开始

单机模式用内存回环驱动跑通请求 / 响应：

```cpp
#include "Transport/MockNetTransport.h"

FMockNetTransport MockTransport;
MockTransport.RegisterMockHandler(
    1001,
    1002,
    [](const FNetPacketHeader& ReqHeader, TConstArrayView<uint8> Payload) -> TOptional<TArray<uint8>> {
        return TArray<uint8>(Payload.GetData(), Payload.Num());
    }
);

MockTransport.Connect(TEXT("mock://standalone"));
MockTransport.Tick(0.016f);
```

## 核心能力

### 线格式与编解码

- `FNetPacketHeader` 固定 `Magic = 0x5545`、`HeaderSize = 16`，后接 `PayloadLength`、`MessageId`、`SequenceId`。
- 默认单包负载上限 `DEFAULT_MAX_PAYLOAD_BYTES = 2 MB`；`ValidateHeader` / `IsValidHeader` 在分配负载内存前核验魔数、头长与负载上限。
- `FNetPacketCodec` 固定小端序读写，`EncodeHeaderTo` / `EncodeFrameTo` 写入既有缓冲，`TryDecodeHeader` 快速反解。
- 编解码无状态、零全局变量，可跨语言按同一布局实现。

### 流式解帧

- `FNetPacketRingBuffer` 使用 2 的幂次容量与 `& CapacityMask` 索引，初始 4096 字节，最大 1 GB。
- 总容量受 `MaxBufferCapacityBytes` 约束，防止无头垃圾字节流撑爆内存。
- `ResetEpoch` 阻止消费者回调重入 `Reset()` 破坏读游标。
- `TryConsumePacket` / `ConsumeAllAvailable` 返回 `ENetFrameParseStatus`，支持跨回绕段拼接与零拷贝连续段直读。

### 传输契约与单机回环

- `INetTransport` 只搬运 `TConstArrayView<uint8>` 切片，暴露状态、收包与错误委托。
- `FMockNetTransport` 支持显式 `Tick()` 派发、连接拒绝注入、下行推送模拟与断线注入，无需 `if (bIsSinglePlayer)` 分支。
- `RegisterMockHandler` 可按键值注册本地模拟路由；`SetEchoUnregisteredMessages` 可让未注册消息原样回声。
- 单机回环只在本进程内有效，不提供跨进程或真实网络能力。

### 序列化契约

- `INetSerializer` 以单一纯虚 `SerializeToBuffer` / `Deserialize` 返回 `TGameStatusResult<void>`，基类提供非虚 `Serialize` 与强类型模板。
- `FJsonNetSerializer` 输出紧凑 UTF-8 JSON；`FMemoryArchiveNetSerializer` 使用二进制 Archive。
- 二进制 Archive 反序列化时禁用隐式 `StaticLoadObject`，避免不可信字节流触发资产加载。

### 消息信封与预留 ID

- 声明 `Game.Net.Outbound` / `Game.Net.Inbound` / `Game.Net.StateChanged` 三个 `GameplayTag`，让 `GameCore` 保持零网络污染。
- 预留心跳 `NET_MSG_HEARTBEAT_PING = 1` / `PONG = 2`、远程日志 `100`、云存档 `201~206`。
- 信封结构 `FGameNetOutboundMessage` / `FGameNetInboundMessage` / `FGameNetStateMessage` 供 `GameMessage` 总线互通。

### 错误码与连接状态

- `Game::NetErrorCodes` 覆盖 `2001~2011`：端点非法、未连接、报文头非法、序列化失败、重复连接、请求取消、Travel 失败等。
- `ENetConnectionState` 统一描述断开、连接中、已连接与重连状态，供上层门面与总线广播复用。

## 架构与依赖

| 项目 | 内容 |
| :--- | :--- |
| 模块名 | `GameNetCore`（`GAMENETCORE_API`） |
| 引擎依赖 | Core, CoreUObject, GameplayTags（公有）；Json, JsonUtilities（私有） |
| 插件依赖 | GameCore（公有）；GameLogging（私有） |
| 物理网络 / 平台 SDK | 0；不链接 Socket、WebSocket 或第三方网络库 |

- 报文：`Packet/NetPacketTypes.h`
- 编解码：`Codec/NetPacketCodec.h`、`Codec/NetPacketRingBuffer.h`
- 传输：`Transport/INetTransport.h`、`Transport/MockNetTransport.h`
- 序列化：`Serializer/INetSerializer.h`、`JsonNetSerializer.h`、`MemoryArchiveNetSerializer.h`
- 消息与错误：`Message/GameNetworkMessageTypes.h`、`Types/NetErrorCodes.h`

<details>
<summary><b>展开目录结构</b></summary>

```text
Plugins/GameNetCore/
├── Source/
│   └── GameNetCore/
│       ├── Public/
│       │   ├── GameNetCore.h
│       │   ├── Codec/         # NetCodecTypes、FNetPacketCodec、FNetPacketRingBuffer
│       │   ├── Message/       # GameNetworkMessageTypes 跨插件消息标签、信封与预留 MessageId
│       │   ├── Packet/        # NetPacketTypes 纯数据报文头与报文结构
│       │   ├── Serializer/    # INetSerializer、FJsonNetSerializer、FMemoryArchiveNetSerializer
│       │   ├── Tests/         # GameNetCoreTestTypes 测试反射夹具
│       │   ├── Transport/     # INetTransport、FMockNetTransport
│       │   └── Types/         # ENetConnectionState 连接状态枚举、NetErrorCodes 统一网络错误码
│       ├── Private/
│       │   ├── Codec/
│       │   ├── Message/
│       │   ├── Serializer/
│       │   ├── Transport/
│       │   └── Tests/         # 单元测试与多轮统计性能基准（GameNetCorePerformanceTest）
│       └── GameNetCore.Build.cs
├── Docs/
│   └── Architecture/
│       └── Overview.md
├── GameNetCore.uplugin
└── README.md
```

</details>

## 性能与边界

- **网络边界**：本插件不创建真实 Socket；需要真实连接时由 `GameNetService` 注入 WebSocket 驱动。
- **负载上限**：默认单包 2 MB，环缓冲默认初始 4096 字节、最大 1 GB，超出即拒绝或熔断。
- **协议边界**：只承载业务长连接（登录、大厅、商城等），局内高频同步交给引擎网络层。
- **单机边界**：`FMockNetTransport` 仅进程内回环，不可跨进程；显式 `Tick()` 才派发队列。
- **生命周期**：环缓冲与 Mock 驱动均为纯 C++ 对象，不依赖 `UObject` GC。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

下表为自动化测试中的中位耗时预算上限，不是仓库固化的实测值；实际数值随机器与构建配置变化。

| 基准用例 | 场景 | 预算 |
| :--- | :--- | :--- |
| `Game.Net.Core.Perf.PacketCodecEncode` | 帧就地编码 | < 200 ns |
| `Game.Net.Core.Perf.PacketCodecDecode` | 报文头反解 | < 50 ns |
| `Game.Net.Core.Perf.RingBufferWrapAndSticky` | 4 帧跨界粘包解帧 | < 1500 ns |
| `Game.Net.Core.Perf.MockTransportLoopback` | 单包回环 | < 2000 ns |
| `Game.Net.Core.Perf.MemoryArchiveRoundTrip` | 二进制 Archive 往返 | < 30 μs |
| `Game.Net.Core.Perf.JsonRoundTrip` | JSON 往返 | < 200 μs |

来源：`Source/GameNetCore/Private/Tests/GameNetCorePerformanceTest.cpp`。

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概览与线格式规范](Docs/Architecture/Overview.md) | Layer 1 职责、16 字节线格式与熔断策略 |
| [GameNetService 插件](../GameNetService/README.md) | WebSocket 长连接、心跳、重连与 RPC |
| [GameNetSession 插件](../GameNetSession/README.md) | 局域网发现、OSS 会话与 Travel |
| [根 README](../../README.md) | 插件分层与安装方式 |
