# GameNetCore 插件

**GameNetCore** 是 `GameNet` 分层通用网络套件的底层核心契约插件（Layer 1）。它遵循**零物理 Socket 依赖、零第三方库捆绑、职责单一与单机零开销回路**设计原则，按与 `GameCore` 一致的职责域子目录组织头文件，提供 16 字节二进制报文头数据契约、无状态小端序编解码器、环形流式粘包/半包解帧器、传输驱动抽象接口（`INetTransport`）、可拔插负载序列化契约（`INetSerializer`）、跨插件消息总线信封、系统保留协议 MessageId 以及单机纯内存回环驱动（`FMockNetTransport`）。

---

## 核心特性一览

| 模块 / 机制 | 关键技术亮点 | 核心文件 / 接口 |
| :--- | :--- | :--- |
| **连接状态枚举契约** | 标准化 `Disconnected` / `Connecting` / `Connected` / `Reconnecting` / `Closing` 状态机定义 | `Types/NetConnectionTypes.h` |
| **16 字节紧凑报文数据契约** | 纯数据结构定义（`Magic=0x5545`, `HeaderSize=16`, `PayloadLength`, `MessageId`, `SequenceId`），零行为耦合 | `Packet/NetPacketTypes.h` |
| **无状态小端序报文编解码器** | 跨平台固定小端序读写、魔数与负载上限防溢出核验、完整帧拼装，单文件单类无状态实现 | `Codec/NetPacketCodec.h` |
| **环形流式解包与熔断器** | O(1) 均摊吞吐处理流式粘包、半包切片；遇到错魔数或超大负载（OOM 攻击）立即熔断阻断 | `Codec/NetCodecTypes.h`<br>`Codec/NetPacketRingBuffer.h` |
| **跨插件网络消息总线信封** | 声明 `Game.Net.Outbound` / `Game.Net.Inbound` / `Game.Net.StateChanged` 标签与信封结构，使 `GameCore` 保持零网络污染 | `Message/GameNetworkMessageTypes.h` |
| **系统级 MessageId 常量契约** | 集中预留核心基础设施通信 ID：心跳 (1/2)、远程日志转发 (100)、云存档上传/下载/查询 (201~206) | `Message/GameNetworkMessageTypes.h` |
| **传输驱动抽象与单机回环** | 统一 `INetTransport` 抽象接口与零 Socket 句柄分配的 `FMockNetTransport` 内存回环驱动（支持虚拟延迟与断线注入） | `Transport/INetTransport.h`<br>`Transport/MockNetTransport.h` |
| **可拔插双内置序列化器** | 返回 `TOptional<TArray<uint8>>` 值语义的 `INetSerializer` 契约，内置紧凑 UTF-8 JSON 与带上限守卫的二进制 Archive 适配器 | `Serializer/INetSerializer.h`<br>`Serializer/JsonNetSerializer.h`<br>`Serializer/MemoryArchiveNetSerializer.h` |

---

## 插件依赖

- **引擎模块**：`Core`, `CoreUObject`, `Engine`, `GameplayTags`, `Json`, `JsonUtilities`
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
│       │   └── Types/         # ENetConnectionState 连接状态枚举
│       ├── Private/
│       │   ├── Codec/
│       │   ├── Message/
│       │   ├── Serializer/
│       │   ├── Transport/
│       │   └── Tests/         # 1:1 自动化单元测试
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
