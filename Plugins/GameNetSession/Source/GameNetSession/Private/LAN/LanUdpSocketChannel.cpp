// Copyright Epic Games, Inc. All Rights Reserved.

#include "LAN/LanUdpSocketChannel.h"

#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace {

/**
 * @brief 将 IP 字符串与端口号解析构造为 IPv4 FInternetAddr
 * @param SocketSub 平台 Socket 子系统引用
 * @param IpString 目标 IP（支持 0.0.0.0 与 255.255.255.255）
 * @param Port 目标端口号（0 ~ 65535）
 * @return 合法时返回地址智能指针，非法时返回 nullptr
 */
TSharedPtr<FInternetAddr> BuildIpv4InternetAddr(
    ISocketSubsystem& SocketSub,
    const FString& IpString,
    int32 Port
) {
    if (Port < 0 || Port > 65535) {
        return nullptr;
    }

    const TSharedRef<FInternetAddr> Addr = SocketSub.CreateInternetAddr(FNetworkProtocolTypes::IPv4);
    if (IpString.IsEmpty() || IpString == TEXT("0.0.0.0")) {
        Addr->SetAnyAddress();
    } else if (IpString == TEXT("255.255.255.255")) {
        Addr->SetBroadcastAddress();
    } else {
        bool bIsValidIp = false;
        Addr->SetIp(*IpString, bIsValidIp);
        if (!bIsValidIp) {
            return nullptr;
        }
    }

    Addr->SetPort(Port);
    return Addr;
}

}

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
        return false;
    }

    const TSharedPtr<FInternetAddr> BindAddr = BuildIpv4InternetAddr(*SocketSub, BindIp, BindPort);
    if (!BindAddr.IsValid()) {
        return false;
    }

    FSocket* CreatedSocket = SocketSub->CreateSocket(
        NAME_DGram,
        TEXT("GameNetLanUdpChannel"),
        FNetworkProtocolTypes::IPv4
    );
    if (CreatedSocket == nullptr) {
        return false;
    }

    CreatedSocket->SetNonBlocking(true);
    CreatedSocket->SetBroadcast(true);
    if (BindPort != 0) {
        CreatedSocket->SetReuseAddr(true);
    }

    if (!CreatedSocket->Bind(*BindAddr)) {
        CreatedSocket->Close();
        SocketSub->DestroySocket(CreatedSocket);
        return false;
    }

    RawSocket = CreatedSocket;
    BoundPort = RawSocket->GetPortNo();
    CachedSenderAddr = SocketSub->CreateInternetAddr(FNetworkProtocolTypes::IPv4);
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
    if (RawSocket == nullptr || PayloadBytes.IsEmpty() || TargetPort <= 0 || TargetPort > 65535) {
        return false;
    }

    ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSub == nullptr) {
        return false;
    }

    const TSharedPtr<FInternetAddr> DestAddr = BuildIpv4InternetAddr(*SocketSub, TargetIp, TargetPort);
    if (!DestAddr.IsValid()) {
        return false;
    }

    int32 BytesSent = 0;
    const bool bSentOk = RawSocket->SendTo(PayloadBytes.GetData(), PayloadBytes.Num(), BytesSent, *DestAddr);
    return bSentOk && BytesSent == PayloadBytes.Num();
}

/**
 * @brief 非阻塞尝试从接收缓冲队列读取一帧到达的 UDP 数据报（复用预分配地址对象，消除每帧堆内存分配）
 * @return 有可读数据报时返回包含字节流与来源 IP/端口的值对象，无数据时返回空值
 */
TOptional<FLanReceivedDatagram> FLanUdpSocketChannel::TryReceiveDatagram() {
    if (RawSocket == nullptr || !CachedSenderAddr.IsValid()) {
        return NullOpt;
    }

    uint8 Buffer[MAX_DATAGRAM_BUFFER_BYTES];
    int32 BytesRead = 0;
    if (!RawSocket->RecvFrom(Buffer, MAX_DATAGRAM_BUFFER_BYTES, BytesRead, *CachedSenderAddr) || BytesRead <= 0) {
        return NullOpt;
    }

    FLanReceivedDatagram Datagram;
    Datagram.Bytes.Append(Buffer, BytesRead);
    Datagram.SenderIp = CachedSenderAddr->ToString(false);
    Datagram.SenderPort = CachedSenderAddr->GetPort();
    return Datagram;
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

