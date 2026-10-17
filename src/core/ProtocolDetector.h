#ifndef PROTOCOLDETECTOR_H
#define PROTOCOLDETECTOR_H

#include <QByteArray>
#include <QString>

/**
 * @brief 协议类型枚举
 *
 * 定义系统支持的所有协议类型，包括自动检测模式
 */
enum class ProtocolType {
  Auto = 0,              // 自动检测
  Protocol698 = 1,       // DL/T 698.45
  Protocol101 = 2,       // IEC 60870-5-101
  Protocol104 = 3,       // IEC 60870-5-104
  ProtocolAppMsg = 4,    // 应用层消息
  ProtocolModuleMsg = 5, // 模块消息
  Protocol3762 = 6,      // Q/GDW 376.2
  Protocol645 = 7,       // DL/T 645-2007
  ProtocolCsgUpstream = 8, // 南网计量上行规约
  Unknown = -1           // 未知协议（自动检测失败）
};

/**
 * @brief 协议概要信息结构体
 * 用于全局预览列表显示
 */
struct ProtocolSummary {
  QString address = "-";
  QString direction = "-";
  QString frameSeq = "-";
  QString messageType = "已识别报文";
};

/**
 * @brief 协议自动检测引擎
 *
 * 提供基于帧头特征、长度字段、校验机制的智能协议识别功能
 *
 * 检测优先级（需求 6.1）：
 * 1. IEC 101 固定帧/可变帧/单字符（起始字节：0xE5, 0x10, 0x68）
 * 2. DL/T 698 帧头尾+HCS+FCS校验（起始字节：0x68，严格模式）
 * 3. ModuleMsg 只验证FCS（起始字节：0x68，严格模式，无HCS）
 * 4. Q/GDW 376.2 校验和验证（起始字节：0x68，严格模式）
 * 5. DL/T 645 校验和验证（起始字节：0x68，严格模式）
 * 6. 南网计量上行规约校验和验证（起始字节：0x68，严格模式）
 * 7. IEC 104 长度验证（起始字节：0x68，无校验和）
 * 8. AppMsg 起始字节验证（起始字节：0x00, 0x01）
 *
 * 0x68 起始字节的处理顺序（需求 6.5）：
 * IEC 101 可变帧 → DL/T 698（严格）→ ModuleMsg（严格）→ Q/GDW 376.2（严格）→
 * DL/T 645 → 南网计量上行规约（严格）→ IEC 104
 *
 * 优先级调整说明：
 * - DL/T 698 优先：最严格的校验（HCS + FCS）
 * - IEC 104 推后：因为 104 只有长度校验，没有帧级别的校验和/CRC。
 *   如果放在前面，很容易因为长度巧合而误判其他协议（如376.2, 645,
 * ModuleMsg）的帧。
 *   所有带有严格校验（Checksum/CRC）的协议都应该优先于只校验长度的协议。
 *
 * 严格模式说明：
 * - DL/T 698：必须验证 HCS 和 FCS 都通过
 * - ModuleMsg：必须验证 FCS 通过（无 HCS）
 * - Q/GDW 376.2：必须验证校验和通过
 * - 南网计量上行规约：必须验证双长度、校验和和结束字符
 */
class ProtocolDetector {
public:
  /**
   * @brief 自动检测数据包的协议类型
   * @param data 待检测的原始数据
   * @param frameOffset 输出参数，返回有效帧在data中的起始偏移量（可选）
   * @return 检测到的协议类型
   */
  static ProtocolType detectProtocol(const QByteArray &data,
                                     int *frameOffset = nullptr);

  /**
   * @brief 获取已识别帧的完整长度
   * @param data 包含完整帧的数据（从帧头开始）
   * @param type 已识别的协议类型
   * @return 帧的字节长度，如果无法确定则返回0
   */
  static int getFrameLength(const QByteArray &data, ProtocolType type);

  /**
   * @brief 获取协议类型的显示名称
   * @param type 协议类型枚举值
   * @return 协议的中文名称
   */
  static QString getProtocolName(ProtocolType type);
};

#endif // PROTOCOLDETECTOR_H
