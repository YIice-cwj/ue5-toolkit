/**
 * @file FGameQuantizationTest.cpp
 * @brief FGameQuantization 与 TBitMask 单元测试（浮点半精度、多位宽量化、64位向量打包与强类型枚举位掩码）
 */

#include "Math/FGameQuantization.h"
#include "Types/TBitMask.h"

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

namespace {

enum class ETestChannelBits : uint32 {
    Visibility = 0,
    Collision  = 1,
    Audio      = 2,
    Effects    = 3,
    Combat     = 4,
};

}  // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameQuantizationFloatAndRotatorTest,
    "Game.Core.Math.Quantization.FloatAndRotator",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证 IEEE 754 半精度转换、8/16位范围量化与欧拉角紧凑打包
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameQuantizationFloatAndRotatorTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const float TestValues[] = {0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 128.5f};
    for (float Val : TestValues) {
        const uint16 HalfEncoded = FGameQuantization::FloatToHalf(Val);
        const float Restored = FGameQuantization::HalfToFloat(HalfEncoded);
        TestNearlyEqual(TEXT("半精度浮点往返转换一致"), Restored, Val, 0.01f);
    }

    const uint8 Q8Zero = FGameQuantization::QuantizeFloatToUint8(0.0f, 0.0f, 100.0f);
    const uint8 Q8Half = FGameQuantization::QuantizeFloatToUint8(50.0f, 0.0f, 100.0f);
    const uint8 Q8Full = FGameQuantization::QuantizeFloatToUint8(100.0f, 0.0f, 100.0f);
    TestEqual(TEXT("8位量化下限为 0"), Q8Zero, static_cast<uint8>(0));
    TestEqual(TEXT("8位量化中点为 128"), Q8Half, static_cast<uint8>(128));
    TestEqual(TEXT("8位量化上限为 255"), Q8Full, static_cast<uint8>(255));

    TestNearlyEqual(TEXT("8位还原浮点准确"), FGameQuantization::DequantizeUint8ToFloat(Q8Half, 0.0f, 100.0f), 50.0f, 0.5f);
    TestEqual(TEXT("非法范围直接安全返回 0"), FGameQuantization::QuantizeFloatToUint8(10.0f, 100.0f, 50.0f), static_cast<uint8>(0));

    const uint16 Q16 = FGameQuantization::QuantizeFloatToUint16(250.0f, 0.0f, 1000.0f);
    TestNearlyEqual(TEXT("16位还原浮点误差小于 0.05"), FGameQuantization::DequantizeUint16ToFloat(Q16, 0.0f, 1000.0f), 250.0f, 0.05f);

    const uint16 PackedRot = FGameQuantization::PackYawPitch(90.0f, 45.0f);
    float RestoredYaw = 0.0f;
    float RestoredPitch = 0.0f;
    FGameQuantization::UnpackYawPitch(PackedRot, RestoredYaw, RestoredPitch);
    TestNearlyEqual(TEXT("偏航角还原误差在 1.5 度以内"), RestoredYaw, 90.0f, 1.5f);
    TestNearlyEqual(TEXT("俯仰角还原误差在 1.0 度以内"), RestoredPitch, 45.0f, 1.0f);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGameQuantizationVectorAndBitMaskTest,
    "Game.Core.Math.Quantization.VectorAndBitMask",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

/**
 * @brief 验证三维坐标向量 64 位压缩与 TBitMask 强类型位掩码逻辑运算
 * @param Parameters 自动化测试参数
 * @return 全部断言通过返回 true
 */
bool FGameQuantizationVectorAndBitMaskTest::RunTest([[maybe_unused]] const FString& Parameters) {
    const FVector TestVectors[] = {
        FVector(0.0, 0.0, 0.0),
        FVector(1500.5, 3200.2, 500.8),
        FVector(-4500.3, -1200.7, -800.4),
        FVector(50000.0, -50000.0, 20000.0)
    };

    constexpr double Resolution = 0.1;
    for (const FVector& Vec : TestVectors) {
        const uint64 Packed64 = FGameQuantization::PackVectorToUint64(Vec, Resolution);
        const FVector RestoredVec = FGameQuantization::UnpackUint64ToVector(Packed64, Resolution);
        TestNearlyEqual(TEXT("向量 X 解压误差小于分辨率"), RestoredVec.X, Vec.X, Resolution);
        TestNearlyEqual(TEXT("向量 Y 解压误差小于分辨率"), RestoredVec.Y, Vec.Y, Resolution);
        TestNearlyEqual(TEXT("向量 Z 解压误差小于分辨率"), RestoredVec.Z, Vec.Z, Resolution);
    }

    TBitMask<ETestChannelBits> Mask;
    TestTrue(TEXT("初始掩码为空"), Mask.IsEmpty());
    TestEqual(TEXT("初始原生值为 0"), Mask.GetRawMask(), static_cast<uint32>(0));

    Mask.Set(ETestChannelBits::Visibility);
    Mask.Set(ETestChannelBits::Combat);
    TestTrue(TEXT("包含 Visibility"), Mask.Has(ETestChannelBits::Visibility));
    TestTrue(TEXT("包含 Combat"), Mask.Has(ETestChannelBits::Combat));
    TestFalse(TEXT("未包含 Audio"), Mask.Has(ETestChannelBits::Audio));

    Mask.Toggle(ETestChannelBits::Visibility);
    TestFalse(TEXT("翻转后 Visibility 被清除"), Mask.Has(ETestChannelBits::Visibility));

    Mask.Clear(ETestChannelBits::Combat);
    TestTrue(TEXT("清除全部后掩码为空"), Mask.IsEmpty());

    TBitMask<ETestChannelBits> MaskA;
    MaskA.Set(ETestChannelBits::Collision).Set(ETestChannelBits::Audio);

    TBitMask<ETestChannelBits> MaskB;
    MaskB.Set(ETestChannelBits::Audio).Set(ETestChannelBits::Effects);

    TestTrue(TEXT("HasAny 判定交集成功"), MaskA.HasAny(MaskB));
    TestFalse(TEXT("HasAll 判定子集不满足"), MaskA.HasAll(MaskB));
    TestFalse(TEXT("HasNone 判定有交集返回 false"), MaskA.HasNone(MaskB));

    TBitMask<ETestChannelBits> MaskC(ETestChannelBits::Combat);
    TestTrue(TEXT("HasNone 判定互斥集合返回 true"), MaskA.HasNone(MaskC));
    MaskC.Clear();
    TestTrue(TEXT("Clear 清空全部标记位"), MaskC.IsEmpty());

    const TBitMask<ETestChannelBits> UnionMask = MaskA | MaskB;
    TestTrue(TEXT("并集包含 Collision"), UnionMask.Has(ETestChannelBits::Collision));
    TestTrue(TEXT("并集包含 Audio"), UnionMask.Has(ETestChannelBits::Audio));
    TestTrue(TEXT("并集包含 Effects"), UnionMask.Has(ETestChannelBits::Effects));

    const TBitMask<ETestChannelBits> IntersectMask = MaskA & MaskB;
    TestTrue(TEXT("交集仅包含 Audio"), IntersectMask.Has(ETestChannelBits::Audio));
    TestFalse(TEXT("交集不包含 Collision"), IntersectMask.Has(ETestChannelBits::Collision));

    const FVector ExtremeVec(1.0e20, -1.0e20, 1.0e20);
    const uint64 ExtremePacked = FGameQuantization::PackVectorToUint64(ExtremeVec, 0.1);
    const FVector ExtremeRestored = FGameQuantization::UnpackUint64ToVector(ExtremePacked, 0.1);
    TestNearlyEqual(TEXT("超大正坐标安全钳制到 21 位上限"), ExtremeRestored.X, 104857.5, 0.1);
    TestNearlyEqual(TEXT("超大负坐标安全钳制到 21 位下限"), ExtremeRestored.Y, -104857.6, 0.1);
    TestNearlyEqual(TEXT("超大 Z 坐标安全钳制到 22 位上限"), ExtremeRestored.Z, 209715.1, 0.1);

    const ETestChannelBits OutOfBoundsBit = static_cast<ETestChannelBits>(64);
    TBitMask<ETestChannelBits> SafeBoundsMask(OutOfBoundsBit);
    TestTrue(TEXT("越界枚举位构造的位掩码安全忽略为空"), SafeBoundsMask.IsEmpty());
    SafeBoundsMask.Set(OutOfBoundsBit);
    TestFalse(TEXT("越界枚举位 Has 查询安全返回 false"), SafeBoundsMask.Has(OutOfBoundsBit));

    TMap<TBitMask<ETestChannelBits>, FString> MaskMap;
    MaskMap.Add(MaskA, TEXT("CollisionAndAudio"));
    MaskMap.Add(MaskB, TEXT("AudioAndEffects"));
    const FString* FoundA = MaskMap.Find(MaskA);
    TestNotNull(TEXT("TBitMask 作为 TMap 键可精确命中"), FoundA);
    if (FoundA != nullptr) {
        TestEqual(TEXT("命中值正确"), *FoundA, FString(TEXT("CollisionAndAudio")));
    }

    return true;
}

#endif  // WITH_AUTOMATION_TESTS
