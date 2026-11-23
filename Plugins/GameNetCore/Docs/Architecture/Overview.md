# GameNetCore 架构概览与线格式规范

## 1. 定位与职责边界

`GameNetCore` 处于 `UEToolkit` 的 **Layer 1（契约与基础设施层）**，严格遵循与 `GameCore` 一致的**分类目录组织 + 单文件单类（`R-ARCH-CLASS`）**原则，专注解决四件事：

1. **跨语言统一二进制帧格式**：数据定义收敛于 `Packet/NetPacketTypes.h`（纯数据 `FNetPacketHeader` / `FNetPacket`），小端序编解码与边界核验独立收敛于无状态 `Codec/NetPacketCodec.h`（统一收口为 `ValidateHeader`、`IsValidHeader`、`EncodeHeaderTo`、`EncodeFrameTo`、`TryDecodeHeader`）；
2. **协议与序列化正交解耦**：`Transport/INetTransport.h` 只搬运 `TConstArrayView<uint8>` 切片；`Serializer/INetSerializer.h` 以单一纯虚 `SerializeToBuffer` / `Deserialize` 返回 `TGameStatusResult<void>`（辅以基类非虚 `Serialize` 与强类型模板无损透传 `FGameError`）；`FMemoryArchiveNetSerializer` 在反序列化时禁用 `FObjectAndNameAsStringProxyArchive` 的隐式 `StaticLoadObject`，防止不可信字节流触发任意资产加载；
3. **跨插件消息总线网络信封宿主与统一错误码**：`Message/GameNetworkMessageTypes.h` 承载 `TAG_Game_Net_Outbound` / `TAG_Game_Net_Inbound` / `TAG_Game_Net_StateChanged` 与系统预留 MessageId（含心跳 `1/2`、远程日志 `100`、云存档 `201~206`）；`Types/NetErrorCodes.h` 统一归口 `Game::ModuleId::Net` 下全部错误码；
4. **单机与自动化测试脱机闭环**：`Transport/MockNetTransport.h` 在纯内存中模拟连接生命周期、重入安全路由回包、显式 `Tick()` 队列驱动派发与断线注入，业务模块无需编写 `if (bIsSinglePlayer)` 分支。

> **职责红线**：`GameNet` 套件专司业务长连接（登录、大厅、商城、背包、聊天、匹配），局内高频角色位移、属性复制与预测回滚一律交由 UE 原生 `UNetDriver` / `Iris` 承担。模块日志统一经 `GameLogging` 输出至 `LogGameNetCore`（已在远程日志输出设备中纳入防循环黑名单）。

---

## 2. 16 字节线格式布局 (Little-Endian)

```text
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Magic (0x5545)        |        HeaderSize (16)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       PayloadLength (u32)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         MessageId (u32)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        SequenceId (u32)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                     Payload Bytes (0..Max)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **不可信边界防御**：`FNetPacketCodec::IsValidHeader(Header, MaxAllowedPayloadBytes)` 与 `ValidateHeader(...)` 在分配任何负载内存前率先核验 `Magic == 0x5545`、`HeaderSize == 16` 以及 `PayloadSize <= MaxAllowedPayloadBytes`（默认 2 MB），从根源阻断畸形包与超大包 OOM 攻击。
- **粘包/半包安全拆帧与防 OOM 熔断**：`FNetPacketRingBuffer` 维护 2 的幂次容量与位掩码 `& CapacityMask` 索引、强制 `MaxBufferCapacityBytes` 总容量上限（防止无头垃圾字节流撑爆内存）、使用 `ResetEpoch` 防止消费者回调重入 `Reset()` 破坏读游标，并通过 `TryConsumePacket` / `ConsumeAllAvailable` 直接返回 `ENetFrameParseStatus` 实现零拷贝连续段直读与跨回绕段拼接。
