/**
 * @file Prot3762Parser.cpp
 * @brief DL/T 376.2协议解析器实现
 *
 * 本文件实现了DL/T 376.2协议的完整解析功能。
 *
 * ============================================================================
 * 协议概述
 * ============================================================================
 * DL/T 376.2是中国电力行业标准，用于配电自动化系统的本地通信。
 *
 * 帧结构：
 * ┌────┬────┬────┬────────┬──────────────┬─────┬────┬──────┬────┬────┐
 * │68H │ L  │ C  │   R    │      A       │ AFN │ DT │ APDU │ CS │16H │
 * │(1) │(2) │(1) │  (6)   │(0或12+6*中继)│ (1) │(2) │(可变)│(1) │(1) │
 * └────┴────┴────┴────────┬──────────────┴─────┴────┴──────┴────┴────┘
 *
 * ============================================================================
 * 关键设计要点
 * ============================================================================
 *
 * 1. 信息域R（固定6字节）
 *    - 位于偏移量4-9
 *    - 包含中继级别、通信模块标识、路由标识、通信速率、报文序列号等
 *    - R[0]的D2位（通信模块标识）决定是否存在地址域A
 *    - R[0]的D7-D5位（中继级别）决定中继地址的数量
 *
 * 2. 地址域A（条件存在）
 *    - 当R[0]的D2位 == 1时：存在地址域
 *      长度 = 6 (源地址) + 6*中继级别 (中继地址) + 6 (目的地址) 字节
 *      当中继级别=0时，长度为12字节（无中继地址）
 *    - 当R[0]的D2位 == 0时：0字节（无地址域）
 *    - 这是协议的重要特性，使帧结构可以根据场景灵活调整
 *    - 地址域的条件存在会影响后续所有字段的偏移量计算
 *
 * 3. 校验和计算范围
 *    - 起始位置：偏移量3（从C字段开始，不包含68H和L）
 *    - 计算范围：C + R + A + AFN + DT + APDU
 *    - 计算方法：模256加法（uint8_t类型自动实现）
 *    - 结束位置：CS字段之前
 *
 * ============================================================================
 * 实现历史
 * ============================================================================
 * 本实现经过完全重构，修正了以下关键错误：
 * - 添加了信息域R的解析（原实现缺失）
 * - 实现了地址域A的条件解析（原实现假设固定长度）
 * - 修正了长度L的含义（总帧长，包含68H和16H）
 * - 修正了校验和计算范围（从C字段开始，而非从L字段开始）
 * - 移除了不存在的SEQ字段
 * - 添加了DT字段的解析
 *
 * @author Protocol Parser Team
 * @date 2024
 * @version 2.0 (完全重构版本)
 */

#include "Prot3762Parser.h"
#include "645_prot/Prot645Parser.h"
#include "698_prot/Prot698Parser.h"
#include "Prot3762ConfigManager.h"
#include "ProtocolDetector.h"
#include <QDebug>
#include <QMap>
#include <QStringBuilder>
#include <cstdint>


using uint8_t = quint8;
using uint16_t = quint16;

Prot3762Parser::Prot3762Parser() {}

// 静态方法：检查 Q/GDW 376.2 帧是否有效
bool Prot3762Parser::isValidFrame(const QByteArray &data) {
  // Minimal length: 68 L(2) C(1) R(6) AFN(1) DT(2) CS(1) 16(1) = 15 bytes
  if (data.size() < 15)
    return false;

  // 1. Start Byte 68H
  if ((uint8_t)data[0] != 0x68)
    return false;

  // 2. Length Field Check
  // 关键修改：在日志解析场景中，如果数据长度小于期望长度，说明帧不完整
  uint16_t len = (uint8_t)data[1] | ((uint8_t)data[2] << 8);
  if (data.size() < len) {
    return false;
  }

  // 3. End Byte - 重要修改：检查计算出的位置，而非数据末尾
  // 结束字节应该在 offset = len - 1 的位置
  if ((uint8_t)data[len - 1] != 0x16)
    return false;

  // 4. Checksum
  // CS 位置在 len - 2
  uint8_t cs = (uint8_t)data[len - 2];
  uint8_t calcCs = 0;
  // CS matches from Offset 3 (C) to len - 2
  // 376.2 Checksum: Sum of C, R, A, AFN, DT, APDU (User Data Area)
  for (int i = 3; i < len - 2; ++i) {
    calcCs += (uint8_t)data[i];
  }
  return cs == calcCs;
}

// 静态方法：获取协议概要信息
ProtocolSummary Prot3762Parser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;

  // LogBackend Logic:
  // Address: data.mid(4, 5).toHex().toUpper()
  // AFN: data[9]
  // SEQ: data[10]

  if (data.size() >= 12) {
    // C(1) R(6) -> R is 6 bytes.
    // R starts at Offset 3 (68 L L C R...).
    // Wait, Header: 68(0) L(1) L(2) C(3) R(4,5,6,7,8,9)?
    // LogBackend says: addrBytes = data.mid(4, 5).
    // If 68 L L C R...
    // 0  1 2 3 4
    // So LogBackend treats Offset 4 as start of Address?
    // In 376.2, R is "Information Field" (6 bytes). Starts at Offset 3+1 = 4.
    // So R = data.mid(4, 6).
    // LogBackend uses 5 bytes? Maybe excluding module ID?
    // Let's use 6 bytes for accuracy or follow LogBackend's visual preference.
    // Let's use 6 bytes as R consists of Route(5) + ModuleID(1) usually?
    // Or ModuleID + Route?
    // Standard:
    // R (6 bytes):
    // Byte 1: Module ID (or similar)
    // Byte 2-6: Route / Address?
    // Actually, 376.2 R field:
    // Byte 1: 终端类型/模块标识
    // Byte 2-6: 地址 (5 bytes)
    // So LogBackend taking 5 bytes at offset 4 might be taking Byte 1-5?
    // Wait, LogBackend: mid(4, 5) -> Offsets 4,5,6,7,8.
    // R ends at 3+6 = 9.
    // So it takes the first 5 bytes of R.

    QByteArray addrBytes = data.mid(4, 6); // Take full R (6 bytes)
    summary.address = addrBytes.toHex().toUpper();

    // Control Field C is at Offset 3.
    uint8_t C = (uint8_t)data[3];
    if (C & 0x80)
      summary.direction = "S->C(上行/从->主)";
    else
      summary.direction = "C->S(下行/主->从)";

    // AFN location?
    // 68(1) L(2) C(1) R(6) ...
    // 1+2+1+6 = 10.
    // Next is Address A (conditional).
    // LogBackend assumes A is not present? Or hardcoded for specific structure?
    // LogBackend logic: AFN at 9?
    // If 68(0) L(1,2) C(3) R(4..9) ... AFN should be at 10?
    // LogBackend: "uint8_t afn = data[9];"
    // If data[9] is the last byte of R?
    // Maybe LogBackend logic was slightly off or based on a specific truncated
    // frame? Let's verify standard 376.2 offset. 68(0) L1(1) L2(2) C(3) R1(4)
    // R2(5) R3(6) R4(7) R5(8) R6(9). So offset 9 is R6. Offset 10 is Start of A
    // (if exists) or AFN (if no A). Usually local comms 376.2 often no A. So
    // AFN at 10. LogBackend used 9? Let's CHECK LogBackend again. "uint8_t afn
    // = static_cast<uint8_t>(data[9]);" -> This reads R6. In 376.2, R6 often
    // contains Sequence or similar? No, R is Address stuff. Let's look at 376.2
    // Frame: Byte 10: AFN (if no A). Maybe LogBackend meant 10? The previously
    // read LogBackend code: "uint8_t afn = static_cast<uint8_t>(data[9]);"
    // "uint8_t seq = static_cast<uint8_t>(data[10]);"
    // This suggests LogBackend expects AFN at 9, SEQ at 10.
    // This matches a frame structure: 68 L L C R(5) ...?
    // No standard frame is like that.
    // I will assume LogBackend was buggy or targeting a specific non-standard
    // variant. I will implement Correct 376.2: AFN at 10. And use DT (11,12)
    // for more Info.

    // Frame Sequence: C (Offset 3) has FCB?
  }

  // To provide a summary, we need to parse enough to determine AFN and
  // potentially DT. This requires knowing if Address Field A is present. For a
  // quick summary, we can assume the most common case (no A field) or try to
  // determine it from R[0].

  // Minimal frame for AFN: 68 L L C R(6) AFN(1) = 1+2+1+6+1 = 11 bytes.
  // If data.size() < 11, we can't even get AFN.
  if (data.size() < 11) {
    return summary; // Return empty summary if too short
  }

  // Get R[0] to determine if Address Field A exists
  // R[0] is at offset 4 (68 L L C R[0])
  uint8_t r0 = (uint8_t)data[4];
  uint8_t module_flag = (r0 >> 2) & 0x01; // D2 bit of R[0]
  uint8_t relay_level = (r0 >> 5) & 0x07; // D7-D5 bits of R[0]

  int afnOffset = 10; // Default offset for AFN if no A field
  if (module_flag == 1) {
    // Address field A is present
    int addrLen = 12 + 6 * relay_level;
    afnOffset = 10 + addrLen; // AFN comes after R (6 bytes) + A (addrLen bytes)
  }

  if (data.size() > afnOffset) {
    uint8_t afn = (uint8_t)data[afnOffset];
    auto *def = Prot3762ConfigManager::instance().getAfnDef(afn);
    QString afnName = def ? def->name : "未知 AFN";
    summary.messageType = afnName;
  }

  // For frame sequence, use FCB/ACD from Control Field C
  uint8_t C = (uint8_t)data[3];
  summary.frameSeq = QString::number((C >> 5) & 0x01); // D5 bit of C

  return summary;
}

/**
 * @brief 计算校验和（模256和）
 *
 * 根据DL/T 376.2规范，校验和从控制域C开始计算（偏移量3），
 * 包括C、R、A、AFN、DT、APDU所有字节，使用模256加法。
 *
 * @param data 原始帧数据
 * @param start 起始偏移量（应为1，从L字段开始）
 * @param len 计算长度（从start到CS字段之前的字节数）
 * @return 计算得到的校验和（uint8_t自动实现模256）
 */
uint8_t Prot3762Parser::calculateCS(const QByteArray &data, int start,
                                    int len) {
  uint8_t sum = 0;
  for (int i = 0; i < len; ++i) {
    sum += (uint8_t)data[start + i];
  }
  return sum;
}

/**
 * @brief 解析DL/T 376.2协议帧
 *
 * 本函数实现了完整的DL/T 376.2协议帧解析，严格按照协议规范进行字段提取和验证。
 *
 * 帧结构（DL/T 376.2规范）：
 * ┌────┬────┬────┬────────┬──────────┬─────┬────┬──────┬────┬────┐
 * │68H │ L  │ C  │   R    │    A     │ AFN │ DT │ APDU │ CS │16H │
 * │(1) │(2) │(1) │  (6)   │ (0或12)  │ (1) │(2) │(可变)│(1) │(1) │
 * └────┴────┴────┴────────┴──────────┴─────┴────┴──────┴────┴────┘
 *
 * 关键要点：
 * 1. 长度L：表示总帧长度（包含68H和16H）
 * 2. 信息域R：固定6字节，包含中继级别、通信模块标识、路由标识等
 * 3. 地址域A：条件存在
 *    - 当R[0]的D2位（通信模块标识）== 1时：存在地址域
 *      长度 = 6 (源地址) + 6*中继级别 (中继地址) + 6 (目的地址) 字节
 *    - 当R[0]的D2位（通信模块标识）== 0时：0字节（无地址域）
 * 4. 校验和CS：从控制域C开始计算（偏移量3），包括C、R、A、AFN、DT、APDU
 *
 * @param data 原始帧数据（完整的字节数组）
 * @return 解析后的字段列表，如果帧无效则返回空列表
 */
QList<Prot3762Field> Prot3762Parser::parse(const QByteArray &data) {
  QList<Prot3762Field> fields;
  int offset = 0;

  // ========================================================================
  // 帧验证：最小长度检查
  // ========================================================================
  // DL/T 376.2协议的最小有效帧长度为15字节，包括：
  // 68H(1) + L(2) + C(1) + R(6) + AFN(1) + DT(2) + CS(1) + 16H(1) = 15字节
  //
  // 这是无地址域（module_flag=0）且无APDU数据的最小帧。
  // ========================================================================
  if (data.size() < 15) {
    return fields;
  }

  // ========================================================================
  // 1. 起始字符 68H（偏移量0）
  // ========================================================================
  // 所有有效的DL/T 376.2帧必须以0x68开始。
  // 如果起始字符不匹配，则认为这不是有效的376.2帧，返回空列表。
  // ========================================================================
  if ((uint8_t)data[offset] != 0x68)
    return fields;
  fields.append({"起始符", "68", "", offset++, 1, 0});

  // ========================================================================
  // 2. 长度字段 L（2字节，小端序，偏移量1-2）
  // ========================================================================
  // 长度字段L表示整个帧的总字节数（包含起始字符68H和结束字符16H）。
  //
  // 重要说明：
  // - L = 整个帧的字节数 = data.size()
  // - 采用小端序编码（低字节在前，高字节在后）
  // - 用户数据区长度 = L - 5（减去68H、L本身2字节、CS、16H）
  //
  // 示例：
  // 如果L = 0x0F 0x00（小端序），则L = 15字节
  // 表示整个帧长度为15字节（包含68H和16H）
  // ========================================================================
  uint16_t len = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  fields.append({"长度域 L", data.mid(offset, 2).toHex().toUpper(),
                 QString("总长度: %1 字节").arg(len), offset, 2, 0});
  offset += 2;

  // 验证长度字段
  if (len != data.size()) {
    // 长度不匹配，但继续解析以显示部分结果
    qDebug() << "Warning: Length field L =" << len
             << "but actual data size =" << data.size();
  }

  // ========================================================================
  // 3. 控制域 C（1字节，偏移量3）
  // ========================================================================
  // 控制域C包含帧的控制信息，采用位字段编码。
  //
  // 位字段定义：
  // D7: DIR（传输方向）
  //     0 = 下行（主站→从站）
  //     1 = 上行（从站→主站）
  // D6: PRM（启动标志）
  //     0 = 从站帧
  //     1 = 主站帧
  // D5: FCB/ACD
  //     主站帧：FCB（帧计数位）
  //     从站帧：ACD（要求访问位）
  // D4-D0: 功能码（5位，0~31）
  //     定义帧的具体功能（如确认、否认、数据传输等）
  // ========================================================================
  uint8_t c = (uint8_t)data[offset];
  uint8_t dir = (c >> 7) & 0x01;     // D7位：DIR（传输方向）
  uint8_t prm = (c >> 6) & 0x01;     // D6位：PRM（启动标志）
  uint8_t fcb_acd = (c >> 5) & 0x01; // D5位：FCB/ACD
  uint8_t func_code = c & 0x1F;      // D4-D0位：功能码（5位）

  QString cDesc = QString("DIR=%1(%2), PRM=%3(%4), FCB/ACD=%5, 功能码=%6")
                      .arg(dir)
                      .arg(dir ? "上行" : "下行")
                      .arg(prm)
                      .arg(prm ? "主站" : "从站")
                      .arg(fcb_acd)
                      .arg(func_code);
  fields.append({"控制域 C", QString("%1").arg(c, 2, 16, QChar('0')).toUpper(),
                 cDesc, offset, 1, 0});
  offset++;

  // ========================================================================
  // 4. 信息域 R（固定6字节，偏移量4-9）
  // ========================================================================
  // 信息域R是DL/T 376.2协议的关键字段，固定为6字节长度。
  //
  // 结构说明：
  // R[0]: 位字段
  //   - D7-D5: 中继级别（0~7）
  //   - D4: 冲突检测（下行）/ 保留（上行）
  //   - D3: 保留
  //   - D2: 通信模块标识（module_flag）- 决定是否存在地址域A
  //         0 = 无地址域A（长度0字节）
  //         1 = 有地址域A（长度12+6*中继级别字节）
  //   - D1-D0: 路由标识（下行）/ D0为路由标识（上行）
  // R[1]: 信道标识/纠错编码标识
  // R[2]: 预计应答字节数（下行）/ 实测相线标识（上行）
  // R[3-4]: 通信速率（2字节，小端序）
  //   - D15: 速率单位标识（0=bps, 1=kbps）
  //   - D14-D0: 通信速率值
  // R[5]: 报文序列号
  // ========================================================================

  // 边界检查：确保有足够的数据读取6字节
  if (offset + 6 > data.size()) {
    qDebug()
        << "Error: Not enough data for Info Field R. Need 6 bytes at offset"
        << offset;
    return fields;
  }

  QByteArray rData = data.mid(offset, 6);
  fields.append({"信息域 R", rData.toHex().toUpper(), "6 字节", offset, 6, 0});

  // 4.1 解析R[0]字节的位字段
  uint8_t r0 = (uint8_t)rData[0];
  uint8_t relay_level = (r0 >> 5) & 0x07; // D7-D5位：中继级别
  uint8_t module_flag =
      (r0 >> 2) & 0x01; // D2位：通信模块标识（修正：是D2位，不是D3位）
  uint8_t route_flag = r0 & 0x03; // D1-D0位：路由标识

  QString r0Desc = QString("中继级别=%1, 通信模块标识=%2, 路由标识=%3")
                       .arg(relay_level)
                       .arg(module_flag)
                       .arg(route_flag);
  fields.append({"R[0]", QString("%1").arg(r0, 2, 16, QChar('0')).toUpper(),
                 r0Desc, offset, 1, 1});

  // 4.2 解析R[1]字节：信道标识/纠错编码标识
  uint8_t r1 = (uint8_t)rData[1];
  QString r1Desc = QString("信道标识/纠错编码标识");
  fields.append({"R[1]", QString("%1").arg(r1, 2, 16, QChar('0')).toUpper(),
                 r1Desc, offset + 1, 1, 1});

  // 4.3 解析R[2]字节：预计应答字节数/实测相线标识
  uint8_t r2 = (uint8_t)rData[2];
  QString r2Desc = QString("预计应答字节数/实测相线标识");
  fields.append({"R[2]", QString("%1").arg(r2, 2, 16, QChar('0')).toUpper(),
                 r2Desc, offset + 2, 1, 1});

  // 4.4 解析R[3-4]字节：通信速率（2字节，小端序）
  uint16_t speed = (uint8_t)rData[3] | ((uint8_t)rData[4] << 8);
  uint8_t speed_unit = (speed >> 15) & 0x01; // D15位：速率单位标识
  uint16_t speed_value = speed & 0x7FFF;     // D14-D0位：通信速率值
  QString speedDesc = QString("通信速率=%1 %2")
                          .arg(speed_value)
                          .arg(speed_unit ? "kbps" : "bps");
  fields.append({"R[3-4]",
                 QString("%1 %2")
                     .arg(static_cast<uint8_t>(rData[3]), 2, 16, QChar('0'))
                     .arg(static_cast<uint8_t>(rData[4]), 2, 16, QChar('0'))
                     .toUpper(),
                 speedDesc, offset + 3, 2, 1});

  // 4.5 解析R[5]字节：报文序列号
  uint8_t r5 = (uint8_t)rData[5];
  QString r5Desc = QString("报文序列号=%1").arg(r5);
  fields.append({"R[5]", QString("%1").arg(r5, 2, 16, QChar('0')).toUpper(),
                 r5Desc, offset + 5, 1, 1});

  offset += 6;

  // ========================================================================
  // 5. 地址域 A（条件存在）
  // ========================================================================
  // 地址域A的存在和长度由信息域R中的通信模块标识位（module_flag）决定。
  //
  // 重要修正：通信模块标识是R[0]的D2位，不是D3位！
  //
  // 条件逻辑：
  // - 当module_flag == 0时：无地址域（长度0字节）
  //   适用于不需要地址信息的通信场景
  //
  // - 当module_flag == 1时：有地址域
  //   结构：源地址A1（6字节BCD码）+ 中继地址A2（6*中继级别字节）+
  //   目的地址A3（6字节BCD码） 长度 = 6 + 6*中继级别 + 6 = 12 + 6*中继级别 字节
  //   当中继级别=0时，长度为12字节（无中继地址）
  //
  // 注意：地址域的条件存在会影响后续所有字段的偏移量计算！
  // ========================================================================

  // 根据信息域R中的通信模块标识位(module_flag，R[0]的D2位)决定是否存在地址域
  int addrLen = 0;
  if (module_flag == 1) {
    // 当module_flag==1时，解析地址域
    // 地址域长度 = 6 (源地址) + 6*中继级别 (中继地址) + 6 (目的地址)
    addrLen = 12 + 6 * relay_level;

    // 边界检查：确保有足够的数据读取地址域
    if (offset + addrLen > data.size()) {
      qDebug() << "Error: Not enough data for Address Field A. Need" << addrLen
               << "bytes at offset" << offset;
      return fields;
    }

    QByteArray addrData = data.mid(offset, addrLen);
    QString addrDesc =
        QString("%1 字节 (源地址 6B + 中继地址 %2B + 目的地址 6B)")
            .arg(addrLen)
            .arg(6 * relay_level);
    fields.append(
        {"地址域 A", addrData.toHex().toUpper(), addrDesc, offset, addrLen, 0});

    // 提取前6字节作为源地址A1（BCD码）
    QByteArray srcAddr = addrData.mid(0, 6);
    // 地址在报文中是逆序存储的，需要反转
    QByteArray srcAddrReversed;
    for (int i = srcAddr.size() - 1; i >= 0; --i) {
      srcAddrReversed.append(srcAddr[i]);
    }
    QString srcAddrStr = srcAddrReversed.toHex().toUpper();
    fields.append({"源地址 A1", srcAddr.toHex().toUpper(),
                   QString("地址: %1").arg(srcAddrStr), offset, 6, 1});

    int currentOffset = 6;

    // 提取中继地址A2（如果存在）
    if (relay_level > 0) {
      for (int i = 0; i < relay_level; i++) {
        QByteArray relayAddr = addrData.mid(currentOffset, 6);
        // 地址在报文中是逆序存储的，需要反转
        QByteArray relayAddrReversed;
        for (int j = relayAddr.size() - 1; j >= 0; --j) {
          relayAddrReversed.append(relayAddr[j]);
        }
        QString relayAddrStr = relayAddrReversed.toHex().toUpper();
        fields.append({QString("中继地址 A2[%1]").arg(i),
                       relayAddr.toHex().toUpper(),
                       QString("地址: %1").arg(relayAddrStr),
                       offset + currentOffset, 6, 1});
        currentOffset += 6;
      }
    }

    // 提取最后6字节作为目的地址A3（BCD码）
    QByteArray destAddr = addrData.mid(currentOffset, 6);
    // 地址在报文中是逆序存储的，需要反转
    QByteArray destAddrReversed;
    for (int j = destAddr.size() - 1; j >= 0; --j) {
      destAddrReversed.append(destAddr[j]);
    }
    QString destAddrStr = destAddrReversed.toHex().toUpper();
    fields.append({"目的地址 A3", destAddr.toHex().toUpper(),
                   QString("地址: %1").arg(destAddrStr), offset + currentOffset,
                   6, 1});

    offset += addrLen;
  } else {
    // 当module_flag==0时，不解析地址域（长度为0）
    fields.append(
        {"地址域 A", "无", "无地址域 (通信模块标识=0)", offset, 0, 0});
  }

  // 6. AFN（应用功能码，1字节）
  if (offset >= data.size()) {
    qDebug() << "Error: Not enough data for AFN at offset" << offset;
    return fields;
  }
  uint8_t afn = (uint8_t)data[offset];
  QString afnName = getAfnName(afn);
  fields.append({"应用功能码 AFN",
                 QString("%1").arg(afn, 2, 16, QChar('0')).toUpper(), afnName,
                 offset, 1, 0});
  offset++;

  // ========================================================================
  // 7. DT字段（数据类型标识，2字节：DT1和DT2）
  // ========================================================================
  // DT字段标识数据类型，由DT1和DT2两个字节组成。
  // 注意：DL/T 376.2协议中不存在SEQ（序列号）字段，
  //      序列号信息已包含在信息域R[5]中。
  // ========================================================================

  // 边界检查：确保有足够的数据读取2字节
  if (offset + 2 > data.size()) {
    qDebug() << "Error: Not enough data for DT field. Need 2 bytes at offset"
             << offset;
    return fields;
  }

  uint8_t dt1 = (uint8_t)data[offset];
  uint8_t dt2 = (uint8_t)data[offset + 1];

  // 根据DIR位确定方向字符串
  QString direction = (dir == 1) ? "up" : "down";

  // 解析数据单元标识Fn
  // 根据DL/T 376.2协议：
  // - DT2表示信息类组（0-255）
  // - DT1采用位表示法，每一位对应该组的一个信息类型
  // - Fn = DT2 * 8 + bit_position (bit_position为DT1中置1的位位置，0-7)
  QString fnList;
  QList<int> activeFns; // 记录激活的Fn编号
  for (int bit = 0; bit < 8; bit++) {
    if (dt1 & (1 << bit)) {
      int fn = dt2 * 8 + bit + 1; // Fn从1开始编号
      activeFns.append(fn);
      if (!fnList.isEmpty())
        fnList += ", ";

      // 查询Fn名称（根据方向）
      auto *fnDef =
          Prot3762ConfigManager::instance().getFnDef(afn, fn, direction);
      if (fnDef) {
        fnList += QString("F%1(%2)").arg(fn).arg(fnDef->name);
      } else {
        fnList += QString("F%1").arg(fn);
      }
    }
  }

  if (fnList.isEmpty()) {
    fnList = "无";
  }

  QString dtDesc = QString("DT1=%1, DT2=%2, 数据单元: %3")
                       .arg(dt1, 2, 16, QChar('0'))
                       .arg(dt2, 2, 16, QChar('0'))
                       .arg(fnList);
  fields.append({"数据类型 DT", data.mid(offset, 2).toHex().toUpper(), dtDesc,
                 offset, 2, 0});
  offset += 2;

  // ========================================================================
  // 8. APDU应用数据（可变长度）
  // ========================================================================
  // APDU（Application Protocol Data Unit）是应用层数据，长度可变。
  //
  // 位置计算：
  // - 起始位置：DT字段之后（当前offset）
  // - 结束位置：CS字段之前
  // - CS位于倒数第2字节，16H位于最后1字节
  //
  // 长度计算：
  // APDU长度 = CS偏移量 - 当前偏移量
  //          = (data.size() - 2) - offset
  //
  // 特殊情况：
  // - 如果APDU长度 > 0：提取并显示APDU数据
  // - 如果APDU长度 = 0：无应用数据，不添加字段
  // - 如果APDU长度 < 0：数据结构错误，返回错误
  // ========================================================================

  // APDU起始位置 = 当前offset（DT字段之后）
  // 注意：此时offset已经指向DT字段之后的位置

  // CS位于倒数第2字节，16H位于最后1字节
  // 边界检查：确保至少有CS和16H两个字节
  if (data.size() < offset + 2) {
    qDebug() << "Error: Not enough data for CS and Frame End. Need at least 2 "
                "bytes after offset"
             << offset;
    return fields;
  }

  // 计算CS的偏移量（倒数第2字节）
  int csOffset = data.size() - 2;

  // 计算APDU长度 = CS偏移量 - 当前偏移量
  int apduLen = csOffset - offset;

  // 如果APDU长度大于0，则提取并显示APDU数据
  if (apduLen > 0) {
    QByteArray apduData = data.mid(offset, apduLen);
    fields.append({"应用数据 APDU", apduData.toHex().toUpper(),
                   QString("%1 字节").arg(apduLen), offset, apduLen, 0});

    // 尝试根据Fn定义解析APDU字段
    int apduOffset = 0;
    for (int fn : activeFns) {
      auto *fnDef =
          Prot3762ConfigManager::instance().getFnDef(afn, fn, direction);
      if (fnDef && fnDef->type == "Struct" && !fnDef->fields.isEmpty()) {
        // 添加Fn标题
        fields.append({QString("数据单元 F%1").arg(fn), "", fnDef->name,
                       offset + apduOffset, 0, 1});

        // 用于记录字段ID和其解析后的值（用于嵌套协议解析）
        QMap<QString, int> fieldIdValues;

        // 解析每个字段
        for (const auto &fieldDef : fnDef->fields) {
          // 检查 countRef：如果引用的数量字段值为 0，则跳过此字段
          if (!fieldDef.countRef.isEmpty()) {
            int countValue = fieldIdValues.value(fieldDef.countRef, -1);
            if (countValue == 0) {
              // 数量为0，跳过此字段（不解析）
              continue;
            }
            // TODO: 如果 countValue > 1，应该循环解析多次。目前仅支持 0/1
          }

          if (fieldDef.type == "None") {
            // 无数据字段
            fields.append({fieldDef.name, "无", fieldDef.description,
                           offset + apduOffset, 0, 2});
            continue;
          }

          // 处理可变长度字段（length=-1表示读取剩余所有数据）
          int actualLength = fieldDef.length;
          if (fieldDef.length == -1) {
            actualLength = apduLen - apduOffset; // 剩余所有数据
          }

          if (apduOffset + actualLength > apduLen) {
            // 数据不足
            fields.append({fieldDef.name, "数据不足", fieldDef.description,
                           offset + apduOffset, 0, 2});
            break;
          }

          QByteArray fieldData = apduData.mid(apduOffset, actualLength);
          QString fieldValue = fieldData.toHex().toUpper();

          // 根据字段类型进行格式化
          QString fieldDesc = fieldDef.description;
          if (fieldDef.type == "BIN") {
            // 二进制数据，显示十进制值
            if (actualLength == 1) {
              uint8_t val = (uint8_t)fieldData[0];
              fieldDesc += QString(" (十进制: %1)").arg(val);
              // 记录字段ID和值（用于嵌套协议引用）
              if (!fieldDef.fieldId.isEmpty()) {
                fieldIdValues[fieldDef.fieldId] = val;
              }
            } else if (actualLength == 2) {
              uint16_t val =
                  (uint8_t)fieldData[0] | ((uint8_t)fieldData[1] << 8);
              fieldDesc += QString(" (十进制: %1)").arg(val);
              if (!fieldDef.fieldId.isEmpty()) {
                fieldIdValues[fieldDef.fieldId] = val;
              }
            }
          } else if (fieldDef.type == "BCD") {
            // BCD码，地址在报文中是逆序存储的，需要反转
            QByteArray bcdReversed;
            for (int i = fieldData.size() - 1; i >= 0; --i) {
              bcdReversed.append(fieldData[i]);
            }
            QString bcdStr = bcdReversed.toHex().toUpper();
            fieldDesc += QString(" (地址: %1)").arg(bcdStr);
          } else if (fieldDef.type == "Blob" &&
                     !fieldDef.nestedProtocolRef.isEmpty()) {
            // ========================================
            // 嵌套协议解析
            // ========================================
            int protocolTypeValue =
                fieldIdValues.value(fieldDef.nestedProtocolRef, -1);

            if (protocolTypeValue >= 0 && actualLength > 0) {
              fieldDesc += QString(" [嵌套协议解析]");
              fields.append({fieldDef.name, fieldValue, fieldDesc,
                             offset + apduOffset, actualLength, 2});

              // 解析内嵌协议数据（支持多帧）
              int nestedOffset = 0;
              int frameIndex = 0;
              while (nestedOffset < actualLength) {
                QByteArray nestedData = fieldData.mid(nestedOffset);
                if (nestedData.isEmpty())
                  break;

                ProtocolType detectedType = ProtocolType::Unknown;
                int frameLen = 0;

                // 根据规约类型值确定协议
                // 00H: 透明传输（自动探测）
                // 01H: DL/T645-1997（暂用 2007 解析）
                // 02H: DL/T645-2007
                // 03H: DL/T698.45
                if (protocolTypeValue == 0x00) {
                  // 透明传输：使用自动探测
                  detectedType = ProtocolDetector::detectProtocol(nestedData);
                } else if (protocolTypeValue == 0x01 ||
                           protocolTypeValue == 0x02) {
                  detectedType = ProtocolType::Protocol645;
                } else if (protocolTypeValue == 0x03) {
                  detectedType = ProtocolType::Protocol698;
                }

                if (detectedType == ProtocolType::Protocol645 &&
                    Prot645Parser::isValidFrame(nestedData)) {
                  // 解析 DL/T 645 帧
                  frameLen = ProtocolDetector::getFrameLength(
                      nestedData, ProtocolType::Protocol645);
                  if (frameLen > 0 && frameLen <= nestedData.size()) {
                    QByteArray frame645 = nestedData.left(frameLen);
                    Prot645Parser parser645;
                    auto fields645 = parser645.parse(frame645);

                    // 添加子帧标题
                    QString frameTitle = (frameIndex == 0)
                                             ? "内嵌 DL/T 645 帧"
                                             : QString("内嵌 DL/T 645 帧 #%1")
                                                   .arg(frameIndex + 1);
                    fields.append({frameTitle, frame645.toHex().toUpper(),
                                   QString("%1 字节").arg(frameLen),
                                   offset + apduOffset + nestedOffset, frameLen,
                                   3});

                    // 添加解析后的子字段（增加缩进）
                    for (const auto &f645 : fields645) {
                      fields.append(
                          {f645.name, f645.value, f645.description,
                           offset + apduOffset + nestedOffset + f645.offset,
                           f645.length, 4 + f645.indent});
                    }

                    nestedOffset += frameLen;
                    frameIndex++;
                  } else {
                    break; // 无法确定帧长度，退出循环
                  }
                } else if (detectedType == ProtocolType::Protocol698 &&
                           Prot698Parser::isValidFrame(nestedData)) {
                  // 解析 DL/T 698 帧
                  frameLen = ProtocolDetector::getFrameLength(
                      nestedData, ProtocolType::Protocol698);
                  if (frameLen > 0 && frameLen <= nestedData.size()) {
                    QByteArray frame698 = nestedData.left(frameLen);
                    Prot698Parser parser698;
                    auto fields698 = parser698.parse(frame698);

                    // 添加子帧标题
                    QString frameTitle = (frameIndex == 0)
                                             ? "内嵌 DL/T 698 帧"
                                             : QString("内嵌 DL/T 698 帧 #%1")
                                                   .arg(frameIndex + 1);
                    fields.append({frameTitle, frame698.toHex().toUpper(),
                                   QString("%1 字节").arg(frameLen),
                                   offset + apduOffset + nestedOffset, frameLen,
                                   3});

                    // 添加解析后的子字段（增加缩进）
                    for (const auto &f698 : fields698) {
                      fields.append(
                          {f698.name, f698.value, f698.description,
                           offset + apduOffset + nestedOffset + f698.offset,
                           f698.length, 4 + f698.indent});
                    }

                    nestedOffset += frameLen;
                    frameIndex++;
                  } else {
                    break; // 无法确定帧长度，退出循环
                  }
                } else {
                  // 无法识别的嵌套协议，退出循环
                  if (nestedOffset == 0) {
                    fields.append(
                        {"未识别的嵌套数据", nestedData.toHex().toUpper(),
                         QString("无法解析，协议类型=%1")
                             .arg(protocolTypeValue),
                         offset + apduOffset + nestedOffset, actualLength, 3});
                  }
                  break;
                }
              }

              apduOffset += actualLength;
              continue; // 已处理完嵌套协议字段，跳过下面的普通字段添加
            }
          }

          fields.append({fieldDef.name, fieldValue, fieldDesc,
                         offset + apduOffset, actualLength, 2});
          apduOffset += actualLength;
        }
      }
    }

    offset += apduLen;
  } else if (apduLen < 0) {
    // 如果APDU长度为负数，说明数据结构有问题
    qDebug() << "Error: Invalid APDU length (negative). CS offset:" << csOffset
             << ", current offset:" << offset;
    return fields;
  }
  // 如果apduLen == 0，则没有APDU数据，不添加字段

  // ========================================================================
  // 9. CS（校验和）
  // ========================================================================
  // 校验和计算是帧完整性验证的关键。
  //
  // 计算规则（DL/T 376.2规范）：
  // - 起始位置：控制域C（偏移量3）
  // - 计算范围：C + R + A + AFN + DT + APDU（所有字节）
  // - 计算方法：八位位组算术和，不考虑溢出位（模256加法）
  // - 结束位置：CS字段之前（不包含CS本身）
  //
  // 注意：不包括起始符68H和长度域L
  // ========================================================================

  // 校验和从偏移量3（C字段）开始计算，包括C、R、A、AFN、DT、APDU所有字节
  // 计算范围：从偏移量3到当前offset-1（CS字段之前）
  // 使用模256加法（uint8_t自动溢出）
  uint8_t cs = (uint8_t)data[offset];
  int csCalcStart = 3;                  // 从C字段开始（偏移量3）
  int csCalcLen = offset - csCalcStart; // 计算长度 = CS偏移量 - 起始偏移量
  uint8_t calcCs = calculateCS(data, csCalcStart, csCalcLen);

  QString csDesc =
      (cs == calcCs)
          ? "校验正确"
          : QString("校验错误，期望值 %1").arg(calcCs, 2, 16, QChar('0'));
  fields.append({"校验和 CS",
                 QString("%1").arg(cs, 2, 16, QChar('0')).toUpper(), csDesc,
                 offset, 1, 0});
  offset++;

  // ========================================================================
  // 10. 结束字符 16H（最后1字节）
  // ========================================================================
  // 所有有效的DL/T 376.2帧必须以0x16结束。
  // 如果结束字符不匹配，标记为错误但仍显示解析结果（用于调试）。
  // ========================================================================
  if ((uint8_t)data[offset] == 0x16) {
    fields.append({"结束符", "16", "", offset, 1, 0});
  } else {
    fields.append(
        {"结束符", data.mid(offset, 1).toHex(), "期望值 16", offset, 1, 0});
  }

  return fields;
}

QString Prot3762Parser::getAfnName(int afn) {
  auto *def = Prot3762ConfigManager::instance().getAfnDef(afn);
  if (def)
    return def->name;
  return "未知 AFN";
}
