/**
 * @file UGameNetSessionPerformanceTest.cpp
 * @brief GameNetSession 模块微基准性能测试套件（房间发现表高频更新 / 局域网数据报快速反解派发 / 联机跳转URL生成 / 房间描述符校验）
 */

#include "CoreMinimal.h"

#if WITH_AUTOMATION_TESTS

#include "Codec/NetPacketCodec.h"
#include "HAL/PlatformTime.h"
#include "LAN/LanBeaconBroadcaster.h"
#include "LAN/LanRoomDiscoveryTable.h"
#include "Misc/AutomationTest.h"
#include "Serializer/JsonNetSerializer.h"
#include "Travel/NetTravelLibrary.h"
#include "Types/GameNetSessionTypes.h"

namespace {

/**
 * @brief 构造用于基准测试的标准房间描述符样本
 * @param RoomIndex 房间索引序号
 * @return 初始化的房间描述符
 */
FGameNetRoomDescriptor BuildPerfSampleRoom(int32 RoomIndex) {
    FGameNetRoomDescriptor Descriptor;
    Descriptor.RoomId = FString::Printf(TEXT("Room_Benchmark_%04d"), RoomIndex);
    Descriptor.RoomName = FString::Printf(TEXT("Benchmark_Arena_%04d"), RoomIndex);
    Descriptor.MapPath = TEXT("/Game/Maps/Competitive/Arena_01");
    Descriptor.HostAddress = FString::Printf(TEXT("192.168.1.%d"), (RoomIndex % 200) + 1);
    Descriptor.HostGamePort = 7777 + (RoomIndex % 100);
    Descriptor.CurrentPlayers = (RoomIndex % 4) + 1;
    Descriptor.MaxPlayers = 8;
    Descriptor.PingMilliseconds = 25 + (RoomIndex % 50);
    Descriptor.bIsLanRoom = true;
    return Descriptor;
}

}  // namespace

/**
 * @brief 局域网房间发现表高频插入更新、探测序号 RTT 测算与过期剔除微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetLanDiscoveryTablePerfTest,
    "Game.Net.Session.Perf.DiscoveryTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行局域网房间发现表高频操作性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetLanDiscoveryTablePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanRoomDiscoveryTable Table(5.0f);
    constexpr int32 RoomPoolSize = 64;
    TArray<FGameNetRoomDescriptor> SampleRooms;
    SampleRooms.Reserve(RoomPoolSize);
    for (int32 Index = 0; Index < RoomPoolSize; ++Index) {
        SampleRooms.Add(BuildPerfSampleRoom(Index));
    }

    constexpr int32 WarmupIterations = 1000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        const uint32 ProbeSeq = Table.AllocateAndRecordProbe(10.0);
        Table.UpsertDiscoveredRoom(SampleRooms[Index % RoomPoolSize], TEXT("192.168.1.50"), ProbeSeq, 10.02);
    }
    Table.Reset();

    constexpr int32 Iterations = 20000;
    TArray<FGameNetRoomDescriptor> ReusableSnapshot;
    ReusableSnapshot.Reserve(RoomPoolSize);
    TArray<FString> ReusableExpiredIds;
    ReusableExpiredIds.Reserve(32);

    const uint64 StartCycles = FPlatformTime::Cycles64();
    double SimulatedClock = 100.0;
    for (int32 Index = 0; Index < Iterations; ++Index) {
        SimulatedClock += 0.001;
        const uint32 ProbeSeq = Table.AllocateAndRecordProbe(SimulatedClock);
        Table.UpsertDiscoveredRoom(
            SampleRooms[Index % RoomPoolSize],
            TEXT("192.168.1.50"),
            ProbeSeq,
            SimulatedClock + 0.015
        );
        if ((Index % 500) == 0) {
            Table.GetRoomsSnapshotTo(ReusableSnapshot);
            Table.PruneExpiredRoomsTo(SimulatedClock, ReusableExpiredIds);
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Kops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e3;

    TestTrue(TEXT("最终表内记录数大于 0"), Table.GetRoomCount() > 0);

    AddInfo(FString::Printf(
        TEXT("【性能指标】LanRoomDiscoveryTable: 迭代=%d 次, 总耗时=%.2f ms, 单次均耗=%.2f ns (%.2f μs), 吞吐=%.2f Kops/s"),
        Iterations,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        PerOpNs / 1000.0,
        Kops
    ));

    constexpr double BudgetNsPerOp = 2000.0;
    TestTrue(
        FString::Printf(TEXT("发现表高频更新单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 局域网信标数据报快速反解、校验与派发微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetLanDatagramDecodePerfTest,
    "Game.Net.Session.Perf.DatagramDecode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行局域网信标报文反解与派发性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetLanDatagramDecodePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetRoomDescriptor HostedRoom = BuildPerfSampleRoom(7);
    const FJsonNetSerializer JsonSerializer;
    TArray<uint8> EncodedPayload;
    const TGameStatusResult<void> SerializeStatus = JsonSerializer.SerializeStructTo(HostedRoom, EncodedPayload);
    TestTrue(TEXT("测试房间描述符 JSON 编码成功"), SerializeStatus.IsSuccess());

    FNetPacketHeader AnnounceHeader;
    AnnounceHeader.MessageId = FLanBeaconBroadcaster::LAN_ROOM_ANNOUNCE_MSG_ID;
    AnnounceHeader.SequenceId = 42;

    TArray<uint8> WireFrame;
    FNetPacketCodec::EncodeFrameTo(AnnounceHeader, EncodedPayload, WireFrame);

    constexpr int32 WarmupIterations = 1000;
    FGameNetRoomDescriptor WarmupRoom;
    FNetPacketHeader WarmupHeader;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        if (FNetPacketCodec::TryDecodeHeaderFast(WireFrame, WarmupHeader)) {
            const TConstArrayView<uint8> PayloadView(
                WireFrame.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
                static_cast<int32>(WarmupHeader.PayloadLength)
            );
            JsonSerializer.DeserializeStructTo(PayloadView, WarmupRoom);
        }
    }

    constexpr int32 Iterations = 20000;
    FNetPacketHeader DecodedHeader;
    FGameNetRoomDescriptor DecodedRoom;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        if (FNetPacketCodec::TryDecodeHeaderFast(WireFrame, DecodedHeader)) {
            const TConstArrayView<uint8> PayloadView(
                WireFrame.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
                static_cast<int32>(DecodedHeader.PayloadLength)
            );
            JsonSerializer.DeserializeStructTo(PayloadView, DecodedRoom);
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations);
    const double Kops = (static_cast<double>(Iterations) / ElapsedSeconds) / 1e3;
    const double TotalMB = (static_cast<double>(WireFrame.Num() * Iterations)) / (1024.0 * 1024.0);
    const double ThroughputMBs = TotalMB / ElapsedSeconds;

    TestEqual(TEXT("反解房间 ID 正确"), DecodedRoom.RoomId, HostedRoom.RoomId);
    TestEqual(TEXT("反解房间名称正确"), DecodedRoom.RoomName, HostedRoom.RoomName);

    AddInfo(FString::Printf(
        TEXT("【性能指标】LanDatagramDecode: 迭代=%d 次, 帧大小=%d B, 总耗时=%.2f ms, 单次均耗=%.2f ns (%.2f μs), 吞吐=%.2f Kops/s (%.2f MB/s)"),
        Iterations,
        WireFrame.Num(),
        ElapsedSeconds * 1000.0,
        PerOpNs,
        PerOpNs / 1000.0,
        Kops,
        ThroughputMBs
    ));

    constexpr double BudgetNsPerOp = 20000.0;
    TestTrue(
        FString::Printf(TEXT("信标数据报反解单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 联机地图跳转防注入 URL 词法组装微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetTravelUrlPerfTest,
    "Game.Net.Session.Perf.TravelUrl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行联机地图跳转 URL 构造性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetTravelUrlPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString MapPath = TEXT("/Game/Maps/Dungeon/Arena_BossRoom");
    const FString ExtraOptions = TEXT("?MaxPlayers=8?GameMode=Hardcore");
    const FGameNetRoomDescriptor SampleRoom = BuildPerfSampleRoom(99);

    constexpr int32 WarmupIterations = 2000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        const FString ListenUrl = UNetTravelLibrary::BuildListenServerTravelUrl(MapPath, 7777, ExtraOptions);
        const FString ClientUrl = UNetTravelLibrary::BuildClientTravelUrl(SampleRoom, ExtraOptions);
        const FString ConnectUrl = SampleRoom.GetConnectUrl();
        (void)ListenUrl;
        (void)ClientUrl;
        (void)ConnectUrl;
    }

    constexpr int32 Iterations = 50000;
    FString LastListenUrl;
    FString LastClientUrl;
    FString LastConnectUrl;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        LastListenUrl = UNetTravelLibrary::BuildListenServerTravelUrl(MapPath, 7777, ExtraOptions);
        LastClientUrl = UNetTravelLibrary::BuildClientTravelUrl(SampleRoom, ExtraOptions);
        LastConnectUrl = SampleRoom.GetConnectUrl();
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations * 3);
    const double Mops = (static_cast<double>(Iterations * 3) / ElapsedSeconds) / 1e6;

    TestFalse(TEXT("ListenServer URL 非空"), LastListenUrl.IsEmpty());
    TestFalse(TEXT("ClientTravel URL 非空"), LastClientUrl.IsEmpty());
    TestFalse(TEXT("ConnectUrl 非空"), LastConnectUrl.IsEmpty());

    AddInfo(FString::Printf(
        TEXT("【性能指标】NetTravelUrlBuilder: 迭代=%d 次(共 %d 次URL生成), 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        Iterations * 3,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 500.0;
    TestTrue(
        FString::Printf(TEXT("Travel URL 构建单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

/**
 * @brief 房间描述符有效性校验与相等性判断微基准性能测试
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetRoomDescriptorValidationPerfTest,
    "Game.Net.Session.Perf.DescriptorValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 执行房间描述符有效性校验性能测试
 * @param Parameters 测试附加参数
 * @return 全部断言通过返回 true
 */
bool FGameNetRoomDescriptorValidationPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetRoomDescriptor RoomA = BuildPerfSampleRoom(1);
    const FGameNetRoomDescriptor RoomB = BuildPerfSampleRoom(1);
    const FGameNetRoomDescriptor RoomC = BuildPerfSampleRoom(2);

    constexpr int32 WarmupIterations = 5000;
    for (int32 Index = 0; Index < WarmupIterations; ++Index) {
        const bool bValid = RoomA.IsValid();
        const bool bEqual = (RoomA == RoomB);
        (void)bValid;
        (void)bEqual;
    }

    constexpr int32 Iterations = 100000;
    int32 ValidCount = 0;
    int32 EqualCount = 0;

    const uint64 StartCycles = FPlatformTime::Cycles64();
    for (int32 Index = 0; Index < Iterations; ++Index) {
        if (RoomA.IsValid()) {
            ++ValidCount;
        }
        if (RoomA == RoomB) {
            ++EqualCount;
        }
        if (RoomA == RoomC) {
            --EqualCount;
        }
    }
    const uint64 ElapsedCycles = FPlatformTime::Cycles64() - StartCycles;

    const double ElapsedSeconds = FPlatformTime::ToSeconds64(ElapsedCycles);
    const double ElapsedNs = ElapsedSeconds * 1e9;
    const double PerOpNs = ElapsedNs / static_cast<double>(Iterations * 2);
    const double Mops = (static_cast<double>(Iterations * 2) / ElapsedSeconds) / 1e6;

    TestEqual(TEXT("校验全部有效"), ValidCount, Iterations);
    TestEqual(TEXT("相等计数正确"), EqualCount, Iterations);

    AddInfo(FString::Printf(
        TEXT("【性能指标】RoomDescriptorValidation: 迭代=%d 次(共 %d 次判定), 总耗时=%.2f ms, 单次均耗=%.2f ns, 吞吐=%.2f Mops/s"),
        Iterations,
        Iterations * 2,
        ElapsedSeconds * 1000.0,
        PerOpNs,
        Mops
    ));

    constexpr double BudgetNsPerOp = 50.0;
    TestTrue(
        FString::Printf(TEXT("描述符校验单次均耗应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, PerOpNs),
        PerOpNs < BudgetNsPerOp
    );

    return true;
}

#endif
