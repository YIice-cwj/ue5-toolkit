// Copyright Epic Games, Inc. All Rights Reserved.

#include "System/GameNetSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Logging/GameLogCategories.h"
#include "Travel/NetTravelLibrary.h"

/**
 * @brief 安全获取房间会话子系统指针（支持 UWorld、UGameInstance、UGameInstanceSubsystem 与场景对象）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统实例指针，未就绪时返回 nullptr
 */
UGameNetSessionSubsystem* UGameNetSessionSubsystem::GetPtr(const UObject* WorldContextObject) {
    if (WorldContextObject == nullptr) {
        return nullptr;
    }
    if (const UGameInstance* DirectInstance = Cast<UGameInstance>(WorldContextObject)) {
        return DirectInstance->GetSubsystem<UGameNetSessionSubsystem>();
    }
    if (const UGameInstanceSubsystem* SiblingSubsystem = Cast<UGameInstanceSubsystem>(WorldContextObject)) {
        if (const UGameInstance* OwningInstance = SiblingSubsystem->GetGameInstance()) {
            return OwningInstance->GetSubsystem<UGameNetSessionSubsystem>();
        }
        return nullptr;
    }
    if (GEngine != nullptr) {
        if (const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull)) {
            if (const UGameInstance* GI = World->GetGameInstance()) {
                return GI->GetSubsystem<UGameNetSessionSubsystem>();
            }
        }
    }
    const UWorld* FallbackWorld = WorldContextObject->GetWorld();
    if (FallbackWorld == nullptr || FallbackWorld->GetGameInstance() == nullptr) {
        return nullptr;
    }
    return FallbackWorld->GetGameInstance()->GetSubsystem<UGameNetSessionSubsystem>();
}

/**
 * @brief 获取房间会话子系统引用（断言上下文有效）
 * @param WorldContextObject 世界或子系统上下文对象
 * @return 子系统引用
 */
UGameNetSessionSubsystem& UGameNetSessionSubsystem::Get(const UObject* WorldContextObject) {
    UGameNetSessionSubsystem* Subsystem = GetPtr(WorldContextObject);
    check(Subsystem != nullptr);
    return *Subsystem;
}

/**
 * @brief 子系统初始化：挂载 CoreTicker 帧轮询并绑定局域网信标与平台适配器回调
 * @param Collection 子系统依赖集合
 */
void UGameNetSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection) {
    Super::Initialize(Collection);

    LanDiscoveredHandle = LanBeacon.OnRoomDiscovered().AddUObject(
        this,
        &UGameNetSessionSubsystem::HandleLanRoomDiscovered
    );
    LanExpiredHandle = LanBeacon.OnRoomExpired().AddUObject(
        this,
        &UGameNetSessionSubsystem::HandleLanRoomExpired
    );
    OnlineOpCompletedHandle = OnlineAdapter.OnOperationCompleted().AddUObject(
        this,
        &UGameNetSessionSubsystem::HandleOnlineAdapterOperationCompleted
    );
    CoreTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateUObject(this, &UGameNetSessionSubsystem::HandleCoreTicker)
    );
}

/**
 * @brief 子系统销毁：注销轮询、关闭平台会话适配器并停止局域网 UDP 信标
 */
void UGameNetSessionSubsystem::Deinitialize() {
    if (CoreTickerHandle.IsValid()) {
        FTSTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
        CoreTickerHandle.Reset();
    }

    if (OnlineOpCompletedHandle.IsValid()) {
        OnlineAdapter.OnOperationCompleted().Remove(OnlineOpCompletedHandle);
        OnlineOpCompletedHandle.Reset();
    }
    OnlineAdapter.Shutdown(GetWorld());

    if (LanDiscoveredHandle.IsValid()) {
        LanBeacon.OnRoomDiscovered().Remove(LanDiscoveredHandle);
        LanDiscoveredHandle.Reset();
    }
    if (LanExpiredHandle.IsValid()) {
        LanBeacon.OnRoomExpired().Remove(LanExpiredHandle);
        LanExpiredHandle.Reset();
    }
    LanBeacon.Stop();
    SessionState = EGameNetSessionState::Idle;

    Super::Deinitialize();
}

/**
 * @brief 启动脱离第三方平台 SDK 的局域网 UDP 房间广播信标
 * @param Request 房间创建描述参数
 * @param BeaconBindPort 局域网 UDP 信标监听端口
 * @param BindIp 本地监听网卡 IP
 * @return 信标绑定启动成功返回 true
 */
bool UGameNetSessionSubsystem::HostLanRoom(
    const FGameNetCreateRoomRequest& Request,
    int32 BeaconBindPort,
    const FString& BindIp
) {
    FGameNetRoomDescriptor Descriptor;
    Descriptor.RoomName = Request.RoomName;
    Descriptor.MapPath = Request.MapPath;
    Descriptor.HostGamePort = Request.HostGamePort;
    Descriptor.CurrentPlayers = 1;
    Descriptor.MaxPlayers = Request.MaxPlayers;
    Descriptor.bIsLanRoom = true;

    if (!LanBeacon.StartHosting(Descriptor, BeaconBindPort, BindIp)) {
        return false;
    }

    SetSessionState(EGameNetSessionState::Hosting);
    return true;
}

/**
 * @brief 原子校验并动态刷新当前局域网房主正在广播的房间元数据
 * @param UpdatedRoom 最新房间描述信息
 * @return 更新成功返回 true，非法或超限时保持原状态并返回 false
 */
bool UGameNetSessionSubsystem::UpdateLanHostedRoom(const FGameNetRoomDescriptor& UpdatedRoom) {
    return LanBeacon.UpdateHostedRoom(UpdatedRoom);
}

/**
 * @brief 由房主主动向目标局域网端点推送一次房间宣告数据报
 * @param TargetIp 目标广播或单播 IP
 * @param TargetPort 目标端口
 * @return 宣告成功发出返回 true
 */
bool UGameNetSessionSubsystem::BroadcastLanAnnouncement(const FString& TargetIp, int32 TargetPort) {
    FGameNetLanEndpoint Endpoint;
    Endpoint.IpAddress = TargetIp;
    Endpoint.Port = TargetPort;
    return LanBeacon.BroadcastAnnouncement(Endpoint);
}

/**
 * @brief 启动局域网客户端房间搜索监听端口
 * @param ClientBindPort 本地 UDP 端口
 * @param BindIp 本地监听网卡 IP
 * @return 搜索端口启动成功返回 true
 */
bool UGameNetSessionSubsystem::StartLanDiscovery(int32 ClientBindPort, const FString& BindIp) {
    if (!LanBeacon.StartDiscovery(ClientBindPort, BindIp)) {
        return false;
    }

    SetSessionState(EGameNetSessionState::Searching);
    return true;
}

/**
 * @brief 发送一次局域网 UDP 房间发现探测包
 * @param TargetIp 目标广播或单播 IP
 * @param TargetPort 目标房主信标端口
 * @return 探测包成功发出返回 true
 */
bool UGameNetSessionSubsystem::SendLanDiscoveryProbe(const FString& TargetIp, int32 TargetPort) {
    FGameNetLanEndpoint Endpoint;
    Endpoint.IpAddress = TargetIp;
    Endpoint.Port = TargetPort;
    return LanBeacon.SendDiscoveryProbe(Endpoint);
}

/**
 * @brief 停止当前局域网广播或发现会话并释放 UDP 端口
 */
void UGameNetSessionSubsystem::StopLanSession() {
    LanBeacon.Stop();
    if (SessionState == EGameNetSessionState::Hosting || SessionState == EGameNetSessionState::Searching) {
        SetSessionState(EGameNetSessionState::Idle);
    }
}

/**
 * @brief 获取当前局域网发现缓存中的活跃房间列表
 * @return 房间描述符数组
 */
TArray<FGameNetRoomDescriptor> UGameNetSessionSubsystem::GetDiscoveredLanRooms() const {
    TArray<FGameNetRoomDescriptor> Rooms;
    LanBeacon.GetDiscoveredRooms(Rooms);
    return Rooms;
}

/**
 * @brief 委托平台会话适配器创建在线房间
 * @param Request 房间创建描述参数
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步创建流程成功发起返回 true
 */
bool UGameNetSessionSubsystem::CreateOnlineRoom(const FGameNetCreateRoomRequest& Request, int32 LocalUserNum) {
    return ExecuteOnlineTransition(EGameNetSessionState::Creating, [this, &Request, LocalUserNum]() {
        return OnlineAdapter.CreateRoom(GetWorld(), Request, LocalUserNum);
    });
}

/**
 * @brief 委托平台会话适配器检索在线房间列表
 * @param bLanQuery 是否仅搜索局域网会话
 * @param MaxResults 最大返回房间条数
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步搜索流程成功发起返回 true
 */
bool UGameNetSessionSubsystem::FindOnlineRooms(bool bLanQuery, int32 MaxResults, int32 LocalUserNum) {
    return ExecuteOnlineTransition(EGameNetSessionState::Searching, [this, bLanQuery, MaxResults, LocalUserNum]() {
        return OnlineAdapter.FindRooms(GetWorld(), bLanQuery, MaxResults, LocalUserNum);
    });
}

/**
 * @brief 按最近一次平台搜索结果下标发起异步加入指定房间
 * @param SearchResultIndex 搜索结果数组下标
 * @param LocalUserNum 本地玩家控制器索引
 * @return 异步加入流程成功发起返回 true
 */
bool UGameNetSessionSubsystem::JoinOnlineRoomByIndex(int32 SearchResultIndex, int32 LocalUserNum) {
    return ExecuteOnlineTransition(EGameNetSessionState::Joining, [this, SearchResultIndex, LocalUserNum]() {
        return OnlineAdapter.JoinRoomByIndex(GetWorld(), SearchResultIndex, LocalUserNum);
    });
}

/**
 * @brief 委托平台会话适配器销毁当前在线会话
 * @return 异步销毁流程成功发起返回 true
 */
bool UGameNetSessionSubsystem::DestroyOnlineRoom() {
    return ExecuteOnlineTransition(EGameNetSessionState::Destroying, [this]() {
        return OnlineAdapter.DestroyRoom(GetWorld());
    });
}

/**
 * @brief 获取最近一次平台会话搜索转换后的标准化房间描述符列表
 * @return 房间描述符数组
 */
TArray<FGameNetRoomDescriptor> UGameNetSessionSubsystem::GetOnlineSearchResults() const {
    return OnlineAdapter.GetCachedRooms();
}

/**
 * @brief 委托 UNetTravelLibrary 执行服务端/房主 ListenServer 地图切换
 * @param MapPath 目标地图资源路径
 * @param ListenPort 监听游戏端口号
 * @param ExtraOptions 额外 URL 选项
 * @return 世界上下文有效且触发 ServerTravel 返回 true
 */
bool UGameNetSessionSubsystem::TravelToListenServer(
    const FString& MapPath,
    int32 ListenPort,
    const FString& ExtraOptions
) {
    return UNetTravelLibrary::ExecuteListenServerTravel(GetWorld(), MapPath, ListenPort, ExtraOptions).IsSuccess();
}

/**
 * @brief 委托 UNetTravelLibrary 驱动客户端向目标房间发起 ClientTravel 连接
 * @param Room 目标房间描述符
 * @param ExtraOptions 附加连接参数
 * @return 本地 PlayerController 有效且发起跳转返回 true
 */
bool UGameNetSessionSubsystem::TravelToRoom(const FGameNetRoomDescriptor& Room, const FString& ExtraOptions) {
    if (UNetTravelLibrary::ExecuteClientRoomTravel(GetWorld(), Room, ExtraOptions).IsFailure()) {
        return false;
    }

    SetSessionState(EGameNetSessionState::Joining);
    return true;
}

/**
 * @brief 驱动局域网 UDP 信标收包与房间过期淘汰（运行时由 CoreTicker 自动调用）
 * @param DeltaTimeSeconds 步进秒数
 */
void UGameNetSessionSubsystem::Tick(float DeltaTimeSeconds) {
    LanBeacon.Tick(DeltaTimeSeconds);
}

/**
 * @brief CoreTicker 周期帧回调桥接入口
 * @param DeltaTimeSeconds 帧间隔秒数
 * @return 始终返回 true 以保持常驻调度
 */
bool UGameNetSessionSubsystem::HandleCoreTicker(float DeltaTimeSeconds) {
    Tick(DeltaTimeSeconds);
    return true;
}

/**
 * @brief 变更会话状态并在状态实际变化时触发多播通知
 * @param NewState 目标状态
 */
void UGameNetSessionSubsystem::SetSessionState(EGameNetSessionState NewState) {
    if (SessionState == NewState) {
        return;
    }
    SessionState = NewState;
    OnSessionStateChanged.Broadcast(SessionState);
}

/**
 * @brief 以零虚假广播方式执行平台在线异步过渡（同步拒绝静默回滚、异步成功单次广播、兼容同步回调终态推进）
 * @param PendingState 过渡态枚举
 * @param Action 底层适配器调用闭包
 * @return 操作发起成功返回 true
 */
bool UGameNetSessionSubsystem::ExecuteOnlineTransition(
    EGameNetSessionState PendingState,
    TFunctionRef<bool()> Action
) {
    const EGameNetSessionState PreviousState = SessionState;
    SessionState = PendingState;
    if (!Action()) {
        if (SessionState == PendingState) {
            SessionState = PreviousState;
        }
        return false;
    }
    if (SessionState == PendingState && PreviousState != PendingState) {
        OnSessionStateChanged.Broadcast(SessionState);
    }
    return true;
}

/**
 * @brief 转发局域网信标发现房间事件至蓝图多播委托
 * @param Room 发现或刷新的房间描述符
 */
void UGameNetSessionSubsystem::HandleLanRoomDiscovered(const FGameNetRoomDescriptor& Room) {
    OnLanRoomDiscovered.Broadcast(Room);
}

/**
 * @brief 转发局域网信标房间超时过期事件至蓝图多播委托
 * @param RoomId 过期剔除的房间 ID
 */
void UGameNetSessionSubsystem::HandleLanRoomExpired(const FString& RoomId) {
    OnLanRoomExpired.Broadcast(RoomId);
}

/**
 * @brief 响应平台会话适配器异步操作完成，推进会话状态机并按需触发客户端跳转
 * @param OperationName 操作名称
 * @param bSucceeded 是否执行成功
 * @param ResolvedConnectString 加入成功时解析出的连接地址
 */
void UGameNetSessionSubsystem::HandleOnlineAdapterOperationCompleted(
    FName OperationName,
    bool bSucceeded,
    const FString& ResolvedConnectString
) {
    bool bEffectiveSuccess = bSucceeded;

    if (OperationName == FOnlineSessionRoomAdapter::OP_CREATE_ROOM) {
        SetSessionState(bSucceeded ? EGameNetSessionState::Hosting : EGameNetSessionState::Idle);
    } else if (OperationName == FOnlineSessionRoomAdapter::OP_JOIN_ROOM) {
        if (!bSucceeded) {
            SetSessionState(EGameNetSessionState::Idle);
        } else {
            const TGameStatusResult<void> TravelResult =
                UNetTravelLibrary::ExecuteConnectStringTravel(GetWorld(), ResolvedConnectString);
            if (TravelResult.IsFailure()) {
                GAME_LOG(
                    Warning,
                    TEXT("[Session] 加入在线房间成功但客户端跳转失败: %s"),
                    *TravelResult.GetError().ToString()
                );
                bEffectiveSuccess = false;
                SetSessionState(EGameNetSessionState::Idle);
            }
        }
    } else {
        SetSessionState(EGameNetSessionState::Idle);
    }

    OnOnlineOperationCompleted.Broadcast(OperationName, bEffectiveSuccess);
}
