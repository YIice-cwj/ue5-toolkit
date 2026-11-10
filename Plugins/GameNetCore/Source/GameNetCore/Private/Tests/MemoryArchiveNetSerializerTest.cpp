/**
 * @file MemoryArchiveNetSerializerTest.cpp
 * @brief UE 原生二进制 Archive 网络序列化器（FMemoryArchiveNetSerializer）往返保真与截断脏字节防御单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Serializer/MemoryArchiveNetSerializer.h"
#include "Tests/GameNetCoreTestTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetMemoryArchiveSerializerTest,
    "Game.Net.Core.MemoryArchiveSerializer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证二进制 Archive 序列化器的复杂结构体往返一致性、半包截断拦截与尾部脏字节拒绝
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetMemoryArchiveSerializerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    FGameNetTestPurchaseRsp Expected;
    Expected.bSuccess = true;
    Expected.RemainingGold = 8850;
    Expected.DiscountRate = 0.85f;
    Expected.ReceiptMessage = TEXT("LegendarySwordPurchased");
    Expected.GrantedSerialNumbers = {101, 102, 103};
    Expected.BonusItem.ItemCode = FName(TEXT("HealthPotion"));
    Expected.BonusItem.Quantity = 5;

    const UScriptStruct* StructMeta = FGameNetTestPurchaseRsp::StaticStruct();
    const FMemoryArchiveNetSerializer BinarySerializer;

    TestTrue(TEXT("二进制序列化空数据指针返回失败态"), BinarySerializer.Serialize(nullptr, StructMeta).IsFailure());
    TestTrue(TEXT("二进制序列化空类型指针返回失败态"), BinarySerializer.Serialize(&Expected, nullptr).IsFailure());

    const TGameStatusResult<TArray<uint8>> BinaryBytes = BinarySerializer.SerializeStruct(Expected);
    TestTrue(TEXT("UE 二进制 Archive 序列化成功"), BinaryBytes.IsSuccess() && !BinaryBytes.GetValue().IsEmpty());
    if (BinaryBytes.IsFailure()) {
        return false;
    }

    const TGameStatusResult<FGameNetTestPurchaseRsp> BinaryDecoded =
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(BinaryBytes.GetValue());
    TestTrue(TEXT("UE 二进制 Archive 反序列化成功"), BinaryDecoded.IsSuccess());
    if (BinaryDecoded.IsFailure()) {
        return false;
    }

    TestEqual(TEXT("bSuccess 字段一致"), BinaryDecoded.GetValue().bSuccess, Expected.bSuccess);
    TestEqual(TEXT("RemainingGold 字段一致"), BinaryDecoded.GetValue().RemainingGold, Expected.RemainingGold);
    TestTrue(TEXT("DiscountRate 浮点近似一致"), FMath::IsNearlyEqual(BinaryDecoded.GetValue().DiscountRate, Expected.DiscountRate));
    TestEqual(TEXT("ReceiptMessage 字符串一致"), BinaryDecoded.GetValue().ReceiptMessage, Expected.ReceiptMessage);
    TestEqual(TEXT("GrantedSerialNumbers 数组一致"), BinaryDecoded.GetValue().GrantedSerialNumbers, Expected.GrantedSerialNumbers);
    TestTrue(TEXT("BonusItem 嵌套结构一致"), BinaryDecoded.GetValue().BonusItem == Expected.BonusItem);

    FGameNetTestPurchaseRsp TempOut;
    TestTrue(TEXT("二进制反序列化空类型指针返回失败态"), BinarySerializer.Deserialize(BinaryBytes.GetValue(), nullptr, &TempOut).IsFailure());
    TestTrue(TEXT("二进制反序列化空目标指针返回失败态"), BinarySerializer.Deserialize(BinaryBytes.GetValue(), StructMeta, nullptr).IsFailure());
    TestTrue(TEXT("二进制反序列化空切片对非空结构返回失败态"), BinarySerializer.Deserialize({}, StructMeta, &TempOut).IsFailure());

    const TConstArrayView<uint8> TruncatedSlice(BinaryBytes.GetValue().GetData(), BinaryBytes.GetValue().Num() / 2);
    TestTrue(
        TEXT("截断半包的二进制切片被安全拒绝"),
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(TruncatedSlice).IsFailure()
    );

    TArray<uint8> TrailingGarbage = BinaryBytes.GetValue();
    TrailingGarbage.Add(0xFF);
    TestTrue(
        TEXT("尾部夹带多余脏字节的二进制切片因 !AtEnd 被拒绝"),
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(TrailingGarbage).IsFailure()
    );

    return true;
}

#endif

