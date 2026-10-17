#include "Prot104Parser.h"
#include "ProtocolDetector.h"
#include <QDebug>
#include <cstdint>
#include <cstring>

Prot104Parser::Prot104Parser() {}

// 静态方法：检查 IEC 104 帧是否有效（用于协议自动检测）
// 进行严格的帧格式和APCI验证
bool Prot104Parser::isValidFrame(const QByteArray &data) {
  // Minimal length: 68 L + 4 bytes APCI = 6 bytes.
  if (data.size() < 6)
    return false;

  // 1. Start Byte 68H
  if ((uint8_t)data[0] != 0x68)
    return false;

  // 2. APDU Length
  // Length is byte 1.
  uint8_t len = (uint8_t)data[1];

  // Check minimal length (4 bytes APCI)
  if (len < 4)
    return false;
  // Check max length (253, since APDU max 253 + 2 header = 255. Max L is 255?)
  // Standard says Max APDU Length is 253.
  // L is length of APDU (APCI+ASDU).
  // Total Size = L + 2.
  if (data.size() != (int)len + 2)
    return false;

  // 3. Control Field (APCI) Checks
  // APCI is 4 bytes starting at offset 2.
  uint8_t c1 = (uint8_t)data[2];
  uint8_t c2 = (uint8_t)data[3];
  uint8_t c3 = (uint8_t)data[4];
  uint8_t c4 = (uint8_t)data[5];

  // Check Format Type
  if ((c1 & 0x01) == 0) {
    // I-Format (Information transfer)
    // Bit 0 is 0.
    // Valid.
  } else if ((c1 & 0x03) == 1) {
    // S-Format (Numbered supervisory functions)
    // Bit 0=1, Bit 1=0.
    // C1 should be 0x01? The standard says only first byte matters for type
    // bits. But S-Format usually has fixed structure? S-Format: Byte 1 = 0x01.
    // Byte 2 = 0x00. Byte 3-4 Receive Sequence. But we should allow Rx
    // sequence. So just checking type bits is enough? Let's rely on type bits.
  } else if ((c1 & 0x03) == 3) {
    // U-Format (Unnumbered control functions)
    // Bit 0=1, Bit 1=1.
    // U-Format usually Byte 1 has specific values (START, STOP, TEST).
    // Byte 2,3,4 usually 0.
    // But strict check: C2=0, C3=0, C4=0 is often required?
    // Standard IEC 60870-5-104:
    // U-Format: Length is 4 (APCI only). So len=4.
    // If U-Format has ASDU? No, U-Format is control only.
    // So if (c1 & 3 == 3), len must be 4?
    // Yes, U-frames do not carry ASDUs.
    if (len != 4)
      return false;

    // Also checks reserved bits?
    // Byte 1: Allowed: 0x07 (STARTDT act), 0x0B (STARTDT con), 0x13 (STOPDT
    // act), 0x23 (STOPDT con), 0x43 (TESTFR act), 0x83 (TESTFR con). 0x07 =
    // 0000 0111 0x0B = 0000 1011 0x13 = 0001 0011
    // ...
    // All have Bit 0=1, Bit 1=1.
  } else {
    // Invalid?
    // (c1 & 1 == 0) handled
    // (c1 & 3 == 1) handled
    // (c1 & 3 == 3) handled
    // Logic covers all bytes?
    // 00 -> I
    // 01 -> S
    // 10 -> Impossible if bit 0 is 0 (would be I).
    // 11 -> U
    // Wait, (c1 & 1) == 0 covers X0.
    // (c1 & 3) == 1 covers 01.
    // (c1 & 3) == 3 covers 11.
    // Are there cases like 10?
    // 10 means bit 0 is 0. So it falls into I-Format.
    // So logic holds.
  }

  return true;
}

ProtocolSummary Prot104Parser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;

  // Minimal length: 68 L + 4 bytes APCI = 6 bytes.
  if (data.size() < 6)
    return summary;

  // 根据 APCI byte 1 (Offset 2) 判断格式
  // 68 L C1 C2 C3 C4
  uint8_t c1 = (uint8_t)data[2];

  if ((c1 & 0x01) == 0) {
    summary.messageType = "I格式(信息)";
    // 可选：尝试解析TypeID (ASDU Start at Offset 6)
    if (data.size() >= 7) {
      uint8_t typeId = (uint8_t)data[6];
      // 简单转义，这里不依赖实例方法
      summary.messageType += QString(" Type:%1").arg(typeId);
    }
  } else if ((c1 & 0x03) == 1) {
    summary.messageType = "S格式(监视)";
  } else {
    summary.messageType = "U格式(控制)";
    // Test/Start/Stop detction?
  }

  // 104通常不显示链路地址(IP层处理)，公共地址(Common Addr, ASDU Offset 1, 2
  // bytes in ASDU header?) ASDU Header structure: Type(1) + VSQ(1) + COT(2) +
  // CommonAddr(2) ASDU starts at 6. CommonAddr at 6+1+1+2 = 10.
  if (data.size() >= 12) {
    // Only for I-Format usually
    if ((c1 & 0x01) == 0) {
      uint16_t addr = (uint8_t)data[10] | ((uint8_t)data[11] << 8);
      summary.address = QString::number(addr);
    }
  }

  return summary;
}

QList<Prot104Field> Prot104Parser::parse(const QByteArray &data) {
  QList<Prot104Field> fields;

  if (data.size() < 2) {
    fields.append({"错误", "", "数据长度不足(至少需要2字节)", 0, 0, 0});
    return fields;
  }

  int offset = 0;

  // 检查启动字符 0x68
  uint8_t startByte = (uint8_t)data[offset];
  if (startByte != 0x68) {
    fields.append({"错误", QString::asprintf("%02X", startByte),
                   "不是有效的IEC 104帧(启动字符应为0x68)", offset, 1, 0});
    return fields;
  }

  fields.append({"启动字符", "68", "IEC 60870-5-104帧起始标识", offset, 1, 0});
  offset++;

  // APDU长度
  if (offset >= data.size()) {
    fields.append({"错误", "", "数据不完整:缺少APDU长度字段", offset, 0, 0});
    return fields;
  }

  uint8_t apduLen = (uint8_t)data[offset];
  fields.append({"APDU长度", QString::asprintf("%02X", apduLen),
                 QString("长度: %1 字节").arg(apduLen), offset, 1, 0});
  offset++;

  // 验证数据长度
  if (offset + apduLen > data.size()) {
    fields.append({"错误", "",
                   QString("数据不完整:期望%1字节,实际%2字节")
                       .arg(offset + apduLen)
                       .arg(data.size()),
                   offset, 0, 0});
    return fields;
  }

  // 解析APCI
  parseAPCI(data, offset, fields, 0);

  return fields;
}

void Prot104Parser::parseAPCI(const QByteArray &data, int &offset,
                              QList<Prot104Field> &fields, int indent) {
  if (offset + 4 > data.size()) {
    fields.append(
        {"错误", "", "APCI控制域数据不足(需要4字节)", offset, 0, indent});
    return;
  }

  int apciStart = offset;
  uint8_t byte1 = (uint8_t)data[offset];
  uint8_t byte2 = (uint8_t)data[offset + 1];
  uint8_t byte3 = (uint8_t)data[offset + 2];
  uint8_t byte4 = (uint8_t)data[offset + 3];

  QString apciHex = QString("%1 %2 %3 %4")
                        .arg(byte1, 2, 16, QChar('0'))
                        .toUpper()
                        .arg(byte2, 2, 16, QChar('0'))
                        .toUpper()
                        .arg(byte3, 2, 16, QChar('0'))
                        .toUpper()
                        .arg(byte4, 2, 16, QChar('0'))
                        .toUpper();

  // 判断帧类型
  // APCI Format:
  // Byte 0: Type (bit 0,1)
  // I-Frame: Bit 0 = 0.
  // S-Frame: Bit 0 = 1, Bit 1 = 0.
  // U-Frame: Bit 0 = 1, Bit 1 = 1.

  if ((byte1 & 0x01) == 0) {
    // I帧 (信息传输格式)
    // I-Frame Structure:
    // Byte 0: Tx Seq LBS (bit 1-7) + 0 (bit 0)
    // Byte 1: Tx Seq MSB
    // Byte 2: Rx Seq LSB (bit 1-7) + 0 (bit 0)
    // Byte 3: Rx Seq MSB

    uint16_t sendSeqLow = (byte1 >> 1) & 0x7F;
    uint16_t sendSeqHigh = byte2;
    uint16_t sendSeq = (sendSeqHigh << 7) | sendSeqLow;

    uint16_t recvSeq = getRecvSeq(data, offset);

    QString desc = QString("I帧(信息传输) - 发送序号:%1, 接收序号:%2")
                       .arg(sendSeq)
                       .arg(recvSeq);

    fields.append({"APCI控制域", apciHex, desc, apciStart, 4, indent});
    offset += 4;

    // I帧包含ASDU,继续解析
    parseASDU(data, offset, fields, indent);

  } else if ((byte1 & 0x03) == 0x01) {
    // S帧 (监督功能格式)
    uint16_t recvSeq = getRecvSeq(data, offset);

    QString desc = QString("S帧(监督/确认) - 接收序号:%1").arg(recvSeq);

    fields.append({"APCI控制域", apciHex, desc, apciStart, 4, indent});
    offset += 4;

  } else if ((byte1 & 0x03) == 0x03) {
    // U帧 (未编号控制功能格式)
    uint8_t func = (byte1 >> 2) & 0x0F;
    QString funcName = getUFuncName(func);

    QString desc = QString("U帧(控制) - %1").arg(funcName);

    fields.append({"APCI控制域", apciHex, desc, apciStart, 4, indent});
    offset += 4;

  } else {
    fields.append({"APCI控制域", apciHex, "未知帧格式", apciStart, 4, indent});
    offset += 4;
  }
}

void Prot104Parser::parseASDU(const QByteArray &data, int &offset,
                              QList<Prot104Field> &fields, int indent) {
  if (offset >= data.size()) {
    return; // 没有ASDU数据(如S帧、U帧)
  }

  if (offset + 4 > data.size()) {
    fields.append({"错误", "", "ASDU头数据不足", offset, 0, indent});
    return;
  }

  int asduStart = offset;

  // 类型标识
  uint8_t typeId = (uint8_t)data[offset];
  QString typeIdName = getTypeIdName(typeId);
  fields.append({"类型标识", QString::asprintf("%02X", typeId),
                 QString("Type: %1 - %2").arg(typeId).arg(typeIdName), offset,
                 1, indent + 1});
  offset++;

  // 可变结构限定词 VSQ
  uint8_t vsq = (uint8_t)data[offset];
  Prot104VSQ vsqUnion;
  vsqUnion.val = vsq;
  bool sq = vsqUnion.bits.sq;
  int num = vsqUnion.bits.num;

  QString vsqDesc = QString("SQ:%1(%2), 数量:%3")
                        .arg(sq)
                        .arg(sq ? "连续" : "不连续")
                        .arg(num);
  fields.append({"可变结构限定词", QString::asprintf("%02X", vsq), vsqDesc,
                 offset, 1, indent + 1});
  offset++;

  // 传输原因 COT (2字节: 原因 + 源地址)
  uint8_t cotByte = (uint8_t)data[offset];
  uint8_t cot = cotByte & 0x3F;      // 低6位
  bool pn = (cotByte >> 6) & 0x01;   // bit6: 确认标志
  bool test = (cotByte >> 7) & 0x01; // bit7: 测试标志
  offset++;

  uint8_t oa = (uint8_t)data[offset]; // 源地址
  offset++;

  QString cotName = getCauseOfTransmissionName(cot);

  QString cotDesc = QString("原因:%1 - %2").arg(cot).arg(cotName);
  if (pn)
    cotDesc += " [确认]";
  if (test)
    cotDesc += " [测试]";
  if (oa != 0)
    cotDesc += QString(", 源地址:%1").arg(oa);

  // 104规定COT为2字节，我们在Field里显示合并后的hex或分开展现
  // 这里合并展示
  fields.append({"传输原因", data.mid(offset - 2, 2).toHex().toUpper(), cotDesc,
                 offset - 2, 2, indent + 1});

  // 公共地址 (2字节)
  uint16_t addr = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  fields.append({"公共地址", data.mid(offset, 2).toHex().toUpper(),
                 QString("ASDU地址: %1").arg(addr), offset, 2, indent + 1});
  offset += 2;

  // 解析信息对象
  parseInformationObject(data, offset, fields, indent + 1, typeId, sq, num);
}

void Prot104Parser::parseInformationObject(const QByteArray &data, int &offset,
                                           QList<Prot104Field> &fields,
                                           int indent, uint8_t typeId, bool sq,
                                           int num) {
  if (num <= 0 || num > 127) {
    fields.append({"警告", "", QString("信息对象数量异常: %1").arg(num), offset,
                   0, indent});
    return;
  }

  for (int i = 0; i < num; ++i) {
    if (offset + 3 > data.size()) {
      fields.append({"错误", "", "信息对象数据不足", offset, 0, indent});
      return;
    }

    QString objHeader = QString("信息对象 [%1/%2]").arg(i + 1).arg(num);

    // 信息体地址 (3字节)
    uint32_t ioa = 0;
    if (!sq || i == 0) {
      // 地址不连续或第一个对象,解析地址
      ioa = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8) |
            ((uint8_t)data[offset + 2] << 16);

      fields.append({objHeader + " - 信息体地址",
                     data.mid(offset, 3).toHex().toUpper(),
                     QString("地址: %1 (0x%2)")
                         .arg(ioa)
                         .arg(ioa, 6, 16, QChar('0'))
                         .toUpper(),
                     offset, 3, indent});
      offset += 3;
    } else {
      // 地址连续,递增
      ioa++;
      fields.append({objHeader + " - 信息体地址", QString("(连续)"),
                     QString("地址: %1 (0x%2)")
                         .arg(ioa)
                         .arg(ioa, 6, 16, QChar('0'))
                         .toUpper(),
                     offset, 0, indent});
    }

    // 根据类型标识解析信息元素
    Prot104TypeId type = (Prot104TypeId)typeId;

    switch (type) {
    case Prot104TypeId::M_SP_NA_1: // 单点信息
      if (offset < data.size()) {
        uint8_t siq = (uint8_t)data[offset];
        QString siqDesc = getSIQDescription(siq);
        fields.append({objHeader + " - SIQ", QString::asprintf("%02X", siq),
                       siqDesc, offset, 1, indent + 1});
        offset++;
      }
      break;

    case Prot104TypeId::M_DP_NA_1: // 双点信息
      if (offset < data.size()) {
        uint8_t diq = (uint8_t)data[offset];
        QString diqDesc = getDIQDescription(diq);
        fields.append({objHeader + " - DIQ", QString::asprintf("%02X", diq),
                       diqDesc, offset, 1, indent + 1});
        offset++;
      }
      break;

    case Prot104TypeId::M_ME_NA_1: // 测量值,归一化值
    case Prot104TypeId::M_ME_ND_1: // 测量值,不带品质描述的归一化值
      if (offset + 2 <= data.size()) {
        int16_t nva =
            (int16_t)((uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8));
        double normalized = nva / 32768.0; // 归一化到 -1.0 ~ +1.0
        fields.append(
            {objHeader + " - 归一化值", data.mid(offset, 2).toHex().toUpper(),
             QString("值: %1 (归一化: %2)").arg(nva).arg(normalized, 0, 'f', 4),
             offset, 2, indent + 1});
        offset += 2;

        if (type == Prot104TypeId::M_ME_NA_1 && offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          QString qdsDesc = getQDSDescription(qds);
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         qdsDesc, offset, 1, indent + 1});
          offset++;
        }
      }
      break;

    case Prot104TypeId::M_ME_NB_1: // 测量值,标度化值
      if (offset + 2 <= data.size()) {
        int16_t sva =
            (int16_t)((uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8));
        fields.append({objHeader + " - 标度化值",
                       data.mid(offset, 2).toHex().toUpper(),
                       QString("值: %1").arg(sva), offset, 2, indent + 1});
        offset += 2;

        if (offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          QString qdsDesc = getQDSDescription(qds);
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         qdsDesc, offset, 1, indent + 1});
          offset++;
        }
      }
      break;

    case Prot104TypeId::M_ME_NC_1: // 测量值,短浮点数
      if (offset + 4 <= data.size()) {
        float val;
        memcpy(&val, data.constData() + offset, 4);
        fields.append(
            {objHeader + " - 短浮点数", data.mid(offset, 4).toHex().toUpper(),
             QString("值: %1").arg(val, 0, 'f', 2), offset, 4, indent + 1});
        offset += 4;

        if (offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          QString qdsDesc = getQDSDescription(qds);
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         qdsDesc, offset, 1, indent + 1});
          offset++;
        }
      }
      break;

    case Prot104TypeId::M_SP_TB_1: // 带CP56Time2a时标的单点信息
      if (offset < data.size()) {
        uint8_t siq = (uint8_t)data[offset];
        QString siqDesc = getSIQDescription(siq);
        fields.append({objHeader + " - SIQ", QString::asprintf("%02X", siq),
                       siqDesc, offset, 1, indent + 1});
        offset++;
      }
      if (offset + 7 <= data.size()) {
        int timeStart = offset;
        QString timeStr = parseCP56Time2a(data, offset);
        fields.append({objHeader + " - CP56Time2a",
                       data.mid(timeStart, 7).toHex().toUpper(), timeStr,
                       timeStart, 7, indent + 1});
      }
      break;

    case Prot104TypeId::M_ME_TD_1: // 带CP56Time2a时标的测量值,归一化值
      if (offset + 2 <= data.size()) {
        int16_t nva =
            (int16_t)((uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8));
        double normalized = nva / 32768.0;
        fields.append(
            {objHeader + " -归一化值", data.mid(offset, 2).toHex().toUpper(),
             QString("值: %1 (归一化: %2)").arg(nva).arg(normalized, 0, 'f', 4),
             offset, 2, indent + 1});
        offset += 2;
      }
      if (offset < data.size()) {
        uint8_t qds = (uint8_t)data[offset];
        QString qdsDesc = getQDSDescription(qds);
        fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                       qdsDesc, offset, 1, indent + 1});
        offset++;
      }
      if (offset + 7 <= data.size()) {
        int timeStart = offset;
        QString timeStr = parseCP56Time2a(data, offset);
        fields.append({objHeader + " - CP56Time2a",
                       data.mid(timeStart, 7).toHex().toUpper(), timeStr,
                       timeStart, 7, indent + 1});
      }
      break;

    case Prot104TypeId::C_SC_NA_1: // 单命令
      if (offset < data.size()) {
        uint8_t sco = (uint8_t)data[offset];
        QString scoDesc = getSCODescription(sco);
        fields.append({objHeader + " - SCO", QString::asprintf("%02X", sco),
                       scoDesc, offset, 1, indent + 1});
        offset++;
      }
      break;

    case Prot104TypeId::C_IC_NA_1: // 总召唤命令
      if (offset < data.size()) {
        uint8_t qoi = (uint8_t)data[offset];
        QString qoiDesc;
        if (qoi == 20)
          qoiDesc = "站召唤(总召唤)";
        else if (qoi >= 21 && qoi <= 36)
          qoiDesc = QString("第%1组召唤").arg(qoi - 20);
        else
          qoiDesc = "未定义";

        fields.append({objHeader + " - QOI", QString::asprintf("%02X", qoi),
                       QString("召唤限定词: %1 - %2").arg(qoi).arg(qoiDesc),
                       offset, 1, indent + 1});
        offset++;
      }
      break;

    case Prot104TypeId::C_CS_NA_1: // 时钟同步命令
      if (offset + 7 <= data.size()) {
        int timeStart = offset;
        QString timeStr = parseCP56Time2a(data, offset);
        fields.append({objHeader + " - CP56Time2a",
                       data.mid(timeStart, 7).toHex().toUpper(),
                       QString("时钟同步时间: %1").arg(timeStr), timeStart, 7,
                       indent + 1});
      }
      break;

    default:
      // 未实现的类型,显示剩余数据
      int remaining = data.size() - offset;
      if (remaining > 0) {
        int displayLen = qMin(remaining, 16); // 最多显示16字节
        fields.append(
            {objHeader + " - 数据",
             data.mid(offset, displayLen).toHex().toUpper(),
             QString("未实现的类型标识解析,剩余%1字节").arg(remaining), offset,
             displayLen, indent + 1});
        offset += remaining;
      }
      break;
    }
  }
}

#include "ProtocolDetector.h" // Assuming this is the correct path for ProtocolSummary

// Forward declaration of Prot104Parser if not already in a header
// class Prot104Parser;

// Forward declaration of Prot104Parser if not already in a header
// class Prot104Parser;

QString Prot104Parser::parseCP24Time2a(const QByteArray &data, int &offset) {
  if (offset + 3 > data.size()) {
    return "时间数据不足";
  }

  uint16_t ms = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  uint8_t byte3 = (uint8_t)data[offset + 2];
  bool iv = (byte3 >> 7) & 0x01;

  offset += 3;

  return QString("%1 ms%2").arg(ms).arg(iv ? " [无效]" : "");
}

QString Prot104Parser::parseCP56Time2a(const QByteArray &data, int &offset) {
  if (offset + 7 > data.size()) {
    return "时间数据不足";
  }

  Prot104CP56Time2a time;
  memcpy(&time, data.constData() + offset, 7);

  offset += 7;

  // 解析时间字段
  int year = 2000 + time.year;
  int month = time.month;
  int day = time.day;
  int hour = time.hour;
  int minute = time.minute;
  int second = time.milliseconds / 1000;
  int ms = time.milliseconds % 1000;

  QString timeStr = QString("%1-%2-%3 %4:%5:%6.%7")
                        .arg(year, 4, 10, QChar('0'))
                        .arg(month, 2, 10, QChar('0'))
                        .arg(day, 2, 10, QChar('0'))
                        .arg(hour, 2, 10, QChar('0'))
                        .arg(minute, 2, 10, QChar('0'))
                        .arg(second, 2, 10, QChar('0'))
                        .arg(ms, 3, 10, QChar('0'));

  if (time.iv)
    timeStr += " [无效]";
  if (time.su)
    timeStr += " [夏令时]";

  return timeStr;
}

QString Prot104Parser::getTypeIdName(uint8_t typeId) {
  switch ((Prot104TypeId)typeId) {
  case Prot104TypeId::M_SP_NA_1:
    return "单点信息";
  case Prot104TypeId::M_SP_TA_1:
    return "带时标的单点信息";
  case Prot104TypeId::M_DP_NA_1:
    return "双点信息";
  case Prot104TypeId::M_DP_TA_1:
    return "带时标的双点信息";
  case Prot104TypeId::M_ST_NA_1:
    return "步位置信息";
  case Prot104TypeId::M_ST_TA_1:
    return "带时标的步位置信息";
  case Prot104TypeId::M_BO_NA_1:
    return "比特串";
  case Prot104TypeId::M_BO_TA_1:
    return "带时标的比特串";
  case Prot104TypeId::M_ME_NA_1:
    return "测量值(归一化值)";
  case Prot104TypeId::M_ME_TA_1:
    return "带时标的测量值(归一化值)";
  case Prot104TypeId::M_ME_NB_1:
    return "测量值(标度化值)";
  case Prot104TypeId::M_ME_TB_1:
    return "带时标的测量值(标度化值)";
  case Prot104TypeId::M_ME_NC_1:
    return "测量值(短浮点数)";
  case Prot104TypeId::M_ME_TC_1:
    return "带时标的测量值(短浮点数)";
  case Prot104TypeId::M_IT_NA_1:
    return "累计量";
  case Prot104TypeId::M_IT_TA_1:
    return "带时标的累计量";
  case Prot104TypeId::M_EP_TA_1:
    return "带时标的继电保护装置事件";
  case Prot104TypeId::M_EP_TB_1:
    return "带时标的继电保护装置成组启动事件";
  case Prot104TypeId::M_EP_TC_1:
    return "带时标的继电保护装置输出电路信息";
  case Prot104TypeId::M_PS_NA_1:
    return "具有状态变位检出的成组单点信息";
  case Prot104TypeId::M_ME_ND_1:
    return "测量值(不带品质描述的归一化值)";
  case Prot104TypeId::M_SP_TB_1:
    return "带CP56Time2a时标的单点信息";
  case Prot104TypeId::M_DP_TB_1:
    return "带CP56Time2a时标的双点信息";
  case Prot104TypeId::M_ST_TB_1:
    return "带CP56Time2a时标的步位置信息";
  case Prot104TypeId::M_BO_TB_1:
    return "带CP56Time2a时标的比特串";
  case Prot104TypeId::M_ME_TD_1:
    return "带CP56Time2a时标的测量值(归一化值)";
  case Prot104TypeId::M_ME_TE_1:
    return "带CP56Time2a时标的测量值(标度化值)";
  case Prot104TypeId::M_ME_TF_1:
    return "带CP56Time2a时标的测量值(短浮点数)";
  case Prot104TypeId::M_IT_TB_1:
    return "带CP56Time2a时标的累计量";
  case Prot104TypeId::M_EP_TD_1:
    return "带CP56Time2a时标的继电保护装置事件";
  case Prot104TypeId::M_EP_TE_1:
    return "带CP56Time2a时标的继电保护装置成组启动事件";
  case Prot104TypeId::M_EP_TF_1:
    return "带CP56Time2a时标的继电保护装置输出电路信息";
  case Prot104TypeId::C_SC_NA_1:
    return "单命令";
  case Prot104TypeId::C_DC_NA_1:
    return "双命令";
  case Prot104TypeId::C_RC_NA_1:
    return "升降命令";
  case Prot104TypeId::C_SE_NA_1:
    return "设定值命令(归一化值)";
  case Prot104TypeId::C_SE_NB_1:
    return "设定值命令(标度化值)";
  case Prot104TypeId::C_SE_NC_1:
    return "设定值命令(短浮点数)";
  case Prot104TypeId::C_BO_NA_1:
    return "比特串";
  case Prot104TypeId::C_SC_TA_1:
    return "带CP56Time2a时标的单命令";
  case Prot104TypeId::C_DC_TA_1:
    return "带CP56Time2a时标的双命令";
  case Prot104TypeId::C_RC_TA_1:
    return "带CP56Time2a时标的升降命令";
  case Prot104TypeId::C_SE_TA_1:
    return "带CP56Time2a时标的设定值命令(归一化值)";
  case Prot104TypeId::C_SE_TB_1:
    return "带CP56Time2a时标的设定值命令(标度化值)";
  case Prot104TypeId::C_SE_TC_1:
    return "带CP56Time2a时标的设定值命令(短浮点数)";
  case Prot104TypeId::C_BO_TA_1:
    return "带CP56Time2a时标的比特串";
  case Prot104TypeId::M_EI_NA_1:
    return "初始化结束";
  case Prot104TypeId::C_IC_NA_1:
    return "站召唤(总召唤)命令";
  case Prot104TypeId::C_CI_NA_1:
    return "计数量召唤命令";
  case Prot104TypeId::C_RD_NA_1:
    return "读命令";
  case Prot104TypeId::C_CS_NA_1:
    return "时钟同步命令";
  case Prot104TypeId::C_TS_NA_1:
    return "测试命令";
  case Prot104TypeId::C_RP_NA_1:
    return "复位进程命令";
  case Prot104TypeId::C_CD_NA_1:
    return "延时获得命令";
  case Prot104TypeId::C_TS_TA_1:
    return "带CP56Time2a时标的测试命令";
  case Prot104TypeId::F_FR_NA_1:
    return "文件准备就绪";
  case Prot104TypeId::F_SR_NA_1:
    return "节准备就绪";
  case Prot104TypeId::F_SC_NA_1:
    return "召唤目录/文件/节";
  case Prot104TypeId::F_LS_NA_1:
    return "最后的节/段";
  case Prot104TypeId::F_AF_NA_1:
    return "确认文件/节";
  case Prot104TypeId::F_SG_NA_1:
    return "段";
  case Prot104TypeId::F_DR_TA_1:
    return "目录";
  case Prot104TypeId::F_SC_NB_1:
    return "查询日志/日志区段";
  default:
    return QString("未知类型(%1)").arg(typeId);
  }
}

QString Prot104Parser::getCauseOfTransmissionName(uint8_t cot) {
  switch ((Prot104CauseOfTransmission)cot) {
  case Prot104CauseOfTransmission::COT_PER_CYC:
    return "周期/循环";
  case Prot104CauseOfTransmission::COT_BACK:
    return "背景扫描";
  case Prot104CauseOfTransmission::COT_SPONT:
    return "突发/自发";
  case Prot104CauseOfTransmission::COT_INIT:
    return "初始化";
  case Prot104CauseOfTransmission::COT_REQ:
    return "请求";
  case Prot104CauseOfTransmission::COT_ACT:
    return "激活";
  case Prot104CauseOfTransmission::COT_ACTCON:
    return "激活确认";
  case Prot104CauseOfTransmission::COT_DEACT:
    return "停止激活";
  case Prot104CauseOfTransmission::COT_DEACTCON:
    return "停止激活确认";
  case Prot104CauseOfTransmission::COT_ACTTERM:
    return "激活终止";
  case Prot104CauseOfTransmission::COT_RETREM:
    return "远程命令引起的返送";
  case Prot104CauseOfTransmission::COT_RETLOC:
    return "当地命令引起的返送";
  case Prot104CauseOfTransmission::COT_FILE:
    return "文件传输";
  case Prot104CauseOfTransmission::COT_INROGEN:
    return "响应站召唤";
  case Prot104CauseOfTransmission::COT_INRO1:
    return "响应第1组召唤";
  case Prot104CauseOfTransmission::COT_INRO2:
    return "响应第2组召唤";
  case Prot104CauseOfTransmission::COT_INRO3:
    return "响应第3组召唤";
  case Prot104CauseOfTransmission::COT_INRO4:
    return "响应第4组召唤";
  case Prot104CauseOfTransmission::COT_REQCOGEN:
    return "响应计数量站召唤";
  case Prot104CauseOfTransmission::COT_UNKNOWNTYPE:
    return "未知的类型标识";
  case Prot104CauseOfTransmission::COT_UNKNOWNCOT:
    return "未知的传输原因";
  case Prot104CauseOfTransmission::COT_UNKNOWNCA:
    return "未知的公共地址";
  case Prot104CauseOfTransmission::COT_UNKNOWNIOA:
    return "未知的信息对象地址";
  default:
    return QString("未定义(%1)").arg(cot);
  }
}

QString Prot104Parser::getUFuncName(uint8_t func) {
  switch ((Prot104UFuncCode)func) {
  case Prot104UFuncCode::STARTDT_ACT:
    return "启动数据传输激活 (STARTDT act)";
  case Prot104UFuncCode::STARTDT_CON:
    return "启动数据传输确认 (STARTDT con)";
  case Prot104UFuncCode::STOPDT_ACT:
    return "停止数据传输激活 (STOPDT act)";
  case Prot104UFuncCode::STOPDT_CON:
    return "停止数据传输确认 (STOPDT con)";
  case Prot104UFuncCode::TESTFR_ACT:
    return "测试帧激活 (TESTFR act)";
  case Prot104UFuncCode::TESTFR_CON:
    return "测试帧确认 (TESTFR con)";
  default:
    return QString("未知功能(%1)").arg(func);
  }
}

QString Prot104Parser::getSIQDescription(uint8_t siq) {
  Prot104SIQ siqUnion;
  siqUnion.val = siq;

  QString status = siqUnion.bits.spi ? "合(ON)" : "分(OFF)";
  QStringList flags;
  if (siqUnion.bits.bl)
    flags.append("闭塞");
  if (siqUnion.bits.sb)
    flags.append("取代");
  if (siqUnion.bits.nt)
    flags.append("非当前值");
  if (siqUnion.bits.iv)
    flags.append("无效");

  QString result = QString("状态:%1").arg(status);
  if (!flags.isEmpty()) {
    result += QString(" [%1]").arg(flags.join(", "));
  }

  return result;
}

QString Prot104Parser::getDIQDescription(uint8_t diq) {
  Prot104DIQ diqUnion;
  diqUnion.val = diq;

  QString status;
  switch (diqUnion.bits.dpi) {
  case 0:
    status = "不确定或中间状态";
    break;
  case 1:
    status = "分(OFF)";
    break;
  case 2:
    status = "合(ON)";
    break;
  case 3:
    status = "不确定";
    break;
  }

  QStringList flags;
  if (diqUnion.bits.bl)
    flags.append("闭塞");
  if (diqUnion.bits.sb)
    flags.append("取代");
  if (diqUnion.bits.nt)
    flags.append("非当前值");
  if (diqUnion.bits.iv)
    flags.append("无效");

  QString result = QString("状态:%1").arg(status);
  if (!flags.isEmpty()) {
    result += QString(" [%1]").arg(flags.join(", "));
  }

  return result;
}

QString Prot104Parser::getQDSDescription(uint8_t qds) {
  Prot104QDS qdsUnion;
  qdsUnion.val = qds;

  QStringList flags;
  if (qdsUnion.bits.ov)
    flags.append("溢出");
  if (qdsUnion.bits.ei)
    flags.append("累积进位");
  if (qdsUnion.bits.ca)
    flags.append("已调整");
  if (qdsUnion.bits.cy)
    flags.append("周期溢出");
  if (qdsUnion.bits.se)
    flags.append("顺序异常");
  if (qdsUnion.bits.iv)
    flags.append("无效");

  if (flags.isEmpty()) {
    return "品质:正常";
  } else {
    return QString("品质:[%1]").arg(flags.join(", "));
  }
}

QString Prot104Parser::getSCODescription(uint8_t sco) {
  Prot104SCO scoUnion;
  scoUnion.val = sco;

  QString cmd = scoUnion.bits.scs ? "合(ON)" : "分(OFF)";
  QString mode;

  switch (scoUnion.bits.qu) {
  case 0:
    mode = "无额外定义";
    break;
  case 1:
    mode = "短脉冲";
    break;
  case 2:
    mode = "长脉冲";
    break;
  case 3:
    mode = "持续输出";
    break;
  default:
    mode = QString("未定义(%1)").arg(scoUnion.bits.qu);
    break;
  }

  QString seStr = scoUnion.bits.se ? "选择" : "执行";

  return QString("命令:%1, 模式:%2, %3").arg(cmd).arg(mode).arg(seStr);
}

uint16_t Prot104Parser::getSendSeq(const Prot104ControlI &ctrl) {
  return (ctrl.bits.sendSeqHigh << 7) | ctrl.bits.sendSeqLow;
}

uint16_t Prot104Parser::getRecvSeq(const QByteArray &data, int offset) {
  if (offset + 4 > data.size())
    return 0;

  uint8_t byte3 = (uint8_t)data[offset + 2];
  uint8_t byte4 = (uint8_t)data[offset + 3];

  uint8_t recvSeqLow = (byte3 >> 1) & 0x7F;
  uint8_t recvSeqHigh = byte4;

  return (recvSeqHigh << 7) | recvSeqLow;
}
