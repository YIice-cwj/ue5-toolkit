/**
 * @file LanUdpSocketChannelTest.cpp
 * @brief IPv4 非阻塞广播 UDP Socket 通道（FLanUdpSocketChannel）绑定、收发、内核可读等待与边界拦截单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "LAN/LanUdpSocketChannel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetLanUdpSocketChannelTest,
    "Game.Net.Session.LanUdpSocketChannel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证非阻塞 UDP 通道生命周期、非法地址/端口拦截（含空与 0.0.0.0）、零分配空闲早退及本地环回无损收发
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetLanUdpSocketChannelTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanUdpSocketChannel SenderChannel;
    FLanUdpSocketChannel ReceiverChannel;
    FLanReceivedDatagram ReusableDatagram;

    TestFalse(TEXT("初始未打开状态"), SenderChannel.IsOpen());
    TestEqual(TEXT("未打开时绑定端口为 0"), SenderChannel.GetBoundPort(), 0);
    TestFalse(TEXT("未打开时等待可读返回 false"), ReceiverChannel.WaitReadable(0.01));
    TestFalse(TEXT("未打开时读取数据报返回 false"), ReceiverChannel.TryReceiveDatagram(ReusableDatagram));

    const uint8 SampleBytes[] = {0x11, 0x22, 0x33, 0x44};
    TestFalse(TEXT("未打开时发送返回 false"), SenderChannel.SendTo(SampleBytes, TEXT("127.0.0.1"), 15777));

    TestFalse(TEXT("非法 IP 字符串拒绝绑定"), SenderChannel.Open(TEXT("999.999.999.999"), 0));
    TestFalse(TEXT("越界端口拒绝绑定"), SenderChannel.Open(TEXT("127.0.0.1"), 70000));

    TestTrue(TEXT("发送通道绑定环回空闲端口成功"), SenderChannel.Open(TEXT("127.0.0.1"), 0));
    TestTrue(TEXT("接收通道绑定环回空闲端口成功"), ReceiverChannel.Open(TEXT("127.0.0.1"), 0));
    TestTrue(TEXT("绑定后自动分配有效非零端口"), ReceiverChannel.GetBoundPort() > 0);

    TestFalse(TEXT("发送空切片返回 false"), SenderChannel.SendTo({}, TEXT("127.0.0.1"), ReceiverChannel.GetBoundPort()));
    TestFalse(TEXT("向空目标 IP 发送返回 false"), SenderChannel.SendTo(SampleBytes, TEXT(""), ReceiverChannel.GetBoundPort()));
    TestFalse(
        TEXT("向 0.0.0.0 目标地址发送返回 false"),
        SenderChannel.SendTo(SampleBytes, TEXT("0.0.0.0"), ReceiverChannel.GetBoundPort())
    );
    TestFalse(
        TEXT("向非法目标 IP 发送返回 false"),
        SenderChannel.SendTo(SampleBytes, TEXT("invalid_ip"), ReceiverChannel.GetBoundPort())
    );
    TestFalse(TEXT("向非法目标端口 0 发送返回 false"), SenderChannel.SendTo(SampleBytes, TEXT("127.0.0.1"), 0));

    TestFalse(TEXT("空闲通道经 HasPendingData 零分配早退返回 false"), ReceiverChannel.TryReceiveDatagram(ReusableDatagram));

    TestTrue(
        TEXT("向接收通道端口发送数据报成功"),
        SenderChannel.SendTo(SampleBytes, TEXT("127.0.0.1"), ReceiverChannel.GetBoundPort())
    );
    TestTrue(TEXT("内核事件驱动检测到数据报到达"), ReceiverChannel.WaitReadable(1.0));

    TestTrue(TEXT("就地复用读取到达数据报成功"), ReceiverChannel.TryReceiveDatagram(ReusableDatagram));
    TestEqual(TEXT("收到字节长度一致"), ReusableDatagram.Bytes.Num(), 4);
    TestEqual(TEXT("首字节一致"), ReusableDatagram.Bytes[0], static_cast<uint8>(0x11));
    TestEqual(TEXT("来源 IP 为 127.0.0.1"), ReusableDatagram.SenderIp, FString(TEXT("127.0.0.1")));
    TestEqual(TEXT("来源端口等于发送方绑定端口"), ReusableDatagram.SenderPort, SenderChannel.GetBoundPort());

    TestFalse(TEXT("缓冲排空后再次非阻塞读取立即返回 false"), ReceiverChannel.TryReceiveDatagram(ReusableDatagram));

    const int32 ReservedCapacity = ReusableDatagram.Bytes.Max();
    ReusableDatagram.Reset();
    TestEqual(TEXT("Reset 后字节数组归零"), ReusableDatagram.Bytes.Num(), 0);
    TestTrue(TEXT("Reset 后保留缓冲容量"), ReusableDatagram.Bytes.Max() >= ReservedCapacity);

    TestTrue(
        TEXT("复用缓存目标地址再次向接收通道端口发送成功"),
        SenderChannel.SendTo(SampleBytes, TEXT("127.0.0.1"), ReceiverChannel.GetBoundPort())
    );
    TestTrue(TEXT("内核检测到第二次数据报到达"), ReceiverChannel.WaitReadable(1.0));
    TestTrue(TEXT("第二次就地复用读取数据报成功"), ReceiverChannel.TryReceiveDatagram(ReusableDatagram));
    TestEqual(TEXT("第二次就地读取字节长度为 4"), ReusableDatagram.Bytes.Num(), 4);

    SenderChannel.Close();
    ReceiverChannel.Close();
    TestFalse(TEXT("Close 后通道关闭"), SenderChannel.IsOpen());
    TestEqual(TEXT("Close 后绑定端口归零"), ReceiverChannel.GetBoundPort(), 0);

    return true;
}

#endif
