/**
 * @file FStateChecksum.cpp
 * @brief 64 位确定性状态校验器与联机分叉探测器实现
 */

#include "Determinism/FStateChecksum.h"

namespace {

static constexpr uint64 GInitialHashSeed = 0x9E3779B97F4A7C15ULL;
static constexpr uint64 GNonFiniteFloatSentinel = 0x7FF8000000000001ULL;

/**
 * @brief 执行 MurmurHash3 64 位终态雪崩位混合
 * @param Value 输入 64 位整数
 * @return 雪崩混合后的 64 位散列值
 */
[[nodiscard]] constexpr uint64 Mix64(uint64 Value) noexcept {
    Value ^= Value >> 33;
    Value *= 0xff51afd7ed558ccdULL;
    Value ^= Value >> 33;
    Value *= 0xc4ceb9fe1a85ec53ULL;
    Value ^= Value >> 33;
    return Value;
}

}

/**
 * @brief 构造函数：初始化哈希种子
 */
FStateChecksumBuilder::FStateChecksumBuilder() noexcept
    : HashState(GInitialHashSeed) {
}

/**
 * @brief 注入布尔值
 * @param bValue 输入布尔值
 */
void FStateChecksumBuilder::AddBool(bool bValue) noexcept {
    AddUInt32(bValue ? 1U : 0U);
}

/**
 * @brief 注入 32 位有符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddInt32(int32 Value) noexcept {
    AddUInt32(static_cast<uint32>(Value));
}

/**
 * @brief 注入 64 位有符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddInt64(int64 Value) noexcept {
    AddUInt64(static_cast<uint64>(Value));
}

/**
 * @brief 注入 32 位无符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddUInt32(uint32 Value) noexcept {
    HashState = Mix64(HashState ^ (static_cast<uint64>(Value) * 0x517cc1b727220a95ULL));
}

/**
 * @brief 注入 64 位无符号整数
 * @param Value 输入数值
 */
void FStateChecksumBuilder::AddUInt64(uint64 Value) noexcept {
    HashState = Mix64(HashState ^ (Value * 0x9E3779B97F4A7C15ULL));
}

/**
 * @brief 注入字符串视图（按字符底层字节连续注入，各端表现严格一致）
 * @param Str 输入字符串视图
 */
void FStateChecksumBuilder::AddString(FStringView Str) noexcept {
    AddBytes(Str.GetData(), Str.Len() * sizeof(TCHAR));
}

/**
 * @brief 注入 FName（自动转为全字符串内容注入，杜绝各端资产加载顺序不同导致的索引伪分叉）
 * @param Name 输入 FName
 */
void FStateChecksumBuilder::AddName(const FName& Name) noexcept {
    const FString NameStr = Name.ToString();
    AddString(FStringView(NameStr));
}

/**
 * @brief 注入按指定精度量化截断的单精度浮点数
 * @param Value 输入浮点数
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddFloatQuantized(float Value, float Precision) noexcept {
    if (!FMath::IsFinite(Value)) {
        AddUInt64(GNonFiniteFloatSentinel);
        return;
    }
    const float SafePrecision = Precision > 1e-7f ? Precision : 0.001f;
    const int64 Quantized = FMath::RoundToInt64(Value / SafePrecision);
    AddUInt64(static_cast<uint64>(Quantized));
}

/**
 * @brief 注入按指定精度量化截断的双精度浮点数
 * @param Value 输入双精度浮点数
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddDoubleQuantized(double Value, double Precision) noexcept {
    if (!FMath::IsFinite(Value)) {
        AddUInt64(GNonFiniteFloatSentinel);
        return;
    }
    const double SafePrecision = Precision > 1e-12 ? Precision : 0.001;
    const int64 Quantized = FMath::RoundToInt64(Value / SafePrecision);
    AddUInt64(static_cast<uint64>(Quantized));
}

/**
 * @brief 注入按指定精度量化截断的三维空间向量
 * @param Vec 输入三维向量
 * @param Precision 量化精度
 */
void FStateChecksumBuilder::AddVectorQuantized(const FVector& Vec, double Precision) noexcept {
    AddDoubleQuantized(Vec.X, Precision);
    AddDoubleQuantized(Vec.Y, Precision);
    AddDoubleQuantized(Vec.Z, Precision);
}

/**
 * @brief 注入原生二进制连续字节流
 * @param Data 字节流首地址
 * @param Size 字节数
 */
void FStateChecksumBuilder::AddBytes(const void* Data, size_t Size) noexcept {
    if (Data == nullptr || Size == 0) {
        return;
    }

    const uint8* BytePtr = static_cast<const uint8*>(Data);
    for (size_t Index = 0; Index < Size; ++Index) {
        HashState = Mix64(HashState ^ (static_cast<uint64>(BytePtr[Index]) * 0x100000001b3ULL));
    }
}

/**
 * @brief 计算并输出最终的 64 位确定性状态哈希码
 * @return 最终校验码
 */
uint64 FStateChecksumBuilder::Finalize() const noexcept {
    return Mix64(HashState);
}

/**
 * @brief 重置哈希状态机以供复用
 */
void FStateChecksumBuilder::Reset() noexcept {
    HashState = GInitialHashSeed;
}

/**
 * @brief 记录本地指定帧计算出的状态校验码
 * @param Frame 仿真帧序号
 * @param Checksum 本地校验码
 */
void FDesyncDetector::RecordLocalChecksum(uint64 Frame, uint64 Checksum) noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    History[Slot].Frame = Frame;
    History[Slot].Checksum = Checksum;
    History[Slot].bValid = true;
}

/**
 * @brief 校验远端传入的帧校验码
 * @param Frame 远端帧序号
 * @param RemoteChecksum 远端校验码
 * @param OutReport 校验报告输出
 * @return 未分叉且校验成功返回 true
 */
bool FDesyncDetector::VerifyRemoteChecksum(uint64 Frame, uint64 RemoteChecksum, FDesyncReport& OutReport) {
    OutReport.Frame = Frame;
    OutReport.RemoteChecksum = RemoteChecksum;

    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    if (!History[Slot].bValid || History[Slot].Frame != Frame) {
        OutReport.LocalChecksum = 0ULL;
        OutReport.bDiverged = false;
        return false;
    }

    OutReport.LocalChecksum = History[Slot].Checksum;
    if (History[Slot].Checksum == RemoteChecksum) {
        OutReport.bDiverged = false;
        return true;
    }

    OutReport.bDiverged = true;
    if (OnDesyncDetectedDelegate.IsBound()) {
        OnDesyncDetectedDelegate.Execute(OutReport);
    }
    return false;
}

/**
 * @brief 绑定状态分叉报警回调
 * @param InCallback 回调委托
 */
void FDesyncDetector::SetDesyncCallback(FOnDesyncDetected InCallback) {
    OnDesyncDetectedDelegate = MoveTemp(InCallback);
}

/**
 * @brief 检查本地历史记录中是否存在指定帧的记录
 * @param Frame 目标帧序号
 * @return 存在有效记录返回 true
 */
bool FDesyncDetector::HasFrame(uint64 Frame) const noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    return History[Slot].bValid && History[Slot].Frame == Frame;
}

/**
 * @brief 查询本地历史记录中是否存在指定帧的记录
 * @param Frame 目标帧序号
 * @param OutChecksum 输出校验码
 * @return 存在有效记录返回 true
 */
bool FDesyncDetector::GetLocalChecksum(uint64 Frame, uint64& OutChecksum) const noexcept {
    const uint32 Slot = static_cast<uint32>(Frame & Mask);
    if (History[Slot].bValid && History[Slot].Frame == Frame) {
        OutChecksum = History[Slot].Checksum;
        return true;
    }
    return false;
}

/**
 * @brief 清空所有历史快照记录
 */
void FDesyncDetector::Reset() noexcept {
    for (uint32 Index = 0; Index < HistoryCapacity; ++Index) {
        History[Index].bValid = false;
    }
}
