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

    TestFalse(TEXT("二进制序列化空数据指针返回空值"), BinarySerializer.Serialize(nullptr, StructMeta).IsSet());
    TestFalse(TEXT("二进制序列化空类型指针返回空值"), BinarySerializer.Serialize(&Expected, nullptr).IsSet());

    const TOptional<TArray<uint8>> BinaryBytes = BinarySerializer.SerializeStruct(Expected);
    TestTrue(TEXT("UE 二进制 Archive 序列化成功"), BinaryBytes.IsSet() && !BinaryBytes->IsEmpty());
    if (!BinaryBytes.IsSet()) {
        return false;
    }

    const TOptional<FGameNetTestPurchaseRsp> BinaryDecoded =
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(*BinaryBytes);
    TestTrue(TEXT("UE 二进制 Archive 反序列化成功"), BinaryDecoded.IsSet());
    if (!BinaryDecoded.IsSet()) {
        return false;
    }

    TestEqual(TEXT("bSuccess 字段一致"), BinaryDecoded->bSuccess, Expected.bSuccess);
    TestEqual(TEXT("RemainingGold 字段一致"), BinaryDecoded->RemainingGold, Expected.RemainingGold);
    TestTrue(TEXT("DiscountRate 浮点近似一致"), FMath::IsNearlyEqual(BinaryDecoded->DiscountRate, Expected.DiscountRate));
    TestEqual(TEXT("ReceiptMessage 字符串一致"), BinaryDecoded->ReceiptMessage, Expected.ReceiptMessage);
    TestEqual(TEXT("GrantedSerialNumbers 数组一致"), BinaryDecoded->GrantedSerialNumbers, Expected.GrantedSerialNumbers);
    TestTrue(TEXT("BonusItem 嵌套结构一致"), BinaryDecoded->BonusItem == Expected.BonusItem);

    FGameNetTestPurchaseRsp TempOut;
    TestFalse(TEXT("二进制反序列化空类型指针返回 false"), BinarySerializer.Deserialize(*BinaryBytes, nullptr, &TempOut));
    TestFalse(TEXT("二进制反序列化空目标指针返回 false"), BinarySerializer.Deserialize(*BinaryBytes, StructMeta, nullptr));
    TestFalse(TEXT("二进制反序列化空切片对非空结构返回 false"), BinarySerializer.Deserialize({}, StructMeta, &TempOut));

    const TConstArrayView<uint8> TruncatedSlice(BinaryBytes->GetData(), BinaryBytes->Num() / 2);
    TestFalse(
        TEXT("截断半包的二进制切片被安全拒绝"),
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(TruncatedSlice).IsSet()
    );

    TArray<uint8> TrailingGarbage = *BinaryBytes;
    TrailingGarbage.Add(0xFF);
    TestFalse(
        TEXT("尾部夹带多余脏字节的二进制切片因 !AtEnd 被拒绝"),
        BinarySerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(TrailingGarbage).IsSet()
    );

    return true;
}

#endif

