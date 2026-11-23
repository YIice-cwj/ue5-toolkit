// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file NetErrorCodes.h
 * @brief GameNet 网络套件统一强类型错误码常量契约（配合 Game::ModuleId::Net 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::NetErrorCodes {

/**
 * @brief 目标连接端点为空或格式非法
 */
inline constexpr int32 InvalidEndpoint = 2001;

/**
 * @brief 传输通道尚未处于已连接状态，无法发送数据或执行操作
 */
inline constexpr int32 NotConnected = 2002;

/**
 * @brief 报文头校验未通过（魔数不符、头部尺寸异常或负载超出最大上限）
 */
inline constexpr int32 InvalidPacketHeader = 2003;

/**
 * @brief 结构体序列化或反序列化失败（空指针、语法畸形、截断或尾部脏字节）
 */
inline constexpr int32 SerializationFailed = 2004;

/**
 * @brief 当前未装配有效的网络负载序列化器实例
 */
inline constexpr int32 InvalidSerializer = 2005;

/**
 * @brief 传输通道已处于连接中或已连接状态，拒绝重复发起连接
 */
inline constexpr int32 AlreadyConnected = 2006;

/**
 * @brief 目标端点或传输桩拒绝建立连接
 */
inline constexpr int32 ConnectionRefused = 2007;

/**
 * @brief 底层传输实例创建失败或平台传输驱动不可用
 */
inline constexpr int32 TransportUnavailable = 2008;

/**
 * @brief 异步网络请求已被外部取消令牌或显式取消接口中止
 */
inline constexpr int32 RequestCancelled = 2009;

/**
 * @brief 联机地图跳转参数非法（空世界、空实例、非法端口或疑似 URL 选项注入）
 */
inline constexpr int32 TravelInvalidArgument = 2010;

/**
 * @brief 底层 ServerTravel 或 ClientTravel 执行失败（缺少本地 PlayerController 或引擎拒绝）
 */
inline constexpr int32 TravelExecutionFailed = 2011;

}  // namespace Game::NetErrorCodes
