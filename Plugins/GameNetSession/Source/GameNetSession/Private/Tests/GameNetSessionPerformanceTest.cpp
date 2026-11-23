/**
 * @file GameNetSessionPerformanceTest.cpp
 * @brief GameNetSession 模块多轮统计微基准性能测试套件（房间发现表多房主 RTT、信标帧解包、Travel URL 防注入构造、描述符校验与预缓存宣告成帧）
 *
 * 遵循通用代码规范与 UE5 性能基准规范：多轮预热与采样（>= 5 轮），统计中位数、均值、极值与变异系数 (CV%)。
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

constexpr int32 DefaultWarmupRounds = 2;
constexpr int32 DefaultSampleRounds = 5;

/**
 * @brief 多轮基准测试统计指标快照
 */
struct FNetSessionBenchmarkMetrics {
    double MedianNs{0.0};
    double MeanNs{0.0};
    double CvPercent{0.0};
    double MinNs{0.0};
    double MaxNs{0.0};
    double OpsPerSec{0.0};
};

/**
 * @brief 执行多轮微基准测试并计算中位数、均值、极值、离散度 (CV%) 与吞吐率
 * @tparam TFunc 待测闭包类型
 * @param WarmupRounds 预热轮数
 * @param SampleRounds 正式采样轮数（>= 5）
 * @param IterationsPerRound 每轮迭代次数
 * @param Body 待执行测量闭包
 * @return 统计度量结果
 */
template <typename TFunc>
FNetSessionBenchmarkMetrics RunSessionBenchmark(
    int32 WarmupRounds,
    int32 SampleRounds,
    int32 IterationsPerRound,
    TFunc&& Body
) {
    for (int32 Round = 0; Round < WarmupRounds; ++Round) {
        Body(IterationsPerRound);
    }

    TArray<double> SamplesNs;
    SamplesNs.Reserve(SampleRounds);

    double SumNs = 0.0;
    for (int32 Round = 0; Round < SampleRounds; ++Round) {
        const uint64 StartCycles = FPlatformTime::Cycles64();
        Body(IterationsPerRound);
        const double ElapsedSec = FMath::Max(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartCycles), 1.0e-9);
        const double NsPerOp = (ElapsedSec * 1.0e9) / static_cast<double>(IterationsPerRound);
        SamplesNs.Add(NsPerOp);
        SumNs += NsPerOp;
    }

    SamplesNs.Sort();
    const double MedianNs = SamplesNs[SampleRounds / 2];
    const double MeanNs = SumNs / static_cast<double>(SampleRounds);

    double VarianceSum = 0.0;
    for (const double Sample : SamplesNs) {
        const double Diff = Sample - MeanNs;
        VarianceSum += Diff * Diff;
    }
    const double StdDev = FMath::Sqrt(VarianceSum / static_cast<double>(SampleRounds));

    FNetSessionBenchmarkMetrics Metrics;
    Metrics.MedianNs = MedianNs;
    Metrics.MeanNs = MeanNs;
    Metrics.CvPercent = (MeanNs > 0.0) ? ((StdDev / MeanNs) * 100.0) : 0.0;
    Metrics.MinNs = SamplesNs[0];
    Metrics.MaxNs = SamplesNs.Last();
    Metrics.OpsPerSec = (MedianNs > 0.0) ? (1.0e9 / MedianNs) : 0.0;
    return Metrics;
}

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLanRoomDiscoveryTablePerfTest,
    "Game.Net.Session.Perf.DiscoveryTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试局域网发现表多房主广播 RTT 测算、原地更新及定期过期扫描
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FLanRoomDiscoveryTablePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FLanRoomDiscoveryTable Table(8.0f);

    TArray<FGameNetRoomDescriptor> RoomTemplates;
    RoomTemplates.Reserve(32);
    for (int32 Index = 0; Index < 32; ++Index) {
        FGameNetRoomDescriptor Desc;
        Desc.RoomId = FString::Printf(TEXT("Room_%03d"), Index);
        Desc.RoomName = FString::Printf(TEXT("Arena_%03d"), Index);
        Desc.MapPath = TEXT("/Game/Maps/Lobby");
        Desc.HostAddress = FString::Printf(TEXT("192.168.1.%d"), 10 + Index);
        Desc.HostGamePort = 7777 + Index;
        Desc.CurrentPlayers = 2;
        Desc.MaxPlayers = 8;
        RoomTemplates.Add(MoveTemp(Desc));
    }

    const FString SenderIp = TEXT("192.168.1.50");
    double SimulatedClock = 100.0;
    uint32 CurrentProbeSeq = Table.AllocateAndRecordProbe(SimulatedClock);
    TArray<FString> ExpiredScratch;
    int32 UpsertedCount = 0;

    constexpr int32 IterationsPerRound = 10000;
    const FNetSessionBenchmarkMetrics Metrics = RunSessionBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Table, &RoomTemplates, &SenderIp, &SimulatedClock, &CurrentProbeSeq, &ExpiredScratch, &UpsertedCount](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                SimulatedClock += 0.001;
                if ((Index & 31) == 0) {
                    CurrentProbeSeq = Table.AllocateAndRecordProbe(SimulatedClock);
                }
                const TOptional<FGameNetRoomDescriptor> Result = Table.UpsertDiscoveredRoom(
                    RoomTemplates[Index & 31],
                    SenderIp,
                    CurrentProbeSeq,
                    SimulatedClock + 0.012
                );
                if (Result.IsSet()) {
                    ++UpsertedCount;
                }
                if ((Index & 255) == 0) {
                    Table.PruneExpiredRooms(SimulatedClock, ExpiredScratch);
                }
            }
        }
    );

    TestEqual(TEXT("稳定去重房间数为 32"), Table.GetRoomCount(), 32);
    TestTrue(TEXT("成功写入计数大于 0"), UpsertedCount > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】LanRoomDiscoveryTable: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 1800.0;
    TestTrue(
        FString::Printf(TEXT("房间发现表更新与多房主 RTT 测算中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLanBeaconDatagramDecodePerfTest,
    "Game.Net.Session.Perf.DatagramDecode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试局域网信标数据包帧头解码与 JSON 反射反序列化链路
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FLanBeaconDatagramDecodePerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FJsonNetSerializer Serializer(false);

    FGameNetRoomDescriptor SourceRoom;
    SourceRoom.RoomId = TEXT("PerfRoom_001");
    SourceRoom.RoomName = TEXT("PerfArena");
    SourceRoom.MapPath = TEXT("/Game/Maps/Arena");
    SourceRoom.HostAddress = TEXT("192.168.1.100");
    SourceRoom.HostGamePort = 17777;
    SourceRoom.CurrentPlayers = 3;
    SourceRoom.MaxPlayers = 8;

    TArray<uint8> JsonPayload;
    TestTrue(TEXT("编码测试房间描述成功"), Serializer.SerializeStructTo(SourceRoom, JsonPayload).IsSuccess());

    FNetPacketHeader SourceHeader;
    SourceHeader.MessageId = FLanBeaconBroadcaster::LAN_ROOM_ANNOUNCE_MSG_ID;
    SourceHeader.SequenceId = 42;
    TArray<uint8> WireDatagram;
    FNetPacketCodec::EncodeFrameTo(SourceHeader, JsonPayload, WireDatagram);

    FGameNetRoomDescriptor DecodedRoom;
    FNetPacketHeader DecodedHeader;
    int32 SuccessCount = 0;

    constexpr int32 IterationsPerRound = 2000;
    const FNetSessionBenchmarkMetrics Metrics = RunSessionBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&WireDatagram, &Serializer, &DecodedRoom, &DecodedHeader, &SuccessCount](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                if (!FNetPacketCodec::TryDecodeHeader(WireDatagram, DecodedHeader)) {
                    continue;
                }
                const TConstArrayView<uint8> PayloadView(
                    WireDatagram.GetData() + FNetPacketHeader::WIRE_HEADER_BYTES,
                    static_cast<int32>(DecodedHeader.PayloadLength)
                );
                if (Serializer.DeserializeStructTo(PayloadView, DecodedRoom).IsSuccess()) {
                    ++SuccessCount;
                }
            }
        }
    );

    TestTrue(TEXT("解码计数大于 0"), SuccessCount > 0);
    TestEqual(TEXT("还原房间名一致"), DecodedRoom.RoomName, SourceRoom.RoomName);
    AddInfo(FString::Printf(
        TEXT("【基准】LanBeaconDatagramDecode: Median=%.2f us, Mean=%.2f us, CV=%.1f%%, 吞吐=%.2f Kops/s"),
        Metrics.MedianNs / 1000.0,
        Metrics.MeanNs / 1000.0,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e3
    ));

    constexpr double BudgetNsPerOp = 35000.0;
    TestTrue(
        FString::Printf(TEXT("局域网宣告帧解包中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNetTravelUrlBuilderPerfTest,
    "Game.Net.Session.Perf.TravelUrl",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试防选项注入的 ListenServer 与 Client Travel URL 构造性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FNetTravelUrlBuilderPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FString MapPath = TEXT("/Game/Maps/MultiplayerLobby");
    const FString ExtraOptions = TEXT("?MaxPlayers=8?Mode=Coop");

    FGameNetRoomDescriptor Room;
    Room.HostAddress = TEXT("192.168.1.25");
    Room.HostGamePort = 17777;

    int32 TotalUrlChars = 0;

    constexpr int32 IterationsPerRound = 20000;
    const FNetSessionBenchmarkMetrics Metrics = RunSessionBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&MapPath, &ExtraOptions, &Room, &TotalUrlChars](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                const FString ListenUrl = UNetTravelLibrary::BuildListenServerTravelUrl(MapPath, 7777, ExtraOptions);
                const FString ClientUrl = UNetTravelLibrary::BuildClientTravelUrl(Room, TEXT("token=abc123"));
                TotalUrlChars += ListenUrl.Len() + ClientUrl.Len();
            }
        }
    );

    TestTrue(TEXT("累计生成的 URL 字符数大于 0"), TotalUrlChars > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】NetTravelUrlBuilder(Listen+Client): Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 1500.0;
    TestTrue(
        FString::Printf(TEXT("双 Travel URL 防注入构造中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetRoomDescriptorValidationPerfTest,
    "Game.Net.Session.Perf.DescriptorValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试房间描述符与建房请求字段合法性校验性能
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FGameNetRoomDescriptorValidationPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    TArray<FGameNetRoomDescriptor> Rooms;
    Rooms.Reserve(16);
    for (int32 Index = 0; Index < 16; ++Index) {
        FGameNetRoomDescriptor Room;
        Room.RoomId = (Index == 12) ? FString() : FString::Printf(TEXT("Room_%02d"), Index);
        Room.RoomName = FString::Printf(TEXT("Arena_%02d"), Index);
        Room.HostAddress = (Index == 13) ? FString() : FString::Printf(TEXT("192.168.1.%d"), 10 + Index);
        Room.HostGamePort = (Index == 14) ? 0 : (7777 + Index);
        Room.CurrentPlayers = 2;
        Room.MaxPlayers = (Index == 15) ? 0 : 4;
        Rooms.Add(MoveTemp(Room));
    }

    volatile int32 ValidSink = 0;

    constexpr int32 IterationsPerRound = 100000;
    const FNetSessionBenchmarkMetrics Metrics = RunSessionBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&Rooms, &ValidSink](int32 Count) {
            int32 LocalValid = 0;
            for (int32 Index = 0; Index < Count; ++Index) {
                if (Rooms[Index & 15].IsValid()) {
                    ++LocalValid;
                }
            }
            ValidSink = ValidSink + LocalValid;
        }
    );

    TestEqual(
        TEXT("12/16 有效房间占比校验计数精确"),
        static_cast<int32>(ValidSink),
        (DefaultWarmupRounds + DefaultSampleRounds) * (IterationsPerRound / 16 * 12)
    );
    AddInfo(FString::Printf(
        TEXT("【基准】RoomDescriptorValidation(16-mix): Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 50.0;
    TestTrue(
        FString::Printf(TEXT("描述符校验中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLanBeaconCachedFrameBuildPerfTest,
    "Game.Net.Session.Perf.CachedBeaconAnnouncement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 多轮基准测试房主预缓存 JSON 负载零反射复用成帧热路径（对照单次成帧吞吐）
 * @param Parameters 自动化测试参数
 * @return 测试是否通过性能预算
 */
bool FLanBeaconCachedFrameBuildPerfTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FJsonNetSerializer Serializer(false);
    FGameNetRoomDescriptor HostedRoom;
    HostedRoom.RoomId = TEXT("CachedHost_001");
    HostedRoom.RoomName = TEXT("TaoistArena");
    HostedRoom.MapPath = TEXT("/Game/Maps/SectArena");
    HostedRoom.HostGamePort = 17777;
    HostedRoom.CurrentPlayers = 2;
    HostedRoom.MaxPlayers = 4;

    TArray<uint8> CachedJsonPayload;
    TestTrue(TEXT("预编码房主 JSON 缓存成功"), Serializer.SerializeStructTo(HostedRoom, CachedJsonPayload).IsSuccess());

    TArray<uint8> SendScratchBuffer;
    SendScratchBuffer.Reserve(FNetPacketHeader::WIRE_HEADER_BYTES + CachedJsonPayload.Num());
    int64 TotalFramedBytes = 0;

    constexpr int32 IterationsPerRound = 50000;
    const FNetSessionBenchmarkMetrics Metrics = RunSessionBenchmark(
        DefaultWarmupRounds,
        DefaultSampleRounds,
        IterationsPerRound,
        [&CachedJsonPayload, &SendScratchBuffer, &TotalFramedBytes](int32 Count) {
            for (int32 Index = 0; Index < Count; ++Index) {
                FNetPacketHeader Header;
                Header.MessageId = FLanBeaconBroadcaster::LAN_ROOM_ANNOUNCE_MSG_ID;
                Header.SequenceId = static_cast<uint32>(Index + 1);
                FNetPacketCodec::EncodeFrameTo(Header, CachedJsonPayload, SendScratchBuffer);
                TotalFramedBytes += SendScratchBuffer.Num();
            }
        }
    );

    TestTrue(TEXT("累计组装字节数大于 0"), TotalFramedBytes > 0);
    AddInfo(FString::Printf(
        TEXT("【基准】CachedBeaconAnnouncementFrame: Median=%.2f ns, Mean=%.2f ns, CV=%.1f%%, 吞吐=%.2f Mops/s"),
        Metrics.MedianNs,
        Metrics.MeanNs,
        Metrics.CvPercent,
        Metrics.OpsPerSec / 1.0e6
    ));

    constexpr double BudgetNsPerOp = 150.0;
    TestTrue(
        FString::Printf(TEXT("预缓存房主宣告成帧中位耗时应低于 %.1f ns (实测: %.2f ns)"), BudgetNsPerOp, Metrics.MedianNs),
        Metrics.MedianNs < BudgetNsPerOp
    );
    return true;
}

#endif
