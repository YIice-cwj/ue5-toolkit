# GameNetSession 插件

**GameNetSession** 是 `GameNet` 套件的可选房间会话与联机发现插件（Layer 2）。它严格遵循单文件单类与头文件零底层污染原则：由薄门面 `UGameNetSessionSubsystem` 统一编排局域网信标协调器（`FLanBeaconBroadcaster`，内聚组合非阻塞 UDP 通道 `FLanUdpSocketChannel` 与房间发现表 `FLanRoomDiscoveryTable`）、在线会话适配器（`FOnlineSessionRoomAdapter`）以及无状态关卡跳转工具库（`UNetTravelLibrary`），且公开头文件中零暴露 `SocketSubsystem.h` 或 `OnlineSessionInterface.h`。

> 纯单机项目或纯中心服长连接项目无需启用本插件；仅在需要玩家自建房间（ListenServer）、局域网无感联机或对接 Steam / EOS 会话大厅时按需挂载。

---

## 核心特性一览

| 模块 / 机制 | 关键技术亮点 | 核心文件 / 接口 |
| :--- | :--- | :--- |
| **会话领域值类型定义** | 统一房间描述符 `FGameNetRoomDescriptor`、创建请求 `FGameNetCreateRoomRequest` 与端点值对象 `FGameNetLanEndpoint` | `Types/GameNetSessionTypes.h` |
| **非阻塞 UDP 套接字通道** | RAII 封装 IPv4 广播与端口复用 `FSocket` 生命周期及收发切片，头文件仅前向声明 `FSocket` | `LAN/LanBeaconTypes.h`<br>`LAN/LanUdpSocketChannel.h` |
| **局域网房间去重与 RTT 表** | 独立维护探测 `SequenceId` 发送时间戳、单调时钟 RTT 测算、同 `RoomId` 刷新与静默过期淘汰 | `LAN/LanRoomDiscoveryTable.h` |
| **局域网信标协议协调器** | 组合 `FLanUdpSocketChannel`、`FLanRoomDiscoveryTable` 与 `FJsonNetSerializer`，完成广播公告与探测单播应答 | `LAN/LanBeaconBroadcaster.h` |
| **在线会话房间适配器** | 独立封装 `IOnlineSession` 异步创建/搜索/加入/销毁与委托句柄清理，未配置 OSS 时安全降级返回 `false` | `Online/OnlineSessionRoomAdapter.h` |
| **安全 Travel URL 工具库** | 独立 `UBlueprintFunctionLibrary`，严格拦截空白与控制字符参数注入，执行 `ServerTravel` 与 `ClientTravel` | `Travel/NetTravelLibrary.h` |
| **薄门面房间会话子系统** | `UGameInstanceSubsystem` 统一对外门面，纯委托编排 LAN、Online 与 Travel 三大职责组件 | `System/GameNetSessionSubsystem.h` |

---

## 插件依赖

- **引擎模块**：`Core`, `CoreUObject`, `Engine`, `GameplayTags`（公开依赖）；`Sockets`, `Networking`, `OnlineSubsystem`, `OnlineSubsystemUtils`（私有封装依赖，不泄漏至调用方）
- **内部插件**：`GameCore`, `GameLogging`, `GameNetCore`

---

## 快速上手示例

```cpp
#include "System/GameNetSessionSubsystem.h"

UGameNetSessionSubsystem& SessionSub = UGameNetSessionSubsystem::Get(this);

// 房主启动局域网广播并切入 ListenServer 地图
FGameNetCreateRoomRequest Req;
Req.RoomName = TEXT("CoopDungeon_Room1");
Req.MapPath = TEXT("/Game/Maps/CoopDungeon");
Req.HostGamePort = 7777;
Req.MaxPlayers = 4;

SessionSub.HostLanRoom(Req);
SessionSub.TravelToListenServer(Req.MapPath, Req.HostGamePort);

// 客户端启动局域网自动发现并发出探测
SessionSub.StartLanDiscovery();
SessionSub.SendLanDiscoveryProbe();
```

---

## 目录结构

```text
Plugins/GameNetSession/
├── Source/
│   └── GameNetSession/
│       ├── Public/
│       │   ├── GameNetSession.h
│       │   ├── LAN/           # LanBeaconTypes、FLanUdpSocketChannel、FLanRoomDiscoveryTable、FLanBeaconBroadcaster
│       │   ├── Online/        # FOnlineSessionRoomAdapter 在线会话房间适配器
│       │   ├── System/        # UGameNetSessionSubsystem 薄门面会话子系统
│       │   ├── Travel/        # UNetTravelLibrary 安全关卡跳转静态工具库
│       │   └── Types/         # GameNetSessionTypes 会话领域枚举与数据结构
│       ├── Private/
│       │   ├── LAN/
│       │   ├── Online/
│       │   ├── System/
│       │   ├── Travel/
│       │   └── Tests/         # 1:1 自动化单元测试
│       └── GameNetSession.Build.cs
├── Docs/
│   └── Architecture/
│       └── Overview.md
├── GameNetSession.uplugin
└── README.md
```

---

## 文档导航

- [架构概览与局域网信标协议](Docs/Architecture/Overview.md)
