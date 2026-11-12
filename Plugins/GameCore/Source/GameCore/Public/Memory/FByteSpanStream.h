/**
 * @file FByteSpanStream.h
 * @brief 基于连续内存切片的零拷贝二进制读写流原语（绝对零堆分配，支持小端标量、引擎数学类型、变长整数与流式操作符）
 */

#pragma once

#include "CoreMinimal.h"
#include <type_traits>
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"
#include "Math/Vector.h"
#include "Math/Rotator.h"
#include "Math/Quat.h"

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
