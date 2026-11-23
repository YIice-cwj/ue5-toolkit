/**
 * @file DataErrorCodes.h
 * @brief GameData 数据与持久化模块强类型错误码常量契约定义（配合 Game::ModuleId::Data 使用）
 */

#pragma once

#include "CoreMinimal.h"

namespace Game::DataErrorCodes {

/**
 * @brief 静态数据底层文件读写或二进制流截断失败
 */
inline constexpr int32 BinIoFailure = 10001;

/**
 * @brief 静态数据二进制文件头 Magic 魔数不匹配
 */
inline constexpr int32 BinMagicMismatch = 10002;

/**
 * @brief 静态数据二进制格式版本不受当前运行库支持
 */
inline constexpr int32 BinUnsupportedVersion = 10003;

/**
 * @brief 静态数据二进制负载 CRC32 校验和不匹配
 */
inline constexpr int32 BinChecksumMismatch = 10004;

/**
 * @brief 静态数据仓库中未找到指定结构的数据表
 */
inline constexpr int32 BinTableNotFound = 10005;

/**
 * @brief 静态数据单行二进制负载反序列化失败
 */
inline constexpr int32 BinRowDeserializeFailed = 10006;

/**
 * @brief 对应行结构尚未注册生成式负载读写器
 */
inline constexpr int32 BinSerializerNotFound = 10007;

/**
 * @brief 静态数据 JSON 源文件不存在
 */
inline constexpr int32 JsonFileNotFound = 10008;

/**
 * @brief 静态数据 JSON 源文件读取失败
 */
inline constexpr int32 JsonReadFailed = 10009;

/**
 * @brief 静态数据 JSON 文本格式解析失败
 */
inline constexpr int32 JsonParseFailed = 10010;

/**
 * @brief 静态数据 JSON 存在非法行、缺键、重键或字段反序列化错误
 */
inline constexpr int32 JsonRowValidationFailed = 10011;

/**
 * @brief 静态数据快照已有重载流程进行中（门闩重入拒绝）
 */
inline constexpr int32 ReloadInProgress = 10012;

/**
 * @brief 静态数据 .bin 与 JSON 通道暂存加载均失败
 */
inline constexpr int32 StagingLoadFailed = 10013;

/**
 * @brief 非编辑器构建禁止执行静态数据二进制导出
 */
inline constexpr int32 ExportDisabledInNonEditor = 10014;

/**
 * @brief 存档槽位名称为空、超长、含非法路径字符或不在白名单中
 */
inline constexpr int32 InvalidSlotName = 10020;

/**
 * @brief 待保存或序列化的 USaveGame 对象指针为空或无效
 */
inline constexpr int32 InvalidSaveObject = 10021;

/**
 * @brief 目标存档类型为空或为抽象类
 */
inline constexpr int32 InvalidSaveClass = 10022;

/**
 * @brief 存档对象序列化至内存字节流失败
 */
inline constexpr int32 SaveSerializeFailed = 10023;

/**
 * @brief 存档二进制字节流反序列化为 USaveGame 失败
 */
inline constexpr int32 SaveDeserializeFailed = 10024;

/**
 * @brief 加载得到的存档对象类型与期望类型不兼容
 */
inline constexpr int32 SaveTypeMismatch = 10025;

/**
 * @brief 存档归档格式主版本高于当前运行库支持的版本
 */
inline constexpr int32 ArchiveVersionMismatch = 10026;

/**
 * @brief 目标存档槽位在磁盘上不存在
 */
inline constexpr int32 SaveSlotNotFound = 10027;

/**
 * @brief 删除目标存档槽位文件失败
 */
inline constexpr int32 DeleteSlotFailed = 10028;

/**
 * @brief 写入存档临时文件 (.sav.tmp) 失败
 */
inline constexpr int32 WriteTempFileFailed = 10029;

/**
 * @brief 将存档临时文件原子换入正式路径失败
 */
inline constexpr int32 CommitTempFileFailed = 10030;

/**
 * @brief 存档文件头部或内容损坏（校验未通过）
 */
inline constexpr int32 CorruptedSaveFile = 10031;

/**
 * @brief 自临时文件抢救恢复至正式存档路径失败
 */
inline constexpr int32 RecoveryMoveFailed = 10032;

/**
 * @brief 待注册的存档段契约无效（段名为空、版本号非法或委托缺失）
 */
inline constexpr int32 InvalidSegment = 10033;

/**
 * @brief 目标名称的存档段尚未注册
 */
inline constexpr int32 SegmentNotFound = 10034;

/**
 * @brief 存档段序列化写入归档失败
 */
inline constexpr int32 SegmentSaveFailed = 10035;

/**
 * @brief 落盘存档段版本高于当前代码支持的段版本
 */
inline constexpr int32 SegmentVersionMismatch = 10036;

/**
 * @brief 存档段反序列化或迁移加载失败
 */
inline constexpr int32 SegmentLoadFailed = 10037;

}  // namespace Game::DataErrorCodes
