// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanUdpSocketChannel.h"

#include "IPAddress.h"
#include "Logging/GameLogCategories.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace {

/**
 * @brief 将 IPv4 地址与端口写入目标 FInternetAddr 实例
 * @param OutAddr 目标地址引用
 * @param IpString 目标 IPv4 字符串
 * @param Port 目标端口号（0 ~ 65535）
 * @param bAllowAnyAddress 是否允许空串与 0.0.0.0（仅 Bind 场景允许，SendTo 严禁）
 * @return 解析合法返回 true
 */
bool ConfigureIpv4InternetAddr(
    FInternetAddr& OutAddr,
    const FString& IpString,
    int32 Port,
    bool bAllowAnyAddress
) {
    if (Port < 0 || Port > 65535) {
        return false;
    }

    if (IpString.IsEmpty() || IpString == TEXT("0.0.0.0")) {
        if (!bAllowAnyAddress) {
            return false;
        }
        OutAddr.SetAnyAddress();
    } else if (IpString == TEXT("255.255.255.255")) {
        OutAddr.SetBroadcastAddress();
    } else {
        bool bIsValidIp = false;
        OutAddr.SetIp(*IpString, bIsValidIp);
        if (!bIsValidIp) {
            return false;
        }
    }

    OutAddr.SetPort(Port);
    return true;
}

}  // namespace

/**
 * @brief 析构时自动关闭并销毁底层 UDP Socket
 */
FLanUdpSocketChannel::~FLanUdpSocketChannel() {
    Close();
}

/**
 * @brief 创建并绑定 IPv4 非阻塞广播 UDP Socket
 * @param BindIp 本地绑定 IP 字符串
 * @param BindPort 本地绑定端口号
 * @return 绑定成功返回 true
 */
bool FLanUdpSocketChannel::Open(const FString& BindIp, int32 BindPort) {
    Close();

    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSub == nullptr) {
        GAME_LOG(Warning, TEXT("[LAN/UDP] 平台 SocketSubsystem 不可用，无法打开 UDP 通道。"));
        return false;
    }

    const TSharedRef<FInternetAddr> BindAddr = SocketSub->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
    if (!ConfigureIpv4InternetAddr(*BindAddr, BindIp, BindPort, true)) {
        GAME_LOG(Warning, TEXT("[LAN/UDP] 非法绑定地址或端口: Ip=%s, Port=%d"), *BindIp, BindPort);
        return false;
    }

    FSocket* CreatedSocket = SocketSub->CreateSocket(
        NAME_DGram,
        TEXT("GameNetLanUdpChannel"),
        FNetworkProtocolTypes::IPv4
    );
    if (CreatedSocket == nullptr) {
        GAME_LOG(Warning, TEXT("[LAN/UDP] 创建底层 IPv4 UDP Socket 失败。"));
        return false;
    }

    CreatedSocket->SetNonBlocking(true);
    CreatedSocket->SetBroadcast(true);
    if (BindPort != 0) {
        CreatedSocket->SetReuseAddr(true);
    }

    if (!CreatedSocket->Bind(*BindAddr)) {
        GAME_LOG(Warning, TEXT("[LAN/UDP] 绑定 UDP 端口失败: Ip=%s, Port=%d"), *BindIp, BindPort);
        CreatedSocket->Close();
        SocketSub->DestroySocket(CreatedSocket);
        return false;
    }

    RawSocket = CreatedSocket;
    BoundPort = RawSocket->GetPortNo();
    CachedSenderAddr = SocketSub->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
    CachedTargetAddr = SocketSub->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
    return true;
}

/**
 * @brief 关闭并释放当前持有的底层 UDP Socket
 */
void FLanUdpSocketChannel::Close() {
    if (RawSocket != nullptr) {
        RawSocket->Close();
        if (ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)) {
            SocketSub->DestroySocket(RawSocket);
        }
        RawSocket = nullptr;
    }
    BoundPort = 0;
    CachedSenderAddr.Reset();
    CachedTargetAddr.Reset();
}

/**
 * @brief 向指定目标 IP 与端口发送一帧完整 UDP 数据报
 * @param PayloadBytes 待发送字节切片
 * @param TargetIp 目标 IPv4 地址
 * @param TargetPort 目标 UDP 端口
 * @return 完整写出全部字节返回 true
 */
bool FLanUdpSocketChannel::SendTo(
    TConstArrayView<uint8> PayloadBytes,
    const FString& TargetIp,
    int32 TargetPort
) {
    if (RawSocket == nullptr || !CachedTargetAddr.IsValid() || PayloadBytes.IsEmpty() || TargetPort <= 0 || TargetPort > 65535) {
        return false;
    }

    if (!ConfigureIpv4InternetAddr(*CachedTargetAddr, TargetIp, TargetPort, false)) {
        return false;
    }

    int32 BytesSent = 0;
    const bool bSentOk = RawSocket->SendTo(PayloadBytes.GetData(), PayloadBytes.Num(), BytesSent, *CachedTargetAddr);
    return bSentOk && BytesSent == PayloadBytes.Num();
}

/**
 * @brief 非阻塞尝试从接收缓冲队列读取一帧到达的 UDP 数据报并写入外部复用对象
 * @param OutDatagram 接收数据报的外部复用对象
 * @return 成功读到数据报返回 true，无数据或读取失败返回 false
 */
bool FLanUdpSocketChannel::TryReceiveDatagram(FLanReceivedDatagram& OutDatagram) {
    if (RawSocket == nullptr || !CachedSenderAddr.IsValid()) {
        return false;
    }

    uint32 PendingBytes = 0;
    if (!RawSocket->HasPendingData(PendingBytes) || PendingBytes == 0) {
        return false;
    }

    OutDatagram.Bytes.SetNumUninitialized(MAX_DATAGRAM_BUFFER_BYTES);
    int32 BytesRead = 0;
    if (!RawSocket->RecvFrom(OutDatagram.Bytes.GetData(), MAX_DATAGRAM_BUFFER_BYTES, BytesRead, *CachedSenderAddr) || BytesRead <= 0) {
        OutDatagram.Bytes.Reset();
        return false;
    }

    OutDatagram.Bytes.SetNum(BytesRead, EAllowShrinking::No);
    OutDatagram.SenderIp = CachedSenderAddr->ToString(false);
    OutDatagram.SenderPort = CachedSenderAddr->GetPort();
    return true;
}

/**
 * @brief 基于内核 select/poll 事件驱动等待底层 Socket 可读就绪
 * @param MaxWaitSeconds 最大等待秒数上限
 * @return 超时前检测到可读数据报到达返回 true
 */
bool FLanUdpSocketChannel::WaitReadable(double MaxWaitSeconds) const {
    if (RawSocket == nullptr) {
        return false;
    }

    const double ClampedSeconds = FMath::Max(0.0, MaxWaitSeconds);
    return RawSocket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(ClampedSeconds));
}
