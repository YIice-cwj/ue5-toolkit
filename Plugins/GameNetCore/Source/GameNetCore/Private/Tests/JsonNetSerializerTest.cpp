/**
 * @file JsonNetSerializerTest.cpp
 * @brief UTF-8 紧凑 JSON 网络负载序列化器（FJsonNetSerializer）往返保真、向前兼容与畸形输入防御单元测试
 */

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Serializer/JsonNetSerializer.h"
#include "Tests/GameNetCoreTestTypes.h"

namespace {

/**
 * @brief 构造覆盖标量、字符串、数组与嵌套结构体的测试响应样本
 * @return 测试用购买响应结构体实例
 */
FGameNetTestPurchaseRsp BuildSampleJsonPurchaseRsp() {
    FGameNetTestPurchaseRsp Sample;
    Sample.bSuccess = true;
    Sample.RemainingGold = 8850;
    Sample.DiscountRate = 0.85f;
    Sample.ReceiptMessage = TEXT("LegendarySwordPurchased");
    Sample.GrantedSerialNumbers = {101, 102, 103};
    Sample.BonusItem.ItemCode = FName(TEXT("HealthPotion"));
    Sample.BonusItem.Quantity = 5;
    return Sample;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameNetJsonSerializerTest,
    "Game.Net.Core.JsonSerializer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 JSON 序列化器的复杂结构体往返一致性、向前兼容模式、严格模式及畸形输入防御
 * @param Parameters 自动化测试参数
 * @return 测试是否全部通过
 */
bool FGameNetJsonSerializerTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FGameNetTestPurchaseRsp Expected = BuildSampleJsonPurchaseRsp();
    const UScriptStruct* StructMeta = FGameNetTestPurchaseRsp::StaticStruct();

    const FJsonNetSerializer DefaultJsonSerializer;
    TestFalse(TEXT("默认 JSON 序列化器关闭严格模式以保持向前兼容"), DefaultJsonSerializer.IsStrictMode());

    TestFalse(TEXT("JSON 序列化空数据指针返回空值"), DefaultJsonSerializer.Serialize(nullptr, StructMeta).IsSet());
    TestFalse(TEXT("JSON 序列化空类型指针返回空值"), DefaultJsonSerializer.Serialize(&Expected, nullptr).IsSet());

    const TOptional<TArray<uint8>> JsonBytes = DefaultJsonSerializer.SerializeStruct(Expected);
    TestTrue(TEXT("JSON 泛型序列化复杂结构体成功"), JsonBytes.IsSet() && !JsonBytes->IsEmpty());
    if (!JsonBytes.IsSet()) {
        return false;
    }

    const TOptional<FGameNetTestPurchaseRsp> JsonDecoded =
        DefaultJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(*JsonBytes);
    TestTrue(TEXT("JSON 泛型反序列化成功"), JsonDecoded.IsSet());
    if (!JsonDecoded.IsSet()) {
        return false;
    }

    TestEqual(TEXT("bSuccess 字段一致"), JsonDecoded->bSuccess, Expected.bSuccess);
    TestEqual(TEXT("RemainingGold 字段一致"), JsonDecoded->RemainingGold, Expected.RemainingGold);
    TestTrue(TEXT("DiscountRate 浮点近似一致"), FMath::IsNearlyEqual(JsonDecoded->DiscountRate, Expected.DiscountRate));
    TestEqual(TEXT("ReceiptMessage 字符串一致"), JsonDecoded->ReceiptMessage, Expected.ReceiptMessage);
    TestEqual(TEXT("GrantedSerialNumbers 数组一致"), JsonDecoded->GrantedSerialNumbers, Expected.GrantedSerialNumbers);
    TestTrue(TEXT("BonusItem 嵌套结构一致"), JsonDecoded->BonusItem == Expected.BonusItem);

    FGameNetTestPurchaseRsp TempOut;
    TestFalse(TEXT("JSON 反序列化空字节切片返回 false"), DefaultJsonSerializer.Deserialize({}, StructMeta, &TempOut));
    TestFalse(TEXT("JSON 反序列化空类型指针返回 false"), DefaultJsonSerializer.Deserialize(*JsonBytes, nullptr, &TempOut));
    TestFalse(TEXT("JSON 反序列化空目标指针返回 false"), DefaultJsonSerializer.Deserialize(*JsonBytes, StructMeta, nullptr));

    const ANSICHAR BrokenJson[] = "{invalid_json";
    TestFalse(
        TEXT("畸形 JSON 文本安全拒绝"),
        DefaultJsonSerializer.Deserialize(
            TConstArrayView<uint8>(reinterpret_cast<const uint8*>(BrokenJson), sizeof(BrokenJson) - 1),
            StructMeta,
            &TempOut
        )
    );

    const ANSICHAR ArrayJson[] = "[1, 2, 3]";
    TestFalse(
        TEXT("非对象顶层 JSON 数组安全拒绝"),
        DefaultJsonSerializer.Deserialize(
            TConstArrayView<uint8>(reinterpret_cast<const uint8*>(ArrayJson), sizeof(ArrayJson) - 1),
            StructMeta,
            &TempOut
        )
    );

    const ANSICHAR ForwardCompatJson[] = R"({"bSuccess":true,"remainingGold":777,"unknownServerField":"v2"})";
    const TConstArrayView<uint8> ForwardCompatView(
        reinterpret_cast<const uint8*>(ForwardCompatJson),
        sizeof(ForwardCompatJson) - 1
    );
    const TOptional<FGameNetTestPurchaseRsp> CompatDecoded =
        DefaultJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(ForwardCompatView);
    TestTrue(TEXT("默认非严格模式忽略服务端新增未知字段保持兼容"), CompatDecoded.IsSet());
    if (CompatDecoded.IsSet()) {
        TestEqual(TEXT("向前兼容报文已知字段正常读取"), CompatDecoded->RemainingGold, 777);
    }

    const FJsonNetSerializer StrictJsonSerializer(true);
    TestTrue(TEXT("严格模式标志位查询为 true"), StrictJsonSerializer.IsStrictMode());
    AddExpectedError(TEXT("Missing JSON value named"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(
        TEXT("严格模式下缺失结构体必填反射字段的报文被拒绝"),
        StrictJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(ForwardCompatView).IsSet()
    );

    return true;
}

#endif

