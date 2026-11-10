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

    TestTrue(TEXT("JSON 序列化空数据指针返回失败态"), DefaultJsonSerializer.Serialize(nullptr, StructMeta).IsFailure());
    TestTrue(TEXT("JSON 序列化空类型指针返回失败态"), DefaultJsonSerializer.Serialize(&Expected, nullptr).IsFailure());

    const TGameStatusResult<TArray<uint8>> JsonBytes = DefaultJsonSerializer.SerializeStruct(Expected);
    TestTrue(TEXT("JSON 泛型序列化复杂结构体成功"), JsonBytes.IsSuccess() && !JsonBytes.GetValue().IsEmpty());
    if (JsonBytes.IsFailure()) {
        return false;
    }

    const TGameStatusResult<FGameNetTestPurchaseRsp> JsonDecoded =
        DefaultJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(JsonBytes.GetValue());
    TestTrue(TEXT("JSON 泛型反序列化成功"), JsonDecoded.IsSuccess());
    if (JsonDecoded.IsFailure()) {
        return false;
    }

    TestEqual(TEXT("bSuccess 字段一致"), JsonDecoded.GetValue().bSuccess, Expected.bSuccess);
    TestEqual(TEXT("RemainingGold 字段一致"), JsonDecoded.GetValue().RemainingGold, Expected.RemainingGold);
    TestTrue(TEXT("DiscountRate 浮点近似一致"), FMath::IsNearlyEqual(JsonDecoded.GetValue().DiscountRate, Expected.DiscountRate));
    TestEqual(TEXT("ReceiptMessage 字符串一致"), JsonDecoded.GetValue().ReceiptMessage, Expected.ReceiptMessage);
    TestEqual(TEXT("GrantedSerialNumbers 数组一致"), JsonDecoded.GetValue().GrantedSerialNumbers, Expected.GrantedSerialNumbers);
    TestTrue(TEXT("BonusItem 嵌套结构一致"), JsonDecoded.GetValue().BonusItem == Expected.BonusItem);

    FGameNetTestPurchaseRsp TempOut;
    TestTrue(TEXT("JSON 反序列化空字节切片返回失败态"), DefaultJsonSerializer.Deserialize({}, StructMeta, &TempOut).IsFailure());
    TestTrue(TEXT("JSON 反序列化空类型指针返回失败态"), DefaultJsonSerializer.Deserialize(JsonBytes.GetValue(), nullptr, &TempOut).IsFailure());
    TestTrue(TEXT("JSON 反序列化空目标指针返回失败态"), DefaultJsonSerializer.Deserialize(JsonBytes.GetValue(), StructMeta, nullptr).IsFailure());

    const ANSICHAR BrokenJson[] = "{invalid_json";
    TestTrue(
        TEXT("畸形 JSON 文本安全拒绝"),
        DefaultJsonSerializer.Deserialize(
            TConstArrayView<uint8>(reinterpret_cast<const uint8*>(BrokenJson), sizeof(BrokenJson) - 1),
            StructMeta,
            &TempOut
        ).IsFailure()
    );

    const ANSICHAR ArrayJson[] = "[1, 2, 3]";
    TestTrue(
        TEXT("非对象顶层 JSON 数组安全拒绝"),
        DefaultJsonSerializer.Deserialize(
            TConstArrayView<uint8>(reinterpret_cast<const uint8*>(ArrayJson), sizeof(ArrayJson) - 1),
            StructMeta,
            &TempOut
        ).IsFailure()
    );

    const ANSICHAR ForwardCompatJson[] = R"({"bSuccess":true,"remainingGold":777,"unknownServerField":"v2"})";
    const TConstArrayView<uint8> ForwardCompatView(
        reinterpret_cast<const uint8*>(ForwardCompatJson),
        sizeof(ForwardCompatJson) - 1
    );
    const TGameStatusResult<FGameNetTestPurchaseRsp> CompatDecoded =
        DefaultJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(ForwardCompatView);
    TestTrue(TEXT("默认非严格模式忽略服务端新增未知字段保持兼容"), CompatDecoded.IsSuccess());
    if (CompatDecoded.IsSuccess()) {
        TestEqual(TEXT("向前兼容报文已知字段正常读取"), CompatDecoded.GetValue().RemainingGold, 777);
    }

    const FJsonNetSerializer StrictJsonSerializer(true);
    TestTrue(TEXT("严格模式标志位查询为 true"), StrictJsonSerializer.IsStrictMode());
    AddExpectedError(TEXT("Missing JSON value named"), EAutomationExpectedErrorFlags::Contains, 1);
    TestTrue(
        TEXT("严格模式下缺失结构体必填反射字段的报文被拒绝"),
        StrictJsonSerializer.DeserializeStruct<FGameNetTestPurchaseRsp>(ForwardCompatView).IsFailure()
    );

    return true;
}

#endif

