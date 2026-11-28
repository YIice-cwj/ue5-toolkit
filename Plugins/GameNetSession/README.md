# GameNetSession

> **一句话**：可选的房间会话插件，提供零 SDK 局域网 UDP 发现、OnlineSubsystem 会话适配与安全 Travel。
> **版本** v1.0.0 · **引擎** UE 5.1~5.8 · **层级** Level 2 · **直接依赖** GameNetCore, GameCore, GameLogging

## 30 秒速览

- **解决什么问题**：把「玩家自建房间 → 局域网互相发现 → 加入并跳转地图」这条链路拆成可独立启用的会话能力，同时兼容 Steam / EOS 等 OSS 大厅。
- **核心能力**：
  - `FLanBeaconBroadcaster` 协调非阻塞 UDP 广播与紧凑 JSON 信标；
  - `FLanRoomDiscoveryTable` 做房间去重、多房主 RTT 测算与过期淘汰；
  - `FOnlineSessionRoomAdapter` 封装 `IOnlineSession` 异步创建 / 搜索 / 加入 / 销毁；
  - `UNetTravelLibrary` 提供防 URL 选项注入的 `ServerTravel` / `ClientTravel`；
  - `UGameNetSessionSubsystem` 作为统一门面，公开头不泄漏 Socket 或 OSS 引擎头。
- **什么时候用**：需要 ListenServer 自建房间、局域网无感联机，或对接 Steam / EOS 会话大厅。
- **什么时候不用**：纯单机项目、或只连中心服长连接的项目无需启用；后者使用 `GameNetService` 即可。

## 快速开始

```cpp
#include "System/GameNetSessionSubsystem.h"

UGameNetSessionSubsystem& SessionSub = UGameNetSessionSubsystem::Get(this);

FGameNetCreateRoomRequest Req;
Req.RoomName = TEXT("CoopDungeon_Room1");
Req.MapPath = TEXT("/Game/Maps/CoopDungeon");
Req.HostGamePort = 7777;
Req.MaxPlayers = 4;

SessionSub.HostLanRoom(Req);
SessionSub.TravelToListenServer(Req.MapPath, Req.HostGamePort);

SessionSub.StartLanDiscovery();
SessionSub.SendLanDiscoveryProbe();
```

## 核心能力

### 局域网信标广播

- `FLanBeaconBroadcaster` 组合 `FLanUdpSocketChannel`、`FLanRoomDiscoveryTable` 与 `FJsonNetSerializer`。
- 房主在 `StartHosting` / `UpdateHostedRoom` 时预编码紧凑 JSON 负载并缓存，后续探测应答与主动宣告不再重复反射。
- 所有数据报复用 `FNetPacketCodec` 的 `0x5545` 魔数与 16 字节头，单包负载上限 4096 字节。
- 长度截断、魔数不符或 JSON 损坏的杂包在入口直接丢弃。

### 房间发现与 RTT

- 发现表在发出探测时分配递增 `SequenceId` 并记录本地单调时钟；多房主回显同一序号时分别计算各自 RTT。
- 全字段校验覆盖端口范围、人数边界与主机地址防 URL 注入；未填写固定地址时自动回填 UDP 来源 IP。
- 房间总数硬上限 `MAX_DISCOVERED_ROOMS = 256`，待回收探测上限 `MAX_PENDING_PROBES = 64`。
- 过期房间由 `PruneExpiredRooms` 淘汰，拷贝到局部数组后再逐条触发 `OnLanRoomExpired`，允许回调内重入 `Stop()`。

### 非阻塞 UDP 通道

- `FLanUdpSocketChannel` 以 RAII 封装 IPv4 广播与端口复用 `FSocket`。
- `HasPendingData` 零分配空闲早退；缓存目标地址复用，拒绝空地址与 `0.0.0.0` 目标。
- `TryReceiveDatagram(Out)` 零分配读取；接收缓冲按 `4096 + 64` 字节预留。
- 默认信标端口 `15777`，绑定时可传 `0` 自动分配空闲端口。

### OSS 会话适配

- `FOnlineSessionRoomAdapter` 独立管理 `IOnlineSession` 异步委托，把 `FOnlineSessionSearchResult` 映射为纯值类型 `FGameNetRoomDescriptor`。
- 内置线程安全 `AliveToken` 存活守卫与原接口 `BoundSessionInterface` 精确解绑，PIE 多实例切换时零悬空回调。
- 未配置 OSS 时 `CreateOnlineRoom` 等接口安全返回 `false`，不会崩溃。
- 兼容同步回调的状态机推进，异步成功只广播一次终态。

### 安全 Travel

- `UNetTravelLibrary` 是独立 `UBlueprintFunctionLibrary`，C++ 与蓝图共用。
- 按词法域分治拦截空白、控制符，以及地图路径中的 `?` / `#` 与主机地址中的 `?` / `#` / `/` / `\` 选项注入。
- 统一返回 `Game::NetErrorCodes`，并执行 `ServerTravel` / `ClientTravel`。

### 薄门面会话子系统

- `UGameNetSessionSubsystem` 继承 `UGameInstanceSubsystem`，用 `GetPtr` / `Get` 寻址。
- 对外提供 `HostLanRoom`、`UpdateLanHostedRoom`、`BroadcastLanAnnouncement`、`StartLanDiscovery`、`SendLanDiscoveryProbe`、`StopLanSession`。
- 平台侧提供 `CreateOnlineRoom`、`FindOnlineRooms`、`JoinOnlineRoomByIndex`、`DestroyOnlineRoom`。
- 状态机 `EGameNetSessionState` 通过 `OnSessionStateChanged` 广播；内部成员对象不对外暴露。

## 架构与依赖

| 项目 | 内容 |
| :--- | :--- |
| 模块名 | `GameNetSession`（`GAMENETSESSION_API`） |
| 公有引擎依赖 | Core, CoreUObject, Engine |
| 私有引擎依赖 | Sockets, Networking, OnlineSubsystem, OnlineSubsystemUtils |
| 公有插件依赖 | GameCore, GameNetCore |
| 私有插件依赖 | GameLogging |

- 局域网轨道：`LAN/LanUdpSocketChannel.h`、`LAN/LanRoomDiscoveryTable.h`、`LAN/LanBeaconBroadcaster.h`
- 平台轨道：`Online/OnlineSessionRoomAdapter.h`
- 跳转轨道：`Travel/NetTravelLibrary.h`
- 门面与类型：`System/GameNetSessionSubsystem.h`、`Types/GameNetSessionTypes.h`
- 公开头文件不包含 `SocketSubsystem.h` 或 `OnlineSessionInterface.h`。

<details>
<summary><b>展开目录结构</b></summary>

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
│       │   └── Tests/         # 单元测试与多轮统计性能基准（GameNetSessionPerformanceTest）
│       └── GameNetSession.Build.cs
├── Docs/
│   └── Architecture/
│       └── Overview.md
├── GameNetSession.uplugin
└── README.md
```

</details>

## 性能与边界

- **依赖边界**：Sockets / OSS 仅在私有实现中使用，调用方不接触引擎网络头。
- **容量边界**：局域网单包 4096 字节，最多 256 个房间、64 个待回收探测。
- **平台边界**：未挂载 OSS 时平台会话接口返回 `false`，局域网轨道仍可独立工作。
- **生命周期**：PIE 多实例由 `AliveToken` 与 `BoundSessionInterface` 守卫，子系统析构时解绑并停止信标。
- **部署边界**：按需引入；纯单机或纯中心服项目不必安装本插件。

<details>
<summary>展开性能基准（历史 Development 构建参考值）</summary>

下表为自动化测试中的中位耗时预算上限，不是仓库固化的实测值；实际数值随机器与构建配置变化。

| 基准用例 | 场景 | 预算 |
| :--- | :--- | :--- |
| `Game.Net.Session.Perf.DiscoveryTable` | 房间发现表更新与多房主 RTT 测算 | < 1800 ns |
| `Game.Net.Session.Perf.DatagramDecode` | 局域网宣告帧解包 | < 35 μs |
| `Game.Net.Session.Perf.TravelUrl` | 双 Travel URL 防注入构造 | < 1500 ns |
| `Game.Net.Session.Perf.DescriptorValidation` | 房间描述符校验 | < 50 ns |
| `Game.Net.Session.Perf.CachedBeaconAnnouncement` | 预缓存房主宣告成帧 | < 150 ns |

来源：`Source/GameNetSession/Private/Tests/GameNetSessionPerformanceTest.cpp`。

</details>

## 文档导航

| 文档 | 内容 |
| :--- | :--- |
| [架构概览与局域网信标协议](Docs/Architecture/Overview.md) | 三条轨道分工、信标交互与安全边界 |
| [GameNetCore 插件](../GameNetCore/README.md) | 线格式、编解码与序列化契约 |
| [GameNetService 插件](../GameNetService/README.md) | 中心服长连接服务 |
| [根 README](../../README.md) | 插件分层与安装方式 |
