/**
 * @file WebSocketFragmentAssemblerTest.cpp
 * @brief WebSocket 二进制分片消息零拷贝视图重组器（FWebSocketFragmentAssembler）单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Transport/WebSocketFragmentAssembler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetWebSocketFragmentAssemblerTest,
    "Game.Net.Service.WebSocketFragmentAssembler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证单分片零拷贝直通视图、多分片累加拼装、超大缓存自动回收与超限熔断行为
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetWebSocketFragmentAssemblerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FWebSocketFragmentAssembler Assembler(16);
    TestEqual(TEXT("最大帧长等于 16 字节报文头加 16 字节最大负载"), Assembler.GetMaxFrameBytes(), int64(32));
    TestEqual(TEXT("初始缓存分片字节为 0"), Assembler.GetBufferedFragmentBytes(), 0);

    TConstArrayView<uint8> MessageView;
    const uint8 SingleChunk[] = {1, 2, 3, 4};
    const EWebSocketFragmentStatus DirectStatus = Assembler.AppendFragment(SingleChunk, true, MessageView);
    TestEqual(TEXT("单分片完整消息立即返回 MessageReady"), DirectStatus, EWebSocketFragmentStatus::MessageReady);
    TestEqual(TEXT("单分片消息长度一致"), MessageView.Num(), 4);
    TestTrue(TEXT("单分片零拷贝直通指向原始切片首地址"), MessageView.GetData() == SingleChunk);
    TestEqual(TEXT("单分片直通不残留缓存"), Assembler.GetBufferedFragmentBytes(), 0);

    const uint8 Part1[] = {10, 20, 30};
    const uint8 Part2[] = {40, 50};
    const uint8 Part3[] = {60};
    TestEqual(
        TEXT("首分片未结束返回 Incomplete"),
        Assembler.AppendFragment(Part1, false, MessageView),
        EWebSocketFragmentStatus::Incomplete
    );
    TestEqual(
        TEXT("中间分片未结束返回 Incomplete"),
        Assembler.AppendFragment(Part2, false, MessageView),
        EWebSocketFragmentStatus::Incomplete
    );
    TestEqual(TEXT("已累积 5 字节未完成分片"), Assembler.GetBufferedFragmentBytes(), 5);

    const EWebSocketFragmentStatus MultiStatus = Assembler.AppendFragment(Part3, true, MessageView);
    TestEqual(TEXT("末分片到达产出 MessageReady"), MultiStatus, EWebSocketFragmentStatus::MessageReady);
    TestEqual(TEXT("重组后总字节数为 6"), MessageView.Num(), 6);
    TestEqual(TEXT("重组后首字节为 10"), MessageView[0], static_cast<uint8>(10));
    TestEqual(TEXT("重组后末字节为 60"), MessageView[5], static_cast<uint8>(60));
    TestEqual(TEXT("重组完成后逻辑未完成字节归零"), Assembler.GetBufferedFragmentBytes(), 0);

    const EWebSocketFragmentStatus NextDirect = Assembler.AppendFragment(SingleChunk, true, MessageView);
    TestEqual(TEXT("完成多分片后下次单分片自动清理缓冲并零拷贝直通"), NextDirect, EWebSocketFragmentStatus::MessageReady);
    TestTrue(TEXT("后续单分片指向原始地址"), MessageView.GetData() == SingleChunk);

    TArray<uint8> TwentyBytes;
    TwentyBytes.SetNumZeroed(20);
    TestEqual(
        TEXT("追加 20 字节首分片未超 32 字节上限"),
        Assembler.AppendFragment(TwentyBytes, false, MessageView),
        EWebSocketFragmentStatus::Incomplete
    );
    const EWebSocketFragmentStatus OverflowStatus = Assembler.AppendFragment(TwentyBytes, true, MessageView);
    TestEqual(
        TEXT("累计 40 字节超过 32 字节最大帧长触发 FrameExceedsLimit"),
        OverflowStatus,
        EWebSocketFragmentStatus::FrameExceedsLimit
    );
    TestEqual(TEXT("超限拦截后自动清空半包缓存"), Assembler.GetBufferedFragmentBytes(), 0);

    FWebSocketFragmentAssembler LargeAssembler(128 * 1024);
    TArray<uint8> LargePart;
    LargePart.SetNumZeroed(40 * 1024);
    TestEqual(
        TEXT("推入 40KB 首分片"),
        LargeAssembler.AppendFragment(LargePart, false, MessageView),
        EWebSocketFragmentStatus::Incomplete
    );
    TestEqual(
        TEXT("推入 40KB 末分片拼装出 80KB 消息"),
        LargeAssembler.AppendFragment(LargePart, true, MessageView),
        EWebSocketFragmentStatus::MessageReady
    );
    TestEqual(TEXT("拼装出 80KB 完整视图"), MessageView.Num(), 80 * 1024);
    LargeAssembler.Reset();
    TestEqual(TEXT("大缓存 Reset 回收后未完成字节为 0"), LargeAssembler.GetBufferedFragmentBytes(), 0);

    return true;
}

#endif
