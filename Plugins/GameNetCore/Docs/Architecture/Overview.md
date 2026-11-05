# GameNetCore 架构概览与线格式规范

## 1. 定位与职责边界

`GameNetCore` 处于 `UEToolkit` 的 **Layer 1（契约与基础设施层）**，严格遵循与 `GameCore` 一致的**分类目录组织 + 单文件单类（`R-ARCH-CLASS`）**原则，专注解决四件事：
1. **跨语言统一二进制帧格式**：数据定义收敛于 `Packet/NetPacketTypes.h`（纯数据 `FNetPacketHeader` / `FNetPacket`），小端序编解码与边界核验独立收敛于无状态 `Codec/NetPacketCodec.h`；
2. **协议与序列化正交解耦**：`Transport/INetTransport.h` 只搬运 `TConstArrayView<uint8>` 切片；`Serializer/INetSerializer.h` 以 `TOptional<TArray<uint8>>` 值语义完成反射结构与字节流互转；
3. **跨插件消息总线网络信封宿主**：`Message/GameNetworkMessageTypes.h` 承载 `TAG_Game_Net_Outbound` / `TAG_Game_Net_Inbound` / `TAG_Game_Net_StateChanged`，既避免平级模块强依赖 `WebSockets`，又保证 Layer 0 `GameCore` 绝对不掺杂任何网络领域语义；
4. **单机与自动化测试脱机闭环**：`Transport/MockNetTransport.h` 在纯内存中模拟连接生命周期、路由回包、延迟抖动与断线注入，业务模块无需编写 `if (bIsSinglePlayer)` 分支。

> **职责红线**：`GameNet` 套件专司业务长连接（登录、大厅、商城、背包、聊天、匹配），局内高频角色位移、属性复制与预测回滚一律交由 UE 原生 `UNetDriver` / `Iris` 承担。

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

- **不可信边界防御**：`FNetPacketCodec::IsHeaderValid(Header, MaxAllowedPayloadBytes)` 在分配任何负载内存前率先核验 `Magic == 0x5545`、`HeaderSize == 16` 以及 `PayloadLength <= MaxAllowedPayloadBytes`（默认 2 MB），从根源阻断畸形包与超大包 OOM 攻击。
- **粘包/半包安全拆帧**：`FNetPacketRingBuffer` 委托 `FNetPacketCodec` 执行头部校验，采用双倍扩容与高水位惰性紧缩策略，在流式传输通道上提供确定性解帧与异常熔断。
