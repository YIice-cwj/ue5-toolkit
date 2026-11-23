/**
 * @file GameNetSessionSubsystem.h
 * @brief 轻量级房间会话门面子系统（生命周期与状态机编排，委托局域网信标器、平台会话适配器与地图跳转工具库执行）
 */

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "LAN/LanBeaconBroadcaster.h"
#include "Online/OnlineSessionRoomAdapter.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/GameNetSessionTypes.h"

#include "GameNetSessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameNetSessionStateChanged, EGameNetSessionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameNetLanRoomDiscovered, const FGameNetRoomDescriptor&, Room);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameNetLanRoomExpired, const FString&, RoomId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnGameNetOnlineOpCompleted, FName, OperationName, bool, bSucceeded);

/**
 * @brief 房间会话生命周期常驻门面子系统（协调局域网信标广播器、OnlineSubsystem 适配器与 Travel 执行器）
 */
UCLASS(BlueprintType)
class GAMENETSESSION_API UGameNetSessionSubsystem : public UGameInstanceSubsystem {
    GENERATED_BODY()

public:
    UGameNetSessionSubsystem() = default;

    /**
     * @brief 安全获取房间会话子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与场景对象）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统实例指针，未就绪时返回 nullptr
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Session", meta = (WorldContext = "WorldContextObject"))
    static UGameNetSessionSubsystem* GetPtr(const UObject* WorldContextObject);

    /**
     * @brief 获取房间会话子系统引用（断言上下文有效）
     * @param WorldContextObject 世界或子系统上下文对象
     * @return 子系统引用
     */
    [[nodiscard]] static UGameNetSessionSubsystem& Get(const UObject* WorldContextObject);

    /**
     * @brief 子系统初始化：挂载 CoreTicker 帧轮询并绑定局域网信标与平台适配器回调
     * @param Collection 子系统依赖集合
     */
    void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统销毁：注销轮询、关闭平台会话适配器并停止局域网 UDP 信标
     */
    void Deinitialize() override;

    /**
     * @brief 启动脱离第三方平台 SDK 的局域网 UDP 房间广播信标
     * @param Request 房间创建描述参数
     * @param BeaconBindPort 局域网 UDP 信标监听端口（传 0 自动分配空闲端口）
     * @param BindIp 本地监听网卡 IP（默认 0.0.0.0）
     * @return 信标绑定启动成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool HostLanRoom(
        const FGameNetCreateRoomRequest& Request,
        int32 BeaconBindPort = 15777,
        const FString& BindIp = TEXT("0.0.0.0")
    );

    /**
     * @brief 原子校验并动态刷新当前局域网房主正在广播的房间元数据（失败时保留原状态）
     * @param UpdatedRoom 最新房间描述信息
     * @return 更新并编码合规返回 true，否则返回 false
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool UpdateLanHostedRoom(const FGameNetRoomDescriptor& UpdatedRoom);

    /**
     * @brief 由房主主动向目标局域网端点推送一次房间宣告数据报
     * @param TargetIp 目标广播或单播 IP（默认 255.255.255.255）
     * @param TargetPort 目标端口（默认 15777）
     * @return 宣告成功发出返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool BroadcastLanAnnouncement(const FString& TargetIp = TEXT("255.255.255.255"), int32 TargetPort = 15777);

    /**
     * @brief 启动局域网客户端房间搜索监听端口
     * @param ClientBindPort 本地 UDP 端口（默认 0 自动分配临时端口）
     * @param BindIp 本地监听网卡 IP（默认 0.0.0.0）
     * @return 搜索端口启动成功返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool StartLanDiscovery(int32 ClientBindPort = 0, const FString& BindIp = TEXT("0.0.0.0"));

    /**
     * @brief 发送一次局域网 UDP 房间发现探测包
     * @param TargetIp 目标广播或单播 IP（默认 255.255.255.255）
     * @param TargetPort 目标房主信标端口（默认 15777）
     * @return 探测包成功发出返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool SendLanDiscoveryProbe(const FString& TargetIp = TEXT("255.255.255.255"), int32 TargetPort = 15777);

    /**
     * @brief 停止当前局域网广播或发现会话并释放 UDP 端口
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    void StopLanSession();

    /**
     * @brief 获取当前局域网发现缓存中的活跃房间列表（蓝图按值返回版本）
     * @return 房间描述符数组
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Session")
    TArray<FGameNetRoomDescriptor> GetDiscoveredLanRooms() const;

    /**
     * @brief 获取当前局域网发现缓存中的活跃房间列表并就地写入外部复用数组（C++ 零额外堆分配）
     * @param OutRooms 接收结果的房间描述符数组
     */
    void GetDiscoveredLanRoomsTo(TArray<FGameNetRoomDescriptor>& OutRooms) const {
        LanBeacon.GetDiscoveredRooms(OutRooms);
    }

    /**
     * @brief 获取当前局域网信标实际绑定的本地 UDP 端口号
     * @return 已绑定端口号；未激活时返回 0
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Session")
    int32 GetLanBeaconBoundPort() const noexcept {
        return LanBeacon.GetBoundPort();
    }

    /**
     * @brief 事件驱动等待底层局域网 UDP 信标通道可读就绪（供确定性测试或同步探测工具使用）
     * @param MaxWaitSeconds 最大等待秒数上限
     * @return 超时前检测到可读数据报到达返回 true
     */
    [[nodiscard]] bool WaitForLanBeaconData(double MaxWaitSeconds) const {
        return LanBeacon.WaitForIncomingData(MaxWaitSeconds);
    }

    /**
     * @brief 委托平台会话适配器创建在线房间（未挂载 OSS 时安全返回 false，兼容同步回调状态机推进）
     * @param Request 房间创建描述参数
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步创建流程成功发起返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool CreateOnlineRoom(const FGameNetCreateRoomRequest& Request, int32 LocalUserNum = 0);

    /**
     * @brief 委托平台会话适配器检索在线房间列表
     * @param bLanQuery 是否仅搜索局域网会话
     * @param MaxResults 最大返回房间条数
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步搜索流程成功发起返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool FindOnlineRooms(bool bLanQuery = false, int32 MaxResults = 50, int32 LocalUserNum = 0);

    /**
     * @brief 按最近一次平台搜索结果下标发起异步加入指定房间
     * @param SearchResultIndex 搜索结果数组下标
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步加入流程成功发起返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool JoinOnlineRoomByIndex(int32 SearchResultIndex, int32 LocalUserNum = 0);

    /**
     * @brief 委托平台会话适配器销毁当前在线会话
     * @return 异步销毁流程成功发起返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool DestroyOnlineRoom();

    /**
     * @brief 获取最近一次平台会话搜索转换后的标准化房间描述符列表（蓝图按值返回版本）
     * @return 房间描述符数组
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Session")
    TArray<FGameNetRoomDescriptor> GetOnlineSearchResults() const;

    /**
     * @brief 获取最近一次平台会话搜索转换后的标准化房间描述符快照常引用（C++ 零拷贝）
     * @return 房间描述符数组常引用
     */
    [[nodiscard]] const TArray<FGameNetRoomDescriptor>& GetOnlineSearchResultsRef() const noexcept {
        return OnlineAdapter.GetCachedRooms();
    }

    /**
     * @brief 委托 UNetTravelLibrary 执行服务端/房主 ListenServer 地图切换
     * @param MapPath 目标地图资源路径
     * @param ListenPort 监听游戏端口号
     * @param ExtraOptions 额外 URL 选项
     * @return 世界上下文有效且触发 ServerTravel 返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool TravelToListenServer(
        const FString& MapPath,
        int32 ListenPort = 7777,
        const FString& ExtraOptions = TEXT("")
    );

    /**
     * @brief 委托 UNetTravelLibrary 驱动客户端向目标房间发起 ClientTravel 连接
     * @param Room 目标房间描述符
     * @param ExtraOptions 附加连接参数
     * @return 本地 PlayerController 有效且发起跳转返回 true
     */
    UFUNCTION(BlueprintCallable, Category = "GameNet|Session")
    bool TravelToRoom(const FGameNetRoomDescriptor& Room, const FString& ExtraOptions = TEXT(""));

    /**
     * @brief 查询当前会话状态机枚举
     * @return 当前会话状态
     */
    UFUNCTION(BlueprintPure, Category = "GameNet|Session")
    EGameNetSessionState GetSessionState() const noexcept {
        return SessionState;
    }

    /**
     * @brief 驱动局域网 UDP 信标收包与房间过期淘汰（运行时由 CoreTicker 自动调用）
     * @param DeltaTimeSeconds 步进秒数
     */
    void Tick(float DeltaTimeSeconds);

public:
    UPROPERTY(BlueprintAssignable, Category = "GameNet|Session")
    FOnGameNetSessionStateChanged OnSessionStateChanged;

    UPROPERTY(BlueprintAssignable, Category = "GameNet|Session")
    FOnGameNetLanRoomDiscovered OnLanRoomDiscovered;

    UPROPERTY(BlueprintAssignable, Category = "GameNet|Session")
    FOnGameNetLanRoomExpired OnLanRoomExpired;

    UPROPERTY(BlueprintAssignable, Category = "GameNet|Session")
    FOnGameNetOnlineOpCompleted OnOnlineOperationCompleted;

private:
    /**
     * @brief CoreTicker 周期帧回调桥接入口
     * @param DeltaTimeSeconds 帧间隔秒数
     * @return 始终返回 true 以保持常驻调度
     */
    bool HandleCoreTicker(float DeltaTimeSeconds);

    /**
     * @brief 变更会话状态并在状态实际变化时触发多播通知
     * @param NewState 目标状态
     */
    void SetSessionState(EGameNetSessionState NewState);

    /**
     * @brief 以零虚假广播方式执行平台在线异步过渡（同步拒绝静默回滚、异步成功单次广播、兼容同步回调终态推进）
     * @param PendingState 过渡态枚举
     * @param Action 底层适配器调用闭包
     * @return 操作发起成功返回 true
     */
    bool ExecuteOnlineTransition(EGameNetSessionState PendingState, TFunctionRef<bool()> Action);

    /**
     * @brief 转发局域网信标发现房间事件至蓝图多播委托
     * @param Room 发现或刷新的房间描述符
     */
    void HandleLanRoomDiscovered(const FGameNetRoomDescriptor& Room);

    /**
     * @brief 转发局域网信标房间超时过期事件至蓝图多播委托
     * @param RoomId 过期剔除的房间 ID
     */
    void HandleLanRoomExpired(const FString& RoomId);

    /**
     * @brief 响应平台会话适配器异步操作完成，推进会话状态机并按需触发客户端跳转
     * @param OperationName 操作名称
     * @param bSucceeded 是否执行成功
     * @param ResolvedConnectString 加入成功时解析出的连接地址
     */
    void HandleOnlineAdapterOperationCompleted(
        FName OperationName,
        bool bSucceeded,
        const FString& ResolvedConnectString
    );

private:
    FLanBeaconBroadcaster LanBeacon;
    FOnlineSessionRoomAdapter OnlineAdapter;
    EGameNetSessionState SessionState{EGameNetSessionState::Idle};
    FDelegateHandle LanDiscoveredHandle;
    FDelegateHandle LanExpiredHandle;
    FDelegateHandle OnlineOpCompletedHandle;
    FTSTicker::FDelegateHandle CoreTickerHandle;
};
