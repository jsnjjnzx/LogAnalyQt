#include "ProtocolDetector.h"
#include "3762_prot/Prot3762Parser.h"
#include "645_prot/Prot645Parser.h"
#include "698_prot/Prot698Parser.h"
#include "app_msg/AppMsgParser.h"
#include "csg_upstream/CsgUpstreamParser.h"
#include "iec_101/Prot101Parser.h"
#include "iec_104/Prot104Parser.h"
#include "module_msg/ModuleMsgParser.h"
#include <QString>

// 内部辅助函数：从给定offset开始检测协议类型
// 这个函数包含原始的协议检测逻辑，不包含滑动窗口
static ProtocolType detectFromOffset(const QByteArray &data) {
  // 验证空数据，返回 Unknown
  if (data.isEmpty()) {
    return ProtocolType::Unknown;
  }

  quint8 startByte = static_cast<quint8>(data[0]);

  // ========== 优先级1: IEC 101 检测（特征最明显） ==========
  // IEC 101 单字符确认帧：0xE5（特殊情况，只有1字节）
  if (startByte == 0xE5 && data.size() == 1) {
    return ProtocolType::Protocol101;
  }

  // 验证长度小于 2 字节（排除单字符 0xE5 后），返回 Unknown
  if (data.size() < 2) {
    return ProtocolType::Unknown;
  }

  // IEC 101 固定帧或可变帧：起始字节 0x10 或 0x68
  // 调用 IEC 101 解析器的严格校验方法
  if ((startByte == 0x10 || startByte == 0x68) &&
      Prot101Parser::isValidFrame(data)) {
    return ProtocolType::Protocol101;
  }

  // ========== 优先级2: DL/T 698 检测（严格模式：HCS + FCS） ==========
  // 严格模式：必须验证 HCS 和 FCS 都通过
  // 注意：698 检测应该在 104 之前，因为 698 有更严格的校验（HCS + FCS +
  // 结束字节） 调用 698 解析器的静态方法进行严格校验
  if (startByte == 0x68 && Prot698Parser::isValidFrame(data)) {
    return ProtocolType::Protocol698;
  }

  // ========== 优先级3: ModuleMsg 检测（严格模式：只验证 FCS，无 HCS）
  // ==========
  // 注意：ModuleMsg 在 DL/T 698 检测失败后尝试
  // 关键区别：ModuleMsg 无 HCS，只有 FCS；控制域为 2 字节
  // 调用 ModuleMsg 解析器的严格校验方法
  if (startByte == 0x68 && ModuleMsg::ModuleMsgParser::isValidFrame(data)) {
    return ProtocolType::ProtocolModuleMsg;
  }

  // ========== 优先级4: Q/GDW 376.2 检测（严格模式：校验和验证） ==========
  // 调用 Q/GDW 376.2 解析器的严格校验方法
  if (startByte == 0x68 && Prot3762Parser::isValidFrame(data)) {
    return ProtocolType::Protocol3762;
  }

  // ========== 优先级5: DL/T 645 检测 (A0..A5 68H) ==========
  // 调用 DL/T 645 解析器的严格校验方法
  if (startByte == 0x68 && Prot645Parser::isValidFrame(data)) {
    return ProtocolType::Protocol645;
  }

  // ========== 优先级6: 南网计量上行规约检测（严格校验） ==========
  if ((startByte == 0x68 || startByte == 0x88) &&
      CsgUpstreamParser::isValidFrame(data)) {
    return ProtocolType::ProtocolCsgUpstream;
  }

  // ========== 优先级7: IEC 104 检测（基于长度字段精确匹配） ==========
  // 注意：IEC 104 校验较弱（无帧级校验和），仅检查长度和起始字符
  // 因此必须放在所有具有严格校验（CRC/Checksum）的协议之后，防止误判
  // 调用 IEC 104 解析器的严格校验方法
  if (startByte == 0x68 && Prot104Parser::isValidFrame(data)) {
    return ProtocolType::Protocol104;
  }

  // ========== 优先级8: AppMsg 检测 ==========
  // AppMsg 起始字节为 0x00 或 0x01
  // 调用 AppMsg 解析器的校验方法
  if (AppMsg::AppMsgParser::isValidFrame(data)) {
    return ProtocolType::ProtocolAppMsg;
  }

  // ========== 所有协议侦测均失败，返回 Unknown ==========
  return ProtocolType::Unknown;
}

/**
 * @brief 自动检测数据包的协议类型（支持滑动窗口）
 *
 * 该方法实现了滑动窗口机制，能够跳过前置无效字节，找到有效的协议帧。
 * 当直接从offset=0检测失败时，会尝试从后续字节开始重新检测。
 *
 * @param data 待检测的原始数据
 * @param frameOffset 输出参数，返回有效帧在data中的起始偏移量
 * @return 检测到的协议类型
 */
ProtocolType ProtocolDetector::detectProtocol(const QByteArray &data,
                                              int *frameOffset) {
  // 验证空数据，返回 Unknown
  if (data.isEmpty()) {
    if (frameOffset)
      *frameOffset = 0;
    return ProtocolType::Unknown;
  }

  // 首先尝试 offset=0，包括 AppMsg 检测
  ProtocolType result = detectFromOffset(data);
  if (result != ProtocolType::Unknown) {
    if (frameOffset)
      *frameOffset = 0;
    return result;
  }

  // 滑动窗口：跳过前置无效字节，尝试找到有效帧
  // 注意：AppMsg 只在 offset=0 时检测，滑动窗口不再检测 AppMsg
  // 因为 AppMsg 没有明确的起始标志，滑动窗口只会增加误判
  const int MAX_SKIP_BYTES = 16;
  int maxOffset = qMin(data.size() - 1, MAX_SKIP_BYTES);

  for (int offset = 1; offset <= maxOffset; ++offset) {
    QByteArray subData = data.mid(offset);

    // 滑动窗口时只检测有明确起始标志的协议（0x68, 0x10, 0xE5）
    // 不检测 AppMsg，避免误判
    quint8 startByte = static_cast<quint8>(subData[0]);
    if (startByte != 0x68 && startByte != 0x88 &&
        startByte != 0x10 && startByte != 0xE5) {
      continue; // 跳过没有明确起始标志的位置
    }

    result = detectFromOffset(subData);

    if (result != ProtocolType::Unknown) {
      if (frameOffset)
        *frameOffset = offset;
      return result;
    }
  }

  // 所有offset都无法识别有效帧，返回 Unknown
  if (frameOffset)
    *frameOffset = 0;
  return ProtocolType::Unknown;
}

QString ProtocolDetector::getProtocolName(ProtocolType type) {
  switch (type) {
  case ProtocolType::Auto:
    return "自动检测";
  case ProtocolType::Protocol698:
    return "DL/T 698";
  case ProtocolType::Protocol101:
    return "IEC 101";
  case ProtocolType::Protocol104:
    return "IEC 104";
  case ProtocolType::ProtocolAppMsg:
    return "应用层消息";
  case ProtocolType::ProtocolModuleMsg:
    return "模块消息";
  case ProtocolType::Protocol3762:
    return "Q/GDW 376.2";
  case ProtocolType::Protocol645:
    return "DL/T 645";
  case ProtocolType::ProtocolCsgUpstream:
    return "南网计量上行规约";
  case ProtocolType::Unknown:
    return "未知协议";
  default:
    return "未知";
  }
}

/**
 * @brief 获取已识别帧的完整长度
 *
 * 根据协议类型和数据内容，计算并返回完整帧的字节长度。
 * 这个方法用于多帧解析场景，帮助确定每一帧的边界。
 *
 * @param data 包含完整帧的数据（从帧头开始）
 * @param type 已识别的协议类型
 * @return 帧的字节长度，如果无法确定则返回0
 */
int ProtocolDetector::getFrameLength(const QByteArray &data,
                                     ProtocolType type) {
  if (data.isEmpty()) {
    return 0;
  }

  switch (type) {
  case ProtocolType::Protocol698:
    // 698: 68 L(2) ... 16
    // L是长度域（不含起始和结束字节）
    // 完整帧长度 = L + 2 (68和16)
    if (data.size() >= 3) {
      quint16 rawLen = (quint8)data[1] | ((quint8)data[2] << 8);
      int lenVal = rawLen & 0x3FFF; // 长度域只有低14位有效
      return lenVal + 2;            // +2 for 68 and 16
    }
    break;

  case ProtocolType::Protocol645:
    // 645: 68 A0..A5 68 C L ... CS 16
    // L字段在偏移量9
    if (data.size() >= 10) {
      int len = (quint8)data[9]; // L字段位置
      return 12 + len;           // 68+6addr+68+C+L+data+CS+16
    }
    break;

  case ProtocolType::ProtocolCsgUpstream:
    return CsgUpstreamParser::getFrameLength(data);

  case ProtocolType::Protocol3762:
    // 3762: 68 L(2) ... CS 16
    // L是总帧长度（包含68和16）
    if (data.size() >= 3) {
      quint16 len = (quint8)data[1] | ((quint8)data[2] << 8);
      return len; // L已包含68和16
    }
    break;

  case ProtocolType::Protocol101:
    // IEC 101有多种帧格式
    if (data.size() >= 1) {
      quint8 start = (quint8)data[0];
      if (start == 0xE5) {
        // 单字符确认帧
        return 1;
      } else if (start == 0x10 && data.size() >= 5) {
        // 固定帧: 10 C A CS 16 (5字节)
        return 5;
      } else if (start == 0x68 && data.size() >= 4) {
        // 可变帧: 68 L L 68 ...
        quint8 len = (quint8)data[1];
        return 4 + len + 2; // 68 L L 68 + 用户数据 + CS 16
      }
    }
    break;

  case ProtocolType::Protocol104:
    // 104: 68 L(1) ... (无校验和，无结束符)
    // L是APDU长度（不包含68和L本身）
    if (data.size() >= 2) {
      quint8 len = (quint8)data[1];
      return 2 + len; // 68 + L + APDU
    }
    break;

  case ProtocolType::ProtocolAppMsg:
    return AppMsg::AppMsgParser::getFullFrameSize(data);
    break;

  case ProtocolType::ProtocolModuleMsg:
    // ModuleMsg: 68(1) + L(2) + Control(2) + APDU(L) + FCS(2) + 16(1)
    // L 是 APDU 的长度，总帧长度 = L + 8
    if (data.size() >= 3) {
      quint16 rawLen = (quint8)data[1] | ((quint8)data[2] << 8);
      int lenVal = rawLen & 0x3FFF;
      return lenVal +
             8; // 1(68) + 2(L) + 2(Ctrl) + L(APDU) + 2(FCS) + 1(16) = L + 8
    }
    break;

  default:
    break;
  }

  return 0;
}
