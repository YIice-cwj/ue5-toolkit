// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"

/**
 * @brief 零堆分配连续内存二进制切片写入器
 */
class GAMECORE_API FByteSpanWriter {
public:
    constexpr FByteSpanWriter() noexcept = default;

    /**
     * @brief 构造函数
     * @param InBuffer 目标内存切片
     */
    constexpr explicit FByteSpanWriter(TArrayView<uint8> InBuffer) noexcept
        : Buffer(InBuffer)
        , WriteOffset(0)
        , bHasError(false) {
    }

    /**
     * @brief 写入平凡标量数据（整数、浮点、枚举）
     * @tparam T 平凡可复制类型
     * @param Value 待写入数值
     * @return 容量充足以完成写入返回 true，溢出返回 false
     */
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    bool Write(const T& Value) noexcept {
        if (static_cast<int32>(sizeof(T)) > (Buffer.Num() - WriteOffset)) {
            bHasError = true;
            return false;
        }
        FMemory::Memcpy(Buffer.GetData() + WriteOffset, &Value, sizeof(T));
        WriteOffset += static_cast<int32>(sizeof(T));
        return true;
    }

    /**
     * @brief 批量写入原始字节数据
     * @param Data 源数据指针
     * @param ByteCount 写入字节数
     * @return 成功写入返回 true，空间不足返回 false
     */
    bool WriteBytes(const void* Data, int32 ByteCount) noexcept {
        if (ByteCount <= 0) {
            return true;
        }
        if (Data == nullptr || ByteCount > (Buffer.Num() - WriteOffset)) {
            bHasError = true;
            return false;
        }
        FMemory::Memcpy(Buffer.GetData() + WriteOffset, Data, ByteCount);
        WriteOffset += ByteCount;
        return true;
    }

    /**
     * @brief 写入 LEB128 变长 32 位无符号整数（针对小型长度和 ID 优化）
     * @param Value 待编码 32 位无符号整数
     * @return 成功写入返回 true，容量不足返回 false
     */
    bool WriteVarUInt32(uint32 Value) noexcept {
        while (Value >= 0x80U) {
            if (WriteOffset >= Buffer.Num()) {
                bHasError = true;
                return false;
            }
            Buffer[WriteOffset++] = static_cast<uint8>((Value & 0x7FU) | 0x80U);
            Value >>= 7;
        }
        if (WriteOffset >= Buffer.Num()) {
            bHasError = true;
            return false;
        }
        Buffer[WriteOffset++] = static_cast<uint8>(Value & 0x7FU);
        return true;
    }

    /**
     * @brief 写入 ZigZag 编码变长 32 位有符号整数
     * @param Value 待编码 32 位有符号整数
     * @return 成功写入返回 true，容量不足返回 false
     */
    bool WriteVarInt32(int32 Value) noexcept {
        const uint32 Encoded = (static_cast<uint32>(Value) << 1) ^ (static_cast<uint32>(Value >> 31));
        return WriteVarUInt32(Encoded);
    }

    /**
     * @brief 写入 LEB128 变长 64 位无符号整数
     * @param Value 待编码 64 位无符号整数
     * @return 成功写入返回 true，容量不足返回 false
     */
    bool WriteVarUInt64(uint64 Value) noexcept {
        do {
            uint8 Byte = static_cast<uint8>(Value & 0x7FULL);
            Value >>= 7;
            if (Value != 0ULL) {
                Byte |= 0x80;
            }
            if (WriteOffset >= Buffer.Num()) {
                bHasError = true;
                return false;
            }
            Buffer[WriteOffset++] = Byte;
        } while (Value != 0ULL);
        return true;
    }

    /**
     * @brief 写入 ZigZag 编码变长 64 位有符号整数
     * @param Value 待编码 64 位有符号整数
     * @return 成功写入返回 true，容量不足返回 false
     */
    bool WriteVarInt64(int64 Value) noexcept {
        const uint64 Encoded = (static_cast<uint64>(Value) << 1) ^ (static_cast<uint64>(Value >> 63));
        return WriteVarUInt64(Encoded);
    }

    /**
     * @brief 写入 UTF-8 格式字符串切片（带变长 32 位字节长度前缀）
     * @param Str 字符串视图
     * @return 成功写入返回 true，容量不足返回 false
     */
    bool WriteString(FStringView Str) noexcept {
        const int32 Length = Str.Len();
        if (Length == 0) {
            return WriteVarUInt32(0U);
        }
        const auto Utf8 = StringCast<UTF8CHAR>(Str.GetData(), Length);
        const int32 ByteLen = Utf8.Length();
        if (!WriteVarUInt32(static_cast<uint32>(ByteLen))) {
            return false;
        }
        return WriteBytes(Utf8.Get(), ByteLen);
    }

    /**
     * @brief 流式写入平凡标量运算符（支持链式连续序列化）
     * @tparam T 平凡可复制类型
     * @param Value 待写入值
     * @return 自身引用
     */
    template <typename T>
    requires std::is_trivially_copyable_v<T>
    FByteSpanWriter& operator<<(const T& Value) noexcept {
        Write(Value);
        return *this;
    }

    /**
     * @brief 流式写入字符串视图运算符
     * @param Str 字符串视图
     * @return 自身引用
     */
    FByteSpanWriter& operator<<(FStringView Str) noexcept {
        WriteString(Str);
        return *this;
    }

    /**
     * @brief 流式写入 FString 运算符
     * @param Str 字符串引用
     * @return 自身引用
     */
    FByteSpanWriter& operator<<(const FString& Str) noexcept {
        WriteString(FStringView(Str));
        return *this;
    }

    /**
     * @brief 重置写游标与错误状态
     */
    constexpr void Reset() noexcept {
        WriteOffset = 0;
        bHasError = false;
    }

    /**
     * @brief 清除黏性错误状态
     */
    constexpr void ClearError() noexcept {
        bHasError = false;
    }

    /**
     * @brief 查询流是否处于健康状态（未发生溢出或写入失败）
     * @return 健康返回 true
     */
    [[nodiscard]] constexpr bool IsOk() const noexcept {
        return !bHasError;
    }

    /**
     * @brief 查询流是否已发生写入错误
     * @return 发生错误返回 true
     */
    [[nodiscard]] constexpr bool HasError() const noexcept {
        return bHasError;
    }

    /**
     * @brief 获取已写入字节数
     * @return 字节偏移量
     */
    [[nodiscard]] constexpr int32 GetBytesWritten() const noexcept {
        return WriteOffset;
    }

    /**
     * @brief 获取剩余可写空间容量
     * @return 剩余字节数
     */
    [[nodiscard]] constexpr int32 GetRemainingCapacity() const noexcept {
        return Buffer.Num() - WriteOffset;
    }

    /**
     * @brief 获取剩余可写字节数（对齐 Reader 命名）
     * @return 剩余字节数
     */
    [[nodiscard]] constexpr int32 GetRemainingBytes() const noexcept {
        return Buffer.Num() - WriteOffset;
    }

    /**
     * @brief 获取已写入有效数据的只读切片
     * @return 只读字节切片
     */
    [[nodiscard]] constexpr TArrayView<const uint8> GetWrittenSpan() const noexcept {
        return MakeArrayView(Buffer.GetData(), WriteOffset);
    }

    /**
     * @brief 获取剩余未写入缓冲区的可变切片
     * @return 可变字节切片
     */
    [[nodiscard]] constexpr TArrayView<uint8> GetRemainingSpan() noexcept {
        return MakeArrayView(Buffer.GetData() + WriteOffset, Buffer.Num() - WriteOffset);
    }

private:
    TArrayView<uint8> Buffer;
    int32 WriteOffset = 0;
    bool bHasError = false;
};
