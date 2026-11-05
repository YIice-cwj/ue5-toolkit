/**
 * @file GameNetSessionTypes.h
 * @brief 房间会话生命周期状态枚举、局域网角色枚举与房间描述纯数据契约
 */

#pragma once

#include "CoreMinimal.h"

#include "GameNetSessionTypes.generated.h"

/**
 * @brief 会话与房间管理状态机枚举
 */
UENUM(BlueprintType)
enum class EGameNetSessionState : uint8 {
    Idle = 0 UMETA(DisplayName = "Idle"),
    Creating UMETA(DisplayName = "Creating"),
    Hosting UMETA(DisplayName = "Hosting"),
    Searching UMETA(DisplayName = "Searching"),
    Joining UMETA(DisplayName = "Joining"),
    Destroying UMETA(DisplayName = "Destroying")
};

/**
 * @brief 局域网 UDP 信标工作角色枚举
 */
UENUM(BlueprintType)
enum class EGameNetLanRole : uint8 {
    Inactive = 0 UMETA(DisplayName = "Inactive"),
    Hosting UMETA(DisplayName = "Hosting"),
    Discovering UMETA(DisplayName = "Discovering")
};

/**
 * @brief 局域网 UDP 目标端点参数值对象
 */
USTRUCT(BlueprintType)
struct GAMENETSESSION_API FGameNetLanEndpoint {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString IpAddress{TEXT("255.255.255.255")};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 Port{15777};
};

/**
 * @brief 通用联机房间元数据描述符（同时承载 LAN UDP 发现房间与 OnlineSubsystem 搜索结果）
 */
USTRUCT(BlueprintType)
struct GAMENETSESSION_API FGameNetRoomDescriptor {
    GENERATED_BODY()

public:
    /**
     * @brief 相等性比较运算符
     * @param Other 待比较的另一房间描述符
     * @return 所有字段均相等返回 true
     */
    [[nodiscard]] bool operator==(const FGameNetRoomDescriptor& Other) const noexcept = default;

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString RoomId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString RoomName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString MapPath;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString HostAddress;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 HostGamePort{7777};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 CurrentPlayers{1};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 MaxPlayers{4};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 PingMilliseconds{0};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    bool bIsLanRoom{true};
};

/**
 * @brief 创建联机或局域网房间的配置请求结构
 */
USTRUCT(BlueprintType)
struct GAMENETSESSION_API FGameNetCreateRoomRequest {
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString RoomName{TEXT("DefaultRoom")};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    FString MapPath{TEXT("/Engine/Maps/Entry")};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 HostGamePort{7777};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    int32 MaxPlayers{4};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    bool bIsLanMatch{true};

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameNet|Session")
    bool bAllowJoinInProgress{true};
};

