/**
 * @file GameNetCoreTestTypes.h
 * @brief GameNet 自动化测试专用的强类型反射协议载荷定义
 */

#pragma once

#include "CoreMinimal.h"

#include "GameNetCoreTestTypes.generated.h"

/**
 * @brief 测试用嵌套奖励条目结构体
 */
USTRUCT()
struct GAMENETCORE_API FGameNetTestRewardItem {
    GENERATED_BODY()

public:
    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一奖励条目
     * @return 所有字段均相等时返回 true
     */
    [[nodiscard]] bool operator==(const FGameNetTestRewardItem& Other) const noexcept {
        return ItemCode == Other.ItemCode && Quantity == Other.Quantity;
    }

public:
    UPROPERTY()
    FName ItemCode;

    UPROPERTY()
    int32 Quantity{0};
};

/**
 * @brief 测试用商店购买请求协议结构体
 */
USTRUCT()
struct GAMENETCORE_API FGameNetTestPurchaseReq {
    GENERATED_BODY()

public:
    UPROPERTY()
    int32 ShopItemId{0};

    UPROPERTY()
    int32 BuyCount{0};

    UPROPERTY()
    FString CouponToken;
};

/**
 * @brief 测试用商店购买响应协议结构体（覆盖标量、字符串、FName、数组与嵌套结构体）
 */
USTRUCT()
struct GAMENETCORE_API FGameNetTestPurchaseRsp {
    GENERATED_BODY()

public:
    UPROPERTY()
    bool bSuccess{false};

    UPROPERTY()
    int32 RemainingGold{0};

    UPROPERTY()
    float DiscountRate{1.0f};

    UPROPERTY()
    FString ReceiptMessage;

    UPROPERTY()
    TArray<int32> GrantedSerialNumbers;

    UPROPERTY()
    FGameNetTestRewardItem BonusItem;
};
