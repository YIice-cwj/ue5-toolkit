/**
 * @file FByteSpanStreamTest.cpp
 * @brief 零拷贝内存二进制流读写器单元测试
 */

#include "Memory/FByteSpanWriter.h"
#include "Memory/FByteSpanReader.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FByteSpanStreamScalarAndBytesTest,
    "Game.Core.Memory.ByteSpanStream.ScalarAndBytes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证平凡标量类型与批量字节读写
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FByteSpanStreamScalarAndBytesTest::RunTest([[maybe_unused]] const FString& Parameters) {
    uint8 RawBuffer[128]{0};
    FByteSpanWriter Writer(MakeArrayView(RawBuffer, 128));

    TestEqual(TEXT("初始已写字节为 0"), Writer.GetBytesWritten(), 0);
    TestEqual(TEXT("初始剩余容量为 128"), Writer.GetRemainingCapacity(), 128);
    TestEqual(TEXT("初始 GetRemainingBytes 匹配剩余容量"), Writer.GetRemainingBytes(), 128);

    const uint32 ValU32 = 0xDEADBEEF;
    const float ValFloat = 3.14159f;
    const int64 ValI64 = -9876543210LL;
    const bool ValBool = true;

    TestTrue(TEXT("写入 uint32 成功"), Writer.Write(ValU32));
    TestTrue(TEXT("写入 float 成功"), Writer.Write(ValFloat));
    TestTrue(TEXT("写入 int64 成功"), Writer.Write(ValI64));
    TestTrue(TEXT("写入 bool 成功"), Writer.Write(ValBool));

    const uint8 ExtraBytes[4] = { 0x01, 0x02, 0x03, 0x04 };
    TestTrue(TEXT("写入原始字节切片成功"), Writer.WriteBytes(ExtraBytes, 4));

    const int32 ExpectedWritten = sizeof(uint32) + sizeof(float) + sizeof(int64) + sizeof(bool) + 4;
    TestEqual(TEXT("已写入字节数匹配"), Writer.GetBytesWritten(), ExpectedWritten);

    FByteSpanReader Reader(Writer.GetWrittenSpan());
    TestEqual(TEXT("读取器初始游标为 0"), Reader.GetBytesRead(), 0);
    TestEqual(TEXT("读取器剩余字节匹配写入大小"), Reader.GetRemainingBytes(), ExpectedWritten);
    TestTrue(TEXT("读取器包含未读字节"), Reader.HasRemaining());

    uint32 ReadU32 = 0;
    float ReadFloat = 0.0f;
    int64 ReadI64 = 0;
    bool ReadBool = false;
    uint8 ReadBytes[4] = { 0 };

    TestTrue(TEXT("读取 uint32 成功"), Reader.Read(ReadU32));
    TestEqual(TEXT("uint32 数值完全一致"), ReadU32, ValU32);

    TestTrue(TEXT("读取 float 成功"), Reader.Read(ReadFloat));
    TestEqual(TEXT("float 数值完全一致"), ReadFloat, ValFloat);

    TestTrue(TEXT("读取 int64 成功"), Reader.Read(ReadI64));
    TestEqual(TEXT("int64 数值完全一致"), ReadI64, ValI64);

    TestTrue(TEXT("读取 bool 成功"), Reader.Read(ReadBool));
    TestEqual(TEXT("bool 数值完全一致"), ReadBool, ValBool);

    TestTrue(TEXT("读取原始字节切片成功"), Reader.ReadBytes(ReadBytes, 4));
    TestEqual(TEXT("字节切片首字节匹配"), ReadBytes[0], 0x01);
    TestEqual(TEXT("字节切片尾字节匹配"), ReadBytes[3], 0x04);

    TestFalse(TEXT("读取完毕后无剩余字节"), Reader.HasRemaining());
    TestEqual(TEXT("剩余字节计数为 0"), Reader.GetRemainingBytes(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FByteSpanStreamVarIntAndStringTest,
    "Game.Core.Memory.ByteSpanStream.VarIntAndString",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 LEB128 变长整数、ZigZag 编码与 UTF-8 字符串读写
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FByteSpanStreamVarIntAndStringTest::RunTest([[maybe_unused]] const FString& Parameters) {
    uint8 RawBuffer[256]{0};
    FByteSpanWriter Writer(MakeArrayView(RawBuffer, 256));

    const uint64 TestVarU64[] = { 0ULL, 1ULL, 127ULL, 128ULL, 16384ULL, 0xFFFFFFFFFFFFFFFFULL };
    for (uint64 Val : TestVarU64) {
        TestTrue(TEXT("写入 VarUInt64 成功"), Writer.WriteVarUInt64(Val));
    }

    const int64 TestVarI64[] = { 0LL, -1LL, 1LL, -1000LL, 1000LL, -9223372036854775807LL, 9223372036854775807LL };
    for (int64 Val : TestVarI64) {
        TestTrue(TEXT("写入 VarInt64 成功"), Writer.WriteVarInt64(Val));
    }

    const FString TestStrA = TEXT("");
    const FString TestStrB = TEXT("Hello GameCore!");
    const FString TestStrC = TEXT("游戏核心库高精度中文测试");

    TestTrue(TEXT("写入空字符串成功"), Writer.WriteString(TestStrA));
    TestTrue(TEXT("写入 ASCII 字符串成功"), Writer.WriteString(TestStrB));
    TestTrue(TEXT("写入 UTF-8 中文字符串成功"), Writer.WriteString(TestStrC));

    FByteSpanReader Reader(Writer.GetWrittenSpan());

    for (uint64 Expected : TestVarU64) {
        uint64 ReadVal = 0;
        TestTrue(TEXT("读取 VarUInt64 成功"), Reader.ReadVarUInt64(ReadVal));
        TestEqual(TEXT("VarUInt64 数值完全一致"), ReadVal, Expected);
    }

    for (int64 Expected : TestVarI64) {
        int64 ReadVal = 0;
        TestTrue(TEXT("读取 VarInt64 成功"), Reader.ReadVarInt64(ReadVal));
        TestEqual(TEXT("VarInt64 数值完全一致"), ReadVal, Expected);
    }

    FString ReadStrA;
    FString ReadStrB;
    FString ReadStrC;

    TestTrue(TEXT("读取空字符串成功"), Reader.ReadString(ReadStrA));
    TestEqual(TEXT("空字符串内容匹配"), ReadStrA, TestStrA);

    TestTrue(TEXT("读取 ASCII 字符串成功"), Reader.ReadString(ReadStrB));
    TestEqual(TEXT("ASCII 字符串内容匹配"), ReadStrB, TestStrB);

    TestTrue(TEXT("读取中文字符串成功"), Reader.ReadString(ReadStrC));
    TestEqual(TEXT("中文字符串内容匹配"), ReadStrC, TestStrC);

    const uint8 MalformedVarU64[10] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x02 };
    FByteSpanReader CorruptReader(MakeArrayView(MalformedVarU64, 10));
    uint64 DummyVal = 0;
    TestFalse(TEXT("畸形 VarUInt64 第 10 字节溢出位被安全拦截"), CorruptReader.ReadVarUInt64(DummyVal));
    TestTrue(TEXT("读取失败设置错误标志"), CorruptReader.HasError());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FByteSpanStreamMathAndVarInt32Test,
    "Game.Core.Memory.ByteSpanStream.MathAndVarInt32",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 32 位 VarInt 与引擎常用数学结构（FVector, FVector3f, FRotator, FQuat4f）读写
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FByteSpanStreamMathAndVarInt32Test::RunTest([[maybe_unused]] const FString& Parameters) {
    uint8 RawBuffer[256]{0};
    FByteSpanWriter Writer(MakeArrayView(RawBuffer, 256));

    const uint32 TestU32[] = { 0U, 1U, 127U, 128U, 16383U, 65535U, 0xFFFFFFFFU };
    for (uint32 Val : TestU32) {
        TestTrue(TEXT("写入 VarUInt32 成功"), Writer.WriteVarUInt32(Val));
    }

    const int32 TestI32[] = { 0, -1, 1, -128, 128, -65536, 65536, -2147483647, 2147483647 };
    for (int32 Val : TestI32) {
        TestTrue(TEXT("写入 VarInt32 成功"), Writer.WriteVarInt32(Val));
    }

    const FVector3f Vec3f(1.5f, -2.5f, 3.5f);
    const FVector VecDouble(100.123456789, 200.987654321, -300.555555);
    const FRotator Rot(15.0, 45.0, 90.0);
    const FQuat4f Quat(0.0f, 0.7071f, 0.0f, 0.7071f);

    TestTrue(TEXT("写入 FVector3f 成功"), Writer.Write(Vec3f));
    TestTrue(TEXT("写入 FVector 成功"), Writer.Write(VecDouble));
    TestTrue(TEXT("写入 FRotator 成功"), Writer.Write(Rot));
    TestTrue(TEXT("写入 FQuat4f 成功"), Writer.Write(Quat));

    FByteSpanReader Reader(Writer.GetWrittenSpan());

    for (uint32 Expected : TestU32) {
        uint32 ReadVal = 0U;
        TestTrue(TEXT("读取 VarUInt32 成功"), Reader.ReadVarUInt32(ReadVal));
        TestEqual(TEXT("VarUInt32 数值完全一致"), ReadVal, Expected);
    }

    for (int32 Expected : TestI32) {
        int32 ReadVal = 0;
        TestTrue(TEXT("读取 VarInt32 成功"), Reader.ReadVarInt32(ReadVal));
        TestEqual(TEXT("VarInt32 数值完全一致"), ReadVal, Expected);
    }

    FVector3f ReadVec3f;
    FVector ReadVecDouble;
    FRotator ReadRot;
    FQuat4f ReadQuat;

    TestTrue(TEXT("读取 FVector3f 成功"), Reader.Read(ReadVec3f));
    TestEqual(TEXT("FVector3f 完全一致"), ReadVec3f, Vec3f);

    TestTrue(TEXT("读取 FVector 成功"), Reader.Read(ReadVecDouble));
    TestEqual(TEXT("FVector 完全一致"), ReadVecDouble, VecDouble);

    TestTrue(TEXT("读取 FRotator 成功"), Reader.Read(ReadRot));
    TestEqual(TEXT("FRotator 完全一致"), ReadRot, Rot);

    TestTrue(TEXT("读取 FQuat4f 成功"), Reader.Read(ReadQuat));
    TestEqual(TEXT("FQuat4f 完全一致"), ReadQuat, Quat);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FByteSpanStreamStreamingAndZeroCopyViewTest,
    "Game.Core.Memory.ByteSpanStream.StreamingAndZeroCopyView",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证流式操作符重载、错误黏性状态、原位探测与零拷贝视图
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FByteSpanStreamStreamingAndZeroCopyViewTest::RunTest([[maybe_unused]] const FString& Parameters) {
    uint8 RawBuffer[128]{0};
    FByteSpanWriter Writer(MakeArrayView(RawBuffer, 128));

    const int32 MagicHeader = 0x55AA;
    const float Scale = 2.5f;
    const FString PacketTag = TEXT("Snapshot");
    const uint8 RawPayload[6] = { 10, 20, 30, 40, 50, 60 };

    Writer << MagicHeader << Scale << PacketTag;
    TestTrue(TEXT("写入原始数据块成功"), Writer.WriteBytes(RawPayload, 6));
    TestTrue(TEXT("流写入状态处于 Ok"), Writer.IsOk());
    TestFalse(TEXT("流写入无错误标记"), Writer.HasError());

    FByteSpanReader Reader(Writer.GetWrittenSpan());

    int32 PeekMagic = 0;
    TestTrue(TEXT("原位探测 Magic 成功"), Reader.Peek(PeekMagic));
    TestEqual(TEXT("探测到的 Magic 匹配"), PeekMagic, MagicHeader);
    TestEqual(TEXT("原位探测不推进读取游标"), Reader.GetBytesRead(), 0);

    int32 ReadMagic = 0;
    float ReadScale = 0.0f;
    FString ReadTag;
    Reader >> ReadMagic >> ReadScale >> ReadTag;

    TestTrue(TEXT("流读取状态处于 Ok"), Reader.IsOk());
    TestEqual(TEXT("Magic 匹配"), ReadMagic, MagicHeader);
    TestEqual(TEXT("Scale 匹配"), ReadScale, Scale);
    TestEqual(TEXT("Tag 匹配"), ReadTag, PacketTag);

    TArrayView<const uint8> PayloadView;
    TestTrue(TEXT("零拷贝直接读取切片视图成功"), Reader.ReadBytesView(6, PayloadView));
    TestEqual(TEXT("切片大小为 6"), PayloadView.Num(), 6);
    TestEqual(TEXT("切片首字节匹配"), PayloadView[0], static_cast<uint8>(10));
    TestEqual(TEXT("切片尾字节匹配"), PayloadView[5], static_cast<uint8>(60));
    TestEqual(TEXT("切片指针直指源缓冲区"), PayloadView.GetData(), Writer.GetWrittenSpan().GetData() + (Reader.GetBytesRead() - 6));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FByteSpanStreamBoundsSafetyTest,
    "Game.Core.Memory.ByteSpanStream.BoundsSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证切片越界保护、黏性错误状态与安全降级
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FByteSpanStreamBoundsSafetyTest::RunTest([[maybe_unused]] const FString& Parameters) {
    uint8 TinyBuffer[4]{0};
    FByteSpanWriter Writer(MakeArrayView(TinyBuffer, 4));

    const uint32 ValA = 42;
    TestTrue(TEXT("写入 4 字节成功"), Writer.Write(ValA));
    TestEqual(TEXT("缓冲区已满"), Writer.GetRemainingCapacity(), 0);
    TestTrue(TEXT("写满前流状态处于 Ok"), Writer.IsOk());

    const uint8 OverflowByte = 1;
    TestFalse(TEXT("缓冲区溢出写入失败"), Writer.Write(OverflowByte));
    TestTrue(TEXT("溢出触发黏性错误状态"), Writer.HasError());
    TestFalse(TEXT("溢出后 IsOk 为 false"), Writer.IsOk());

    Writer.ClearError();
    TestTrue(TEXT("清除错误后状态恢复 Ok"), Writer.IsOk());

    FByteSpanReader Reader(Writer.GetWrittenSpan());
    uint32 ReadValA = 0;
    TestTrue(TEXT("读取有效 4 字节成功"), Reader.Read(ReadValA));
    TestEqual(TEXT("数值匹配"), ReadValA, ValA);
    TestTrue(TEXT("读满前流状态处于 Ok"), Reader.IsOk());

    uint8 OverflowRead = 0;
    TestFalse(TEXT("越界读取失败"), Reader.Read(OverflowRead));
    TestTrue(TEXT("越界触发读流黏性错误状态"), Reader.HasError());
    TestFalse(TEXT("越界批量读取失败"), Reader.ReadBytes(&OverflowRead, 1));
    TestFalse(TEXT("越界跳过失败"), Reader.Skip(1));

    TArrayView<const uint8> OverflowView;
    TestFalse(TEXT("越界视图读取失败"), Reader.ReadBytesView(1, OverflowView));

    TestFalse(TEXT("巨大 ByteCount 越界写入被安全拦截"), Writer.WriteBytes(TinyBuffer, MAX_int32));
    TestFalse(TEXT("巨大 ByteCount 越界读取被安全拦截"), Reader.ReadBytes(TinyBuffer, MAX_int32));

    const uint8 MaliciousData[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00 };
    FByteSpanReader MaliciousReader(MakeArrayView(MaliciousData, 8));
    TArrayView<const UTF8CHAR> MaliciousSpan;
    TestFalse(TEXT("巨额 VarInt 长度前缀越界读取被安全拦截"), MaliciousReader.ReadUtf8Span(MaliciousSpan));
    TestTrue(TEXT("拦截后触发黏性错误"), MaliciousReader.HasError());

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
