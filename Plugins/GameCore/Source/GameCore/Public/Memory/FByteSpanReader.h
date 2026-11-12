// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"

/**
 * @brief 零堆分配连续内存二进制切片读取器
 */
class GAMECORE_API FByteSpanReader {
public:
    constexpr FByteSpanReader() noexcept = default;

    /**
     * @brief 构造函数
     * @param InBuffer 源数据只读切片
     */
    constexpr explicit FByteSpanReader(TArrayView<const uint8> InBuffer) noexcept
        : Buffer(InBuffer)
        , ReadOffset(0)
        , bHasError(false) {
    }

    /**
     * @brief 读取平凡标量数据（整数、浮点、枚举）
     * @tparam T 平凡可复制类型
     * @param OutValue 传出数值引用
     * @return 成功读取返回 true，越界返回 false
     */
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    bool Read(T& OutValue) noexcept {
        if (static_cast<int32>(sizeof(T)) > (Buffer.Num() - ReadOffset)) {
            bHasError = true;
            return false;
        }
        FMemory::Memcpy(&OutValue, Buffer.GetData() + ReadOffset, sizeof(T));
        ReadOffset += static_cast<int32>(sizeof(T));
        return true;
    }

    /**
     * @brief 原位探测读取平凡标量数据（不移动读取游标）
     * @tparam T 平凡可复制类型
     * @param OutValue 传出数值引用
     * @return 探测成功返回 true，越界返回 false
     */
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    [[nodiscard]] bool Peek(T& OutValue) const noexcept {
        if (static_cast<int32>(sizeof(T)) > (Buffer.Num() - ReadOffset)) {
            return false;
        }
        FMemory::Memcpy(&OutValue, Buffer.GetData() + ReadOffset, sizeof(T));
        return true;
    }

    /**
     * @brief 批量读取原始字节数据并拷贝到输出缓冲区
     * @param OutData 输出缓冲区指针
     * @param ByteCount 读取字节数
     * @return 成功读取返回 true，剩余不足返回 false
     */
    bool ReadBytes(void* OutData, int32 ByteCount) noexcept {
        if (ByteCount <= 0) {
            return true;
        }
        if (OutData == nullptr || ByteCount > (Buffer.Num() - ReadOffset)) {
            bHasError = true;
            return false;
        }
        FMemory::Memcpy(OutData, Buffer.GetData() + ReadOffset, ByteCount);
        ReadOffset += ByteCount;
        return true;
    }

    /**
     * @brief 零拷贝获取子字节切片视图（直接指向源缓冲区，绝对零内存拷贝）
     * @param ByteCount 需要切片的字节长度
     * @param OutView 传出切片只读视图
     * @return 成功返回 true，越界返回 false
     */
    bool ReadBytesView(int32 ByteCount, TArrayView<const uint8>& OutView) noexcept {
        if (ByteCount < 0 || ByteCount > (Buffer.Num() - ReadOffset)) {
            bHasError = true;
            return false;
        }
        OutView = MakeArrayView(Buffer.GetData() + ReadOffset, ByteCount);
        ReadOffset += ByteCount;
        return true;
    }

    /**
     * @brief 读取 LEB128 变长 32 位无符号整数
     * @param OutValue 传出 32 位无符号整数
     * @return 成功解码返回 true，溢出或越界返回 false
     */
    bool ReadVarUInt32(uint32& OutValue) noexcept {
        OutValue = 0U;
        int32 Shift = 0;
        while (ReadOffset < Buffer.Num()) {
            const uint8 Byte = Buffer[ReadOffset++];
            OutValue |= (static_cast<uint32>(Byte & 0x7FU) << Shift);
            if ((Byte & 0x80U) == 0U) {
                return true;
            }
            Shift += 7;
            if (Shift >= 35) {
                bHasError = true;
                return false;
            }
        }
        bHasError = true;
        return false;
    }

    /**
     * @brief 读取 ZigZag 变长 32 位有符号整数
     * @param OutValue 传出 32 位有符号整数
     * @return 成功解码返回 true，失败返回 false
     */
    bool ReadVarInt32(int32& OutValue) noexcept {
        uint32 RawValue = 0U;
        if (!ReadVarUInt32(RawValue)) {
            return false;
        }
        OutValue = static_cast<int32>((RawValue >> 1) ^ (~(RawValue & 1U) + 1U));
        return true;
    }

    /**
     * @brief 读取 LEB128 变长 64 位无符号整数
     * @param OutValue 传出 64 位无符号整数
     * @return 成功解码返回 true，溢出或越界返回 false
     */
    bool ReadVarUInt64(uint64& OutValue) noexcept {
        OutValue = 0ULL;
        int32 Shift = 0;
        while (ReadOffset < Buffer.Num()) {
            const uint8 Byte = Buffer[ReadOffset++];
            if (Shift == 63) {
                if ((Byte & 0xFEU) != 0U) {
                    bHasError = true;
                    return false;
                }
                OutValue |= (static_cast<uint64>(Byte & 1U) << 63);
                return true;
            }
            OutValue |= (static_cast<uint64>(Byte & 0x7FU) << Shift);
            if ((Byte & 0x80U) == 0U) {
                return true;
            }
            Shift += 7;
        }
        bHasError = true;
        return false;
    }

    /**
     * @brief 读取 ZigZag 变长 64 位有符号整数
     * @param OutValue 传出 64 位有符号整数
     * @return 成功解码返回 true，失败返回 false
     */
    bool ReadVarInt64(int64& OutValue) noexcept {
        uint64 RawValue = 0ULL;
        if (!ReadVarUInt64(RawValue)) {
            return false;
        }
        OutValue = static_cast<int64>((RawValue >> 1) ^ (~(RawValue & 1ULL) + 1ULL));
        return true;
    }

    /**
     * @brief 零堆分配读取 UTF-8 字符串切片视图
     * @param OutUtf8View 传出直接指向源缓冲区的 UTF-8 只读字节切片
     * @return 成功读取返回 true，越界返回 false
     */
    bool ReadUtf8Span(TArrayView<const UTF8CHAR>& OutUtf8View) noexcept {
        uint32 ByteLen = 0U;
        if (!ReadVarUInt32(ByteLen)) {
            return false;
        }
        if (ByteLen == 0U) {
            OutUtf8View = TArrayView<const UTF8CHAR>();
            return true;
        }
        if (ByteLen > static_cast<uint32>(Buffer.Num() - ReadOffset)) {
            bHasError = true;
            return false;
        }
        OutUtf8View = MakeArrayView(reinterpret_cast<const UTF8CHAR*>(Buffer.GetData() + ReadOffset), static_cast<int32>(ByteLen));
        ReadOffset += static_cast<int32>(ByteLen);
        return true;
    }

    /**
     * @brief 读取 UTF-8 字符串并转换为 FString
     * @param OutStr 传出字符串引用
     * @return 成功读取返回 true，失败返回 false
     */
    bool ReadString(FString& OutStr) noexcept {
        TArrayView<const UTF8CHAR> Utf8Span;
        if (!ReadUtf8Span(Utf8Span)) {
            return false;
        }
        if (Utf8Span.IsEmpty()) {
            OutStr.Reset();
            return true;
        }
        FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Utf8Span.GetData()), Utf8Span.Num());
        OutStr = FString(FStringView(Converter.Get(), Converter.Length()));
        return true;
    }

    /**
     * @brief 流式读取平凡标量运算符（支持链式连续反序列化）
     * @tparam T 平凡可复制类型
     * @param OutValue 传出变量引用
     * @return 自身引用
     */
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    FByteSpanReader& operator>>(T& OutValue) noexcept {
        Read(OutValue);
        return *this;
    }

    /**
     * @brief 流式读取 FString 运算符
     * @param OutStr 传出字符串引用
     * @return 自身引用
     */
    FByteSpanReader& operator>>(FString& OutStr) noexcept {
        ReadString(OutStr);
        return *this;
    }

    /**
     * @brief 跳过指定字节数
     * @param ByteCount 跳过字节数
     * @return 成功跳过返回 true，越界返回 false
     */
    bool Skip(int32 ByteCount) noexcept {
        if (ByteCount < 0 || ByteCount > (Buffer.Num() - ReadOffset)) {
            bHasError = true;
            return false;
        }
        ReadOffset += ByteCount;
        return true;
    }

    /**
     * @brief 重置读游标与错误状态
     */
    constexpr void Reset() noexcept {
        ReadOffset = 0;
        bHasError = false;
    }

    /**
     * @brief 清除黏性错误状态
     */
    constexpr void ClearError() noexcept {
        bHasError = false;
    }

    /**
     * @brief 查询流是否处于健康状态（未发生越界或解码错误）
     * @return 健康返回 true
     */
    [[nodiscard]] constexpr bool IsOk() const noexcept {
        return !bHasError;
    }

    /**
     * @brief 查询流是否已发生解码或越界错误
     * @return 发生错误返回 true
     */
    [[nodiscard]] constexpr bool HasError() const noexcept {
        return bHasError;
    }

    /**
     * @brief 获取已读取字节数
     * @return 读取偏移量
     */
    [[nodiscard]] constexpr int32 GetBytesRead() const noexcept {
        return ReadOffset;
    }

    /**
     * @brief 获取剩余待读字节数
     * @return 剩余字节数
     */
    [[nodiscard]] constexpr int32 GetRemainingBytes() const noexcept {
        return Buffer.Num() - ReadOffset;
    }

    /**
     * @brief 查询是否仍有未读字节
     * @return 存在未读返回 true
     */
    [[nodiscard]] constexpr bool HasRemaining() const noexcept {
        return ReadOffset < Buffer.Num();
    }

    /**
     * @brief 获取剩余未读切片
     * @return 剩余字节切片
     */
    [[nodiscard]] constexpr TArrayView<const uint8> GetRemainingSpan() const noexcept {
        return MakeArrayView(Buffer.GetData() + ReadOffset, Buffer.Num() - ReadOffset);
    }

private:
    TArrayView<const uint8> Buffer;
    int32 ReadOffset = 0;
    bool bHasError = false;
};
