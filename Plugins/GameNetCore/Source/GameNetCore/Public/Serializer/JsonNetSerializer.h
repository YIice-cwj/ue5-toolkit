/**
 * @file JsonNetSerializer.h
 * @brief 基于 UE 原生 JsonObjectConverter 的 UTF-8 JSON 网络负载序列化器
 */

#pragma once

#include "CoreMinimal.h"
#include "Serializer/INetSerializer.h"

/**
 * @brief UTF-8 紧凑 JSON 序列化适配器（零第三方库依赖，跨语言明文互通）
 */
class GAMENETCORE_API FJsonNetSerializer final : public INetSerializer {
public:
    /**
     * @brief 构造 JSON 序列化器
     * @param bInStrictMode 为 true 时强制要求 JSON 包含目标结构体全部反射属性字段；默认 false 允许缺省可选字段以保持兼容
     */
    explicit FJsonNetSerializer(bool bInStrictMode = false) noexcept
        : bStrictMode(bInStrictMode) {
    }

    ~FJsonNetSerializer() override = default;

    /**
     * @brief 将反射结构体编码为无缩进紧凑 UTF-8 JSON 字节流
     * @param StructData 待序列化的结构体实例首地址
     * @param StructType 结构体反射元数据指针
     * @return 编码成功返回 UTF-8 字节数组，失败返回空值
     */
    [[nodiscard]] TOptional<TArray<uint8>> Serialize(
        const void* StructData,
        const UStruct* StructType
    ) const override;

    /**
     * @brief 将 UTF-8 JSON 字节切片解码写入目标反射结构体
     * @param InBytes UTF-8 JSON 字节切片
     * @param StructType 目标结构体反射元数据指针
     * @param OutStructData 已初始化的目标结构体内存首地址
     * @return JSON 语法合法且字段转换成功返回 true
     */
    bool Deserialize(TConstArrayView<uint8> InBytes, const UStruct* StructType, void* OutStructData) const override;

    /**
     * @brief 查询当前是否启用严格字段匹配模式
     * @return 严格模式状态
     */
    [[nodiscard]] bool IsStrictMode() const noexcept {
        return bStrictMode;
    }

private:
    bool bStrictMode{false};
};

