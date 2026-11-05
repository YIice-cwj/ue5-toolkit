/**
 * @file OnlineSessionRoomAdapter.h
 * @brief 封装引擎 OnlineSubsystem 平台房间创建、检索、加入、销毁与结果标准化的异步适配器
 */

#pragma once

#include "CoreMinimal.h"
#include "Types/GameNetSessionTypes.h"

class FOnlineSessionSearch;
class UWorld;

DECLARE_MULTICAST_DELEGATE_ThreeParams(
    FOnOnlineRoomAdapterOpCompleted,
    FName,
    bool,
    const FString&
);

/**
 * @brief OnlineSubsystem 房间会话异步生命周期与结果标准化适配器（无 OSS 环境下安全静默降级）
 */
class GAMENETSESSION_API FOnlineSessionRoomAdapter final {
public:
    static const FName DEFAULT_SESSION_NAME;
    static const FName SETTING_KEY_ROOM_NAME;
    static const FName SETTING_KEY_MAP_PATH;
    static const FName SETTING_KEY_GAME_PORT;
    static const FName OP_CREATE_ROOM;
    static const FName OP_FIND_ROOMS;
    static const FName OP_JOIN_ROOM;
    static const FName OP_DESTROY_ROOM;

public:
    FOnlineSessionRoomAdapter() = default;

    /**
     * @brief 析构时自动解绑全部 OnlineSession 异步回调句柄并清空搜索缓存
     */
    ~FOnlineSessionRoomAdapter();

    FOnlineSessionRoomAdapter(const FOnlineSessionRoomAdapter&) = delete;
    FOnlineSessionRoomAdapter& operator=(const FOnlineSessionRoomAdapter&) = delete;
    FOnlineSessionRoomAdapter(FOnlineSessionRoomAdapter&&) = delete;
    FOnlineSessionRoomAdapter& operator=(FOnlineSessionRoomAdapter&&) = delete;

    /**
     * @brief 发起异步创建平台在线房间请求
     * @param WorldContext 当前世界上下文指针（用于编辑器多实例隔离解析 OSS）
     * @param Request 房间创建描述参数
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步流程成功发起返回 true，参数非法或未启用 OSS 返回 false
     */
    bool CreateRoom(const UWorld* WorldContext, const FGameNetCreateRoomRequest& Request, int32 LocalUserNum = 0);

    /**
     * @brief 发起异步搜索平台在线房间列表请求
     * @param WorldContext 当前世界上下文指针
     * @param bLanQuery 是否仅搜索局域网会话
     * @param MaxResults 最大返回房间条数
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步流程成功发起返回 true
     */
    bool FindRooms(
        const UWorld* WorldContext,
        bool bLanQuery = false,
        int32 MaxResults = 50,
        int32 LocalUserNum = 0
    );

    /**
     * @brief 按最近一次搜索结果数组下标发起异步加入指定房间请求
     * @param WorldContext 当前世界上下文指针
     * @param SearchResultIndex 搜索结果下标
     * @param LocalUserNum 本地玩家控制器索引
     * @return 异步流程成功发起返回 true
     */
    bool JoinRoomByIndex(const UWorld* WorldContext, int32 SearchResultIndex, int32 LocalUserNum = 0);

    /**
     * @brief 发起异步销毁当前命名平台会话请求
     * @param WorldContext 当前世界上下文指针
     * @return 异步流程成功发起返回 true
     */
    bool DestroyRoom(const UWorld* WorldContext);

    /**
     * @brief 解绑所有未决平台会话回调句柄并重置搜索结果缓存
     * @param WorldContext 当前世界上下文指针
     */
    void Shutdown(const UWorld* WorldContext);

    /**
     * @brief 获取最近一次搜索完成并标准化转换后的房间描述符快照
     * @return 房间描述符数组
     */
    [[nodiscard]] const TArray<FGameNetRoomDescriptor>& GetCachedRooms() const noexcept {
        return CachedRooms;
    }

    /**
     * @brief 获取平台异步房间操作完成通知委托
     * @return 多播委托引用
     */
    FOnOnlineRoomAdapterOpCompleted& OnOperationCompleted() noexcept {
        return OperationCompletedDelegate;
    }

private:
    /**
     * @brief 处理平台创建房间异步完成回调
     * @param WorldContext 世界上下文弱指针
     * @param SessionName 会话名
     * @param bWasSuccessful 是否成功
     */
    void HandleCreateComplete(TWeakObjectPtr<const UWorld> WorldContext, FName SessionName, bool bWasSuccessful);

    /**
     * @brief 处理平台搜索房间异步完成回调并构建标准化描述符缓存
     * @param WorldContext 世界上下文弱指针
     * @param bWasSuccessful 是否成功
     */
    void HandleFindComplete(TWeakObjectPtr<const UWorld> WorldContext, bool bWasSuccessful);

    /**
     * @brief 处理平台加入房间异步完成回调并提取连接地址串
     * @param WorldContext 世界上下文弱指针
     * @param SessionName 会话名
     * @param bSucceeded 是否成功加入
     */
    void HandleJoinComplete(TWeakObjectPtr<const UWorld> WorldContext, FName SessionName, bool bSucceeded);

    /**
     * @brief 处理平台销毁房间异步完成回调
     * @param WorldContext 世界上下文弱指针
     * @param SessionName 会话名
     * @param bWasSuccessful 是否成功
     */
    void HandleDestroyComplete(TWeakObjectPtr<const UWorld> WorldContext, FName SessionName, bool bWasSuccessful);

private:
    TSharedPtr<FOnlineSessionSearch> ActiveSessionSearch;
    TArray<FGameNetRoomDescriptor> CachedRooms;
    FDelegateHandle CreateDelegateHandle;
    FDelegateHandle FindDelegateHandle;
    FDelegateHandle JoinDelegateHandle;
    FDelegateHandle DestroyDelegateHandle;
    FOnOnlineRoomAdapterOpCompleted OperationCompletedDelegate;
};

