# GameNetCore 插件

**GameNetCore** 是 `GameNet` 分层通用网络套件的底层核心契约插件（Layer 1）。它遵循**零物理 Socket 依赖、零第三方库捆绑、职责单一与单机零开销回路**设计原则，按与 `GameCore` 一致的职责域子目录组织头文件，提供统一网络错误码（`Game::NetErrorCodes`）、16 字节二进制报文头数据契约、无状态小端序编解码器、2 的幂次位掩码环形流式解帧器、传输驱动抽象接口（`INetTransport`）、可拔插负载序列化契约（`INetSerializer`）、跨插件消息总线信封、系统保留协议 MessageId 以及单机纯内存回环驱动（`FMockNetTransport`）。

---

## 核心特性一览

| 模块 / 机制 | 关键技术亮点 | 核心文件 / 接口 |
| :--- | :--- | :--- |
| **连接状态与错误码契约** | 标准化 `ENetConnectionState` 状态机枚举与 `Game::NetErrorCodes`（`Game::ModuleId::Net` 域统一错误码 `2001~2011`） | `Types/NetConnectionTypes.h`<br>`Types/NetErrorCodes.h` |
| **16 字节紧凑报文数据契约** | 纯数据结构定义（`Magic=0x5545`, `HeaderSize=16`, `PayloadLength`, `MessageId`, `SequenceId`），零行为耦合 | `Packet/NetPacketTypes.h` |
| **无状态小端序报文编解码器** | 跨平台固定小端序读写、魔数与负载上限防溢出核验（`ValidateHeader` / `IsValidHeader`）、零分配目标缓冲拼装（`EncodeHeaderTo` / `EncodeFrameTo`）与快速解码（`TryDecodeHeader`） | `Codec/NetPacketCodec.h` |
| **2 的幂次环形流式解包与熔断器** | 2 的幂次容量配合 `& CapacityMask` 索引计算、总容量防 OOM 上限熔断、`ResetEpoch` 回调重入防护，以及零拷贝 `TryConsumePacket` / `ConsumeAllAvailable`（返回 `ENetFrameParseStatus`） | `Codec/NetCodecTypes.h`<br>`Codec/NetPacketRingBuffer.h` |
| **跨插件网络消息总线信封** | 声明 `Game.Net.Outbound` / `Game.Net.Inbound` / `Game.Net.StateChanged` 标签与信封结构，使 `GameCore` 保持零网络污染 | `Message/GameNetworkMessageTypes.h` |
| **系统级 MessageId 常量契约** | 集中预留核心基础设施通信 ID：心跳（`NET_MSG_HEARTBEAT_PING=1` / `NET_MSG_HEARTBEAT_PONG=2`）、远程日志转发（`100`）、云存档（`201~206`） | `Message/GameNetworkMessageTypes.h` |
| **传输驱动抽象与单机回环** | 统一 `INetTransport` 抽象接口与零 Socket 句柄分配的 `FMockNetTransport` 内存回环驱动（支持显式 `Tick()` 驱动队列派发、连接拒绝对抗与断线注入、重入安全路由派发） | `Transport/INetTransport.h`<br>`Transport/MockNetTransport.h` |
| **可拔插双内置序列化器** | 单一纯虚 `SerializeToBuffer` / `Deserialize` 返回 `TGameStatusResult<void>`（辅以非虚 `Serialize` 与强类型模板保留原始 `FGameError`）；内置紧凑 UTF-8 JSON 与禁用隐式 `StaticLoadObject` 的安全二进制 Archive 适配器 | `Serializer/INetSerializer.h`<br>`Serializer/JsonNetSerializer.h`<br>`Serializer/MemoryArchiveNetSerializer.h` |

---

## 插件依赖

- **引擎模块**：`Core`, `CoreUObject`, `GameplayTags`, `Json`, `JsonUtilities`
- **内部插件**：`GameCore`, `GameLogging`
- **物理网络 / 平台 SDK 依赖**：**0**（纯单机项目仅挂载本插件即可运行全套网络业务契约，无任何多余平台库合规负担）

---

## 快速上手示例

```cpp
#include "Serializer/JsonNetSerializer.h"
#include "Transport/MockNetTransport.h"

FMockNetTransport MockTransport;
FJsonNetSerializer Serializer;

MockTransport.RegisterMockHandler(
    1001,
    1002,
    [](const FNetPacketHeader& ReqHeader, TConstArrayView<uint8> Payload) -> TOptional<TArray<uint8>> {
        return TArray<uint8>(Payload.GetData(), Payload.Num());
    }
);

MockTransport.Connect(TEXT("mock://standalone"));
```

---

## 目录结构

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

---

## 文档导航

- [架构概览与线格式规范](Docs/Architecture/Overview.md)
