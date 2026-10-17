#include "Prot101Parser.h"
#include "ProtocolDetector.h"
#include <QDebug>
#include <cstdint>
#include <cstring>

// 静态方法：检查 IEC 101 帧是否有效（用于协议自动检测）
// 支持单字符(E5)、固定帧(10)、变长帧(68)
bool Prot101Parser::isValidFrame(const QByteArray &data) {
  if (data.isEmpty())
    return false;
  uint8_t start = (uint8_t)data[0];

  // 1. Single Character E5H
  if (start == 0xE5) {
    return data.size() == 1;
  }

  // 2. Fixed Frame 10H
  // Structure: 10 C A CS 16
  if (start == 0x10) {
    if (data.size() < 4)
      return false;

    // End Byte 16H
    if ((uint8_t)data[data.size() - 1] != 0x16)
      return false;

    // Checksum
    // Sum from index 1 (C) to End-2
    uint8_t cs = (uint8_t)data[data.size() - 2];
    uint8_t calcCs = 0;
    for (int i = 1; i < data.size() - 2; ++i) {
      calcCs += (uint8_t)data[i];
    }
    return cs == calcCs;
  }

  // 3. Variable Frame 68H
  // Structure: 68 L L 68 C A ASDU CS 16
  if (start == 0x68) {
    if (data.size() < 7)
      return false; // Minimal valid size

    // Check L1 == L2
    uint8_t l1 = (uint8_t)data[1];
    uint8_t l2 = (uint8_t)data[2];
    if (l1 != l2)
      return false;

    // Check Second Start 68H
    if ((uint8_t)data[3] != 0x68)
      return false;

    // Check End Byte 16H
    if ((uint8_t)data[data.size() - 1] != 0x16)
      return false;

    // Length Check
    // L excludes first 4 bytes (68 L L 68) and last 2 bytes (CS 16)?
    // No, Standard IEC 101: L is length of (C + A + ASDU).
    // Total Size should be: 4 (Header) + L + 2 (CS + 16).
    if (data.size() != 4 + l1 + 2)
      return false;

    // Checksum
    // Sum from index 4 (C) to End-2
    uint8_t cs = (uint8_t)data[data.size() - 2];
    uint8_t calcCs = 0;
    for (int i = 4; i < data.size() - 2; ++i) {
      calcCs += (uint8_t)data[i];
    }
    return cs == calcCs;
  }

  return false;
}

ProtocolSummary Prot101Parser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;
  summary.messageType = "IEC 101 帧";

  // 1. Single Char E5
  if (data.size() == 1 && (uint8_t)data[0] == 0xE5) {
    summary.messageType = "肯定确认(E5)";
    return summary;
  }

  // 2. Fixed Frame 10H
  if (data.size() >= 5 && (uint8_t)data[0] == 0x10) {
    summary.messageType = "固定帧";
    // 10 C A CS 16
    // 地址 A 在 offset 2. 长度取决于链路地址长度(1或2)。
    // 由于这里是静态方法，不能依赖成员变量m_linkAddrLen。
    // 但根据LogBackend的硬编码逻辑以及常见情况，尝试取第3字节。
    // LogBackend逻辑: data[2]
    summary.address =
        QString("%1").arg((uint8_t)data[2], 2, 16, QChar('0')).toUpper();

    uint8_t ctrl = (uint8_t)data[1];
    if (ctrl & 0x40)
      summary.direction = "上行(主->从)"; // PRM=1
    else
      summary.direction = "下行(从->主)"; // PRM=0
  }
  // 3. Variable Frame 68H
  else if (data.size() >= 8 && (uint8_t)data[0] == 0x68) {
    summary.messageType = "可变帧";
    // 68 L L 68 C A ...
    // LogBackend logic: data[5]
    summary.address =
        QString("%1").arg((uint8_t)data[5], 2, 16, QChar('0')).toUpper();

    uint8_t ctrl = (uint8_t)data[4];
    if (ctrl & 0x40)
      summary.direction = "上行(主->从)";
    else
      summary.direction = "下行(从->主)";

    // 尝试解析ASDU TypeID (Offset 4 + LinkAddrLen + 1?)
    // 假设 LinkAddrLen = 1 (A在5), 则 ASDU Type 在 6
    // 假设 LinkAddrLen = 2 (A在5,6), 则 ASDU Type 在 7
    // 简单试探:
    // 如果 L 足够大，且看起来像TypeId
    int typeOffset = 6;
    if (data.size() > typeOffset) {
      // 简单的启发式: ASDU Type usually != 0
      // 可以尝试获取名称
    }
  }

  return summary;
}

Prot101Parser::Prot101Parser()
    : m_linkAddrLen(1), m_commonAddrLen(2), m_cotLen(1), m_ioaLen(2) {}

void Prot101Parser::setLinkAddressLength(int len) {
  if (len == 1 || len == 2) {
    m_linkAddrLen = len;
  }
}

void Prot101Parser::setCommonAddressLength(int len) {
  if (len == 1 || len == 2) {
    m_commonAddrLen = len;
  }
}

void Prot101Parser::setCotLength(int len) {
  if (len == 1 || len == 2) {
    m_cotLen = len;
  }
}

void Prot101Parser::setInfoObjAddrLength(int len) {
  if (len >= 1 && len <= 3) {
    m_ioaLen = len;
  }
}

QList<Prot101Field> Prot101Parser::parse(const QByteArray &data) {
  QList<Prot101Field> fields;

  if (data.isEmpty()) {
    return fields;
  }

  uint8_t startByte = (uint8_t)data[0];

  if (startByte == PROT101_FIXED_START) {
    parseFixedFrame(data, fields);
  } else if (startByte == PROT101_VAR_START) {
    parseVariableFrame(data, fields);
  } else if (startByte == PROT101_SINGLE_CHAR) {
    parseSingleChar(data, fields);
  } else {
    fields.append({"错误", QString::asprintf("%02X", startByte), "未知起始字符",
                   0, 1, 0});
  }

  return fields;
}

void Prot101Parser::parseFixedFrame(const QByteArray &data,
                                    QList<Prot101Field> &fields) {
  // 10 C A CS 16
  int expectedLen = 1 + 1 + m_linkAddrLen + 1 + 1;
  if (data.size() < expectedLen) {
    fields.append({"错误", "",
                   QString("固定帧长度不足(需要%1字节)").arg(expectedLen), 0, 0,
                   0});
    return;
  }

  int offset = 0;
  fields.append({"启动字符", "10", "固定帧起始", offset++, 1, 0});

  uint8_t ctrl = (uint8_t)data[offset];
  parseControlField(ctrl, offset, fields, 0);
  offset++;

  parseLinkAddress(data, offset, fields, 0);

  uint8_t cs = (uint8_t)data[offset];
  // 计算校验和: C + A
  uint8_t calcCs = ctrl;
  for (int i = 0; i < m_linkAddrLen; ++i) {
    calcCs += (uint8_t)data[offset - m_linkAddrLen + i];
  }

  QString csDesc = QString("校验和: %1").arg(cs, 2, 16, QChar('0')).toUpper();
  if (cs != calcCs) {
    csDesc +=
        QString(" [错误, 期望: %1]").arg(calcCs, 2, 16, QChar('0')).toUpper();
  } else {
    csDesc += " [正确]";
  }
  fields.append(
      {"校验和", QString::asprintf("%02X", cs), csDesc, offset++, 1, 0});

  if (offset < data.size()) {
    uint8_t stop = (uint8_t)data[offset];
    if (stop == PROT101_STOP_BYTE) {
      fields.append({"结束字符", "16", "帧结束", offset++, 1, 0});
    } else {
      fields.append({"错误", QString::asprintf("%02X", stop),
                     "无效结束字符(期望16)", offset++, 1, 0});
    }
  }
}

void Prot101Parser::parseVariableFrame(const QByteArray &data,
                                       QList<Prot101Field> &fields) {
  // 68 L L 68 C A ASDU CS 16
  if (data.size() < 4) {
    fields.append({"错误", "", "可变帧头长度不足", 0, 0, 0});
    return;
  }

  int offset = 0;
  fields.append({"启动字符", "68", "可变帧起始", offset++, 1, 0});

  uint8_t len1 = (uint8_t)data[offset];
  fields.append({"长度L", QString::asprintf("%02X", len1),
                 QString("长度: %1").arg(len1), offset++, 1, 0});

  uint8_t len2 = (uint8_t)data[offset];
  QString len2Desc = QString("重复长度: %1").arg(len2);
  if (len1 != len2)
    len2Desc += " [不匹配]";
  fields.append({"长度L(重复)", QString::asprintf("%02X", len2), len2Desc,
                 offset++, 1, 0});

  uint8_t start2 = (uint8_t)data[offset];
  if (start2 == PROT101_VAR_START) {
    fields.append({"启动字符(重复)", "68", "可变帧起始确认", offset++, 1, 0});
  } else {
    fields.append({"错误", QString::asprintf("%02X", start2),
                   "无效启动字符(期望68)", offset++, 1, 0});
    return;
  }

  // 检查总长度
  // L = C + A + ASDU
  // Frame = 68 L L 68 [L bytes] CS 16
  // Total = 4 + L + 2
  if (data.size() < 4 + len1 + 2) {
    fields.append({"错误", "",
                   QString("数据不完整:期望%1字节,实际%2字节")
                       .arg(4 + len1 + 2)
                       .arg(data.size()),
                   offset, 0, 0});
    return;
  }

  int dataStart = offset;

  uint8_t ctrl = (uint8_t)data[offset];
  parseControlField(ctrl, offset, fields, 0);
  offset++;

  parseLinkAddress(data, offset, fields, 0);

  // ASDU
  // ASDU length = L - 1(C) - LinkAddrLen
  int asduLen = len1 - 1 - m_linkAddrLen;
  if (asduLen > 0) {
    parseASDU(data, offset, fields, 0);
  }

  // Checksum
  // CS = Sum(C + A + ASDU) % 256
  uint8_t calcCs = 0;
  for (int i = dataStart; i < offset; ++i) {
    calcCs += (uint8_t)data[i];
  }

  if (offset < data.size()) {
    uint8_t cs = (uint8_t)data[offset];
    QString csDesc = QString("校验和: %1").arg(cs, 2, 16, QChar('0')).toUpper();
    if (cs != calcCs) {
      csDesc +=
          QString(" [错误, 期望: %1]").arg(calcCs, 2, 16, QChar('0')).toUpper();
    } else {
      csDesc += " [正确]";
    }
    fields.append(
        {"校验和", QString::asprintf("%02X", cs), csDesc, offset++, 1, 0});
  }

  if (offset < data.size()) {
    uint8_t stop = (uint8_t)data[offset];
    if (stop == PROT101_STOP_BYTE) {
      fields.append({"结束字符", "16", "帧结束", offset++, 1, 0});
    } else {
      fields.append({"错误", QString::asprintf("%02X", stop),
                     "无效结束字符(期望16)", offset++, 1, 0});
    }
  }
}

void Prot101Parser::parseSingleChar(const QByteArray &data,
                                    QList<Prot101Field> &fields) {
  fields.append({"单字符", "E5", "肯定确认", 0, 1, 0});
}

void Prot101Parser::parseControlField(uint8_t ctrl, int offset,
                                      QList<Prot101Field> &fields, int indent) {
  Prot101Control c;
  c.val = ctrl;

  QString desc = QString("DIR=%1, PRM=%1, FCB=%2, FCV=%3, FUNC=%4")
                     .arg(c.bits.dir)
                     .arg(c.bits.prm)
                     .arg(c.bits.fcb_acd)
                     .arg(c.bits.fcv_dfc)
                     .arg(c.bits.func);

  QString funcName = getFunctionCodeName(c.bits.func, c.bits.prm);
  desc += QString(" (%1)").arg(funcName);

  fields.append(
      {"控制域C", QString::asprintf("%02X", ctrl), desc, offset, 1, indent});
}

void Prot101Parser::parseLinkAddress(const QByteArray &data, int &offset,
                                     QList<Prot101Field> &fields, int indent) {
  if (offset + m_linkAddrLen > data.size()) {
    fields.append({"错误", "", "链路地址长度不足", offset, 0, indent});
    return;
  }

  uint16_t addr = 0;
  if (m_linkAddrLen == 1) {
    addr = (uint8_t)data[offset];
  } else if (m_linkAddrLen == 2) {
    addr = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  }

  fields.append({"链路地址A", data.mid(offset, m_linkAddrLen).toHex().toUpper(),
                 QString("地址: %1").arg(addr), offset, m_linkAddrLen, indent});
  offset += m_linkAddrLen;
}

void Prot101Parser::parseASDU(const QByteArray &data, int &offset,
                              QList<Prot101Field> &fields, int indent) {
  if (offset + 2 > data.size()) { // 至少TypeID + VSQ
    fields.append({"错误", "", "ASDU头数据不足", offset, 0, indent});
    return;
  }

  // 类型标识
  uint8_t typeId = (uint8_t)data[offset];
  QString typeIdName = getTypeIdName(typeId);
  fields.append({"类型标识", QString::asprintf("%02X", typeId),
                 QString("Type: %1 - %2").arg(typeId).arg(typeIdName), offset++,
                 1, indent + 1});

  // 可变结构限定词 VSQ
  uint8_t vsq = (uint8_t)data[offset];
  bool sq = (vsq >> 7) & 0x01;
  int num = vsq & 0x7F;

  QString vsqDesc = QString("SQ:%1(%2), 数量:%3")
                        .arg(sq)
                        .arg(sq ? "连续" : "不连续")
                        .arg(num);
  fields.append({"可变结构限定词", QString::asprintf("%02X", vsq), vsqDesc,
                 offset++, 1, indent + 1});

  // 传送原因 COT
  if (offset + m_cotLen > data.size()) {
    fields.append({"错误", "", "传送原因数据不足", offset, 0, indent + 1});
    return;
  }

  uint8_t cotByte = (uint8_t)data[offset];
  uint8_t cot = cotByte & 0x3F;
  bool test = (cotByte >> 7) & 0x01;
  bool pn = (cotByte >> 6) & 0x01;

  QString cotName = getCauseOfTransmissionName(cot);
  QString cotDesc = QString("原因:%1 - %2").arg(cot).arg(cotName);
  if (test)
    cotDesc += " [测试]";
  if (pn)
    cotDesc += " [肯定/否定]";

  if (m_cotLen == 2) {
    uint8_t origAddr = (uint8_t)data[offset + 1];
    cotDesc += QString(", 源地址:%1").arg(origAddr);
  }

  fields.append({"传送原因", data.mid(offset, m_cotLen).toHex().toUpper(),
                 cotDesc, offset, m_cotLen, indent + 1});
  offset += m_cotLen;

  // 公共地址
  if (offset + m_commonAddrLen > data.size()) {
    fields.append({"错误", "", "公共地址数据不足", offset, 0, indent + 1});
    return;
  }

  uint16_t commonAddr = 0;
  if (m_commonAddrLen == 1) {
    commonAddr = (uint8_t)data[offset];
  } else {
    commonAddr = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  }

  fields.append({"公共地址",
                 data.mid(offset, m_commonAddrLen).toHex().toUpper(),
                 QString("ASDU地址: %1").arg(commonAddr), offset,
                 m_commonAddrLen, indent + 1});
  offset += m_commonAddrLen;

  // 解析信息对象
  parseInformationObject(data, offset, fields, indent + 1, typeId, sq, num);
}

void Prot101Parser::parseInformationObject(const QByteArray &data, int &offset,
                                           QList<Prot101Field> &fields,
                                           int indent, uint8_t typeId, bool sq,
                                           int num) {
  if (num <= 0 || num > 127) {
    fields.append({"警告", "", QString("信息对象数量异常: %1").arg(num), offset,
                   0, indent});
    return;
  }

  uint32_t ioa = 0;

  for (int i = 0; i < num; ++i) {
    if (offset >= data.size())
      break;

    QString objHeader = QString("信息对象 [%1/%2]").arg(i + 1).arg(num);

    // 信息体地址
    if (!sq || i == 0) {
      if (offset + m_ioaLen > data.size()) {
        fields.append({"错误", "", "信息对象地址数据不足", offset, 0, indent});
        return;
      }

      ioa = 0;
      for (int j = 0; j < m_ioaLen; ++j) {
        ioa |= ((uint8_t)data[offset + j] << (j * 8));
      }

      fields.append({objHeader + " - 信息体地址",
                     data.mid(offset, m_ioaLen).toHex().toUpper(),
                     QString("地址: %1").arg(ioa), offset, m_ioaLen, indent});
      offset += m_ioaLen;
    } else {
      ioa++;
      fields.append({objHeader + " - 信息体地址", QString("(连续)"),
                     QString("地址: %1").arg(ioa), offset, 0, indent});
    }

    // 根据类型标识解析信息元素
    // 这里只实现部分常用类型，其他类型可根据需要添加
    switch (static_cast<Prot101TypeId>(typeId)) {
    case Prot101TypeId::M_SP_NA_1: // 单点信息
    case Prot101TypeId::M_SP_TA_1: // 带时标的单点信息
    case Prot101TypeId::M_SP_TB_1: // 带CP56Time2a时标的单点信息
      if (offset < data.size()) {
        uint8_t siq = (uint8_t)data[offset];
        fields.append({objHeader + " - SIQ", QString::asprintf("%02X", siq),
                       getSIQDescription(siq), offset++, 1, indent + 1});

        if (typeId == (uint8_t)Prot101TypeId::M_SP_TA_1) {
          QString timeStr = parseCP24Time2a(data, offset);
          fields.append({objHeader + " - CP24Time2a",
                         data.mid(offset - 3, 3).toHex().toUpper(), timeStr,
                         offset - 3, 3, indent + 1});
        } else if (typeId == (uint8_t)Prot101TypeId::M_SP_TB_1) {
          QString timeStr = parseCP56Time2a(data, offset);
          fields.append({objHeader + " - CP56Time2a",
                         data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                         offset - 7, 7, indent + 1});
        }
      }
      break;

    case Prot101TypeId::M_DP_NA_1: // 双点信息
    case Prot101TypeId::M_DP_TA_1:
    case Prot101TypeId::M_DP_TB_1:
      if (offset < data.size()) {
        uint8_t diq = (uint8_t)data[offset];
        fields.append({objHeader + " - DIQ", QString::asprintf("%02X", diq),
                       getDIQDescription(diq), offset++, 1, indent + 1});

        if (typeId == (uint8_t)Prot101TypeId::M_DP_TA_1) {
          QString timeStr = parseCP24Time2a(data, offset);
          fields.append({objHeader + " - CP24Time2a",
                         data.mid(offset - 3, 3).toHex().toUpper(), timeStr,
                         offset - 3, 3, indent + 1});
        } else if (typeId == (uint8_t)Prot101TypeId::M_DP_TB_1) {
          QString timeStr = parseCP56Time2a(data, offset);
          fields.append({objHeader + " - CP56Time2a",
                         data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                         offset - 7, 7, indent + 1});
        }
      }
      break;

    case Prot101TypeId::M_ME_NA_1: // 测量值, 规一化值
    case Prot101TypeId::M_ME_TA_1:
    case Prot101TypeId::M_ME_TD_1:
    case Prot101TypeId::M_ME_ND_1: // 不带品质
      if (offset + 2 <= data.size()) {
        int16_t nva =
            (int16_t)((uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8));
        double normalized = nva / 32768.0;
        fields.append(
            {objHeader + " - 规一化值", data.mid(offset, 2).toHex().toUpper(),
             QString("值: %1 (归一化: %2)").arg(nva).arg(normalized, 0, 'f', 4),
             offset, 2, indent + 1});
        offset += 2;

        if (typeId != (uint8_t)Prot101TypeId::M_ME_ND_1 &&
            offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         getQDSDescription(qds), offset++, 1, indent + 1});
        }

        if (typeId == (uint8_t)Prot101TypeId::M_ME_TA_1) {
          QString timeStr = parseCP24Time2a(data, offset);
          fields.append({objHeader + " - CP24Time2a",
                         data.mid(offset - 3, 3).toHex().toUpper(), timeStr,
                         offset - 3, 3, indent + 1});
        } else if (typeId == (uint8_t)Prot101TypeId::M_ME_TD_1) {
          QString timeStr = parseCP56Time2a(data, offset);
          fields.append({objHeader + " - CP56Time2a",
                         data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                         offset - 7, 7, indent + 1});
        }
      }
      break;

    case Prot101TypeId::M_ME_NB_1: // 测量值, 标度化值
    case Prot101TypeId::M_ME_TB_1:
    case Prot101TypeId::M_ME_TE_1:
      if (offset + 2 <= data.size()) {
        int16_t sva =
            (int16_t)((uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8));
        fields.append({objHeader + " - 标度化值",
                       data.mid(offset, 2).toHex().toUpper(),
                       QString("值: %1").arg(sva), offset, 2, indent + 1});
        offset += 2;

        if (offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         getQDSDescription(qds), offset++, 1, indent + 1});
        }

        if (typeId == (uint8_t)Prot101TypeId::M_ME_TB_1) {
          QString timeStr = parseCP24Time2a(data, offset);
          fields.append({objHeader + " - CP24Time2a",
                         data.mid(offset - 3, 3).toHex().toUpper(), timeStr,
                         offset - 3, 3, indent + 1});
        } else if (typeId == (uint8_t)Prot101TypeId::M_ME_TE_1) {
          QString timeStr = parseCP56Time2a(data, offset);
          fields.append({objHeader + " - CP56Time2a",
                         data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                         offset - 7, 7, indent + 1});
        }
      }
      break;

    case Prot101TypeId::M_ME_NC_1: // 测量值, 短浮点数
    case Prot101TypeId::M_ME_TC_1:
    case Prot101TypeId::M_ME_TF_1:
      if (offset + 4 <= data.size()) {
        float val;
        memcpy(&val, data.constData() + offset, 4);
        fields.append(
            {objHeader + " - 短浮点数", data.mid(offset, 4).toHex().toUpper(),
             QString("值: %1").arg(val, 0, 'f', 2), offset, 4, indent + 1});
        offset += 4;

        if (offset < data.size()) {
          uint8_t qds = (uint8_t)data[offset];
          fields.append({objHeader + " - QDS", QString::asprintf("%02X", qds),
                         getQDSDescription(qds), offset++, 1, indent + 1});
        }

        if (typeId == (uint8_t)Prot101TypeId::M_ME_TC_1) {
          QString timeStr = parseCP24Time2a(data, offset);
          fields.append({objHeader + " - CP24Time2a",
                         data.mid(offset - 3, 3).toHex().toUpper(), timeStr,
                         offset - 3, 3, indent + 1});
        } else if (typeId == (uint8_t)Prot101TypeId::M_ME_TF_1) {
          QString timeStr = parseCP56Time2a(data, offset);
          fields.append({objHeader + " - CP56Time2a",
                         data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                         offset - 7, 7, indent + 1});
        }
      }
      break;

    case Prot101TypeId::C_SC_NA_1: // 单命令
      if (offset < data.size()) {
        uint8_t sco = (uint8_t)data[offset];
        fields.append({objHeader + " - SCO", QString::asprintf("%02X", sco),
                       getSCODescription(sco), offset++, 1, indent + 1});
      }
      break;

    case Prot101TypeId::C_IC_NA_1: // 总召唤
      if (offset < data.size()) {
        uint8_t qoi = (uint8_t)data[offset];
        fields.append({objHeader + " - QOI", QString::asprintf("%02X", qoi),
                       QString("召唤限定词: %1").arg(qoi), offset++, 1,
                       indent + 1});
      }
      break;

    case Prot101TypeId::C_CS_NA_1: // 时钟同步
      if (offset + 7 <= data.size()) {
        QString timeStr = parseCP56Time2a(data, offset);
        fields.append({objHeader + " - CP56Time2a",
                       data.mid(offset - 7, 7).toHex().toUpper(), timeStr,
                       offset - 7, 7, indent + 1});
      }
      break;

    default:
      fields.append({objHeader, "", "未实现的类型解析", offset, 0, indent});
      // 无法继续解析后续对象，因为不知道长度
      return;
    }
  }
}

QString Prot101Parser::parseCP24Time2a(const QByteArray &data, int &offset) {
  if (offset + 3 > data.size())
    return "数据不足";

  uint16_t ms = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  uint8_t min = (uint8_t)data[offset + 2] & 0x3F;
  bool iv = (data[offset + 2] >> 7) & 0x01;

  offset += 3;

  return QString("%1分 %2毫秒%3").arg(min).arg(ms).arg(iv ? " [无效]" : "");
}

QString Prot101Parser::parseCP56Time2a(const QByteArray &data, int &offset) {
  if (offset + 7 > data.size())
    return "数据不足";

  uint16_t ms = (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
  uint8_t min = (uint8_t)data[offset + 2] & 0x3F;
  uint8_t hour = (uint8_t)data[offset + 3] & 0x1F;
  uint8_t day = (uint8_t)data[offset + 4] & 0x1F;
  uint8_t month = (uint8_t)data[offset + 5] & 0x0F;
  uint8_t year = (uint8_t)data[offset + 6] & 0x7F;

  offset += 7;

  return QString("20%1-%2-%3 %4:%5:%6.%7")
      .arg(year, 2, 10, QChar('0'))
      .arg(month, 2, 10, QChar('0'))
      .arg(day, 2, 10, QChar('0'))
      .arg(hour, 2, 10, QChar('0'))
      .arg(min, 2, 10, QChar('0'))
      .arg(ms / 1000, 2, 10, QChar('0'))
      .arg(ms % 1000, 3, 10, QChar('0'));
}

QString Prot101Parser::getTypeIdName(uint8_t typeId) {
  switch (static_cast<Prot101TypeId>(typeId)) {
  case Prot101TypeId::M_SP_NA_1:
    return "单点信息";
  case Prot101TypeId::M_SP_TA_1:
    return "带时标的单点信息";
  case Prot101TypeId::M_DP_NA_1:
    return "双点信息";
  case Prot101TypeId::M_DP_TA_1:
    return "带时标的双点信息";
  case Prot101TypeId::M_ST_NA_1:
    return "步位置信息";
  case Prot101TypeId::M_ST_TA_1:
    return "带时标的步位置信息";
  case Prot101TypeId::M_BO_NA_1:
    return "32比特串";
  case Prot101TypeId::M_BO_TA_1:
    return "带时标的32比特串";
  case Prot101TypeId::M_ME_NA_1:
    return "测量值, 规一化值";
  case Prot101TypeId::M_ME_TA_1:
    return "带时标的测量值, 规一化值";
  case Prot101TypeId::M_ME_NB_1:
    return "测量值, 标度化值";
  case Prot101TypeId::M_ME_TB_1:
    return "带时标的测量值, 标度化值";
  case Prot101TypeId::M_ME_NC_1:
    return "测量值, 短浮点数";
  case Prot101TypeId::M_ME_TC_1:
    return "带时标的测量值, 短浮点数";
  case Prot101TypeId::M_IT_NA_1:
    return "累计量";
  case Prot101TypeId::M_IT_TA_1:
    return "带时标的累计量";
  case Prot101TypeId::M_EP_TA_1:
    return "带时标的继电保护设备事件";
  case Prot101TypeId::M_EP_TB_1:
    return "带时标的继电保护设备成组启动事件";
  case Prot101TypeId::M_EP_TC_1:
    return "带时标的继电保护设备成组输出电路信息";
  case Prot101TypeId::M_PS_NA_1:
    return "具有状态变位检出的成组单点信息";
  case Prot101TypeId::M_ME_ND_1:
    return "测量值, 不带品质描述的规一化值";
  case Prot101TypeId::M_SP_TB_1:
    return "带CP56Time2a时标的单点信息";
  case Prot101TypeId::M_DP_TB_1:
    return "带CP56Time2a时标的双点信息";
  case Prot101TypeId::M_ST_TB_1:
    return "带CP56Time2a时标的步位置信息";
  case Prot101TypeId::M_BO_TB_1:
    return "带CP56Time2a时标的32比特串";
  case Prot101TypeId::M_ME_TD_1:
    return "带CP56Time2a时标的测量值, 规一化值";
  case Prot101TypeId::M_ME_TE_1:
    return "带CP56Time2a时标的测量值, 标度化值";
  case Prot101TypeId::M_ME_TF_1:
    return "带CP56Time2a时标的测量值, 短浮点数";
  case Prot101TypeId::M_IT_TB_1:
    return "带CP56Time2a时标的累计量";
  case Prot101TypeId::M_EP_TD_1:
    return "带CP56Time2a时标的继电保护设备事件";
  case Prot101TypeId::M_EP_TE_1:
    return "带CP56Time2a时标的继电保护设备成组启动事件";
  case Prot101TypeId::M_EP_TF_1:
    return "带CP56Time2a时标的继电保护设备成组输出电路信息";
  case Prot101TypeId::C_SC_NA_1:
    return "单命令";
  case Prot101TypeId::C_DC_NA_1:
    return "双命令";
  case Prot101TypeId::C_RC_NA_1:
    return "步调节命令";
  case Prot101TypeId::C_SE_NA_1:
    return "设定值命令, 规一化值";
  case Prot101TypeId::C_SE_NB_1:
    return "设定值命令, 标度化值";
  case Prot101TypeId::C_SE_NC_1:
    return "设定值命令, 短浮点数";
  case Prot101TypeId::C_BO_NA_1:
    return "32比特串命令";
  case Prot101TypeId::C_IC_NA_1:
    return "总召唤";
  case Prot101TypeId::C_CI_NA_1:
    return "电能脉冲召唤";
  case Prot101TypeId::C_RD_NA_1:
    return "读命令";
  case Prot101TypeId::C_CS_NA_1:
    return "时钟同步命令";
  case Prot101TypeId::C_TS_NA_1:
    return "测试命令";
  case Prot101TypeId::C_RP_NA_1:
    return "复位进程命令";
  case Prot101TypeId::C_CD_NA_1:
    return "延时获得命令";
  default:
    return "未知类型";
  }
}

QString Prot101Parser::getCauseOfTransmissionName(uint8_t cot) {
  switch (static_cast<Prot101CauseOfTransmission>(cot)) {
  case Prot101CauseOfTransmission::COT_PER_CYC:
    return "周期、循环";
  case Prot101CauseOfTransmission::COT_BACK:
    return "背景扫描";
  case Prot101CauseOfTransmission::COT_SPONT:
    return "突发(自发)";
  case Prot101CauseOfTransmission::COT_INIT:
    return "初始化";
  case Prot101CauseOfTransmission::COT_REQ:
    return "请求或被请求";
  case Prot101CauseOfTransmission::COT_ACT:
    return "激活";
  case Prot101CauseOfTransmission::COT_ACT_CON:
    return "激活确认";
  case Prot101CauseOfTransmission::COT_DEACT:
    return "停止激活";
  case Prot101CauseOfTransmission::COT_DEACT_CON:
    return "停止激活确认";
  case Prot101CauseOfTransmission::COT_ACT_TERM:
    return "激活终止";
  case Prot101CauseOfTransmission::COT_RETREM:
    return "远方命令引起的返送信息";
  case Prot101CauseOfTransmission::COT_RETLOC:
    return "当地命令引起的返送信息";
  case Prot101CauseOfTransmission::COT_FILE:
    return "文件传输";
  case Prot101CauseOfTransmission::COT_INTRO:
    return "响应站召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_1:
    return "响应第1组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_2:
    return "响应第2组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_3:
    return "响应第3组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_4:
    return "响应第4组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_5:
    return "响应第5组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_6:
    return "响应第6组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_7:
    return "响应第7组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_8:
    return "响应第8组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_9:
    return "响应第9组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_10:
    return "响应第10组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_11:
    return "响应第11组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_12:
    return "响应第12组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_13:
    return "响应第13组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_14:
    return "响应第14组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_15:
    return "响应第15组召唤";
  case Prot101CauseOfTransmission::COT_REQ_GRP_16:
    return "响应第16组召唤";
  case Prot101CauseOfTransmission::COT_REQ_COG:
    return "响应计数量总召唤";
  default:
    return "未知原因";
  }
}

QString Prot101Parser::getFunctionCodeName(uint8_t func, bool prm) {
  if (prm) { // 启动站
    switch (static_cast<Prot101FunctionCode>(func)) {
    case Prot101FunctionCode::FC_RESET_LINK:
      return "复位远方链路";
    case Prot101FunctionCode::FC_RESET_USER:
      return "复位用户进程";
    case Prot101FunctionCode::FC_TEST_LINK:
      return "发送测试链路功能";
    case Prot101FunctionCode::FC_USER_DATA_CONF:
      return "发送需要确认的用户数据";
    case Prot101FunctionCode::FC_USER_DATA_NO_CONF:
      return "发送不需要确认的用户数据";
    case Prot101FunctionCode::FC_REQ_ACCESS:
      return "请求访问需求";
    case Prot101FunctionCode::FC_REQ_STATUS:
      return "请求链路状态";
    case Prot101FunctionCode::FC_REQ_USER_DATA_1:
      return "请求1级用户数据";
    case Prot101FunctionCode::FC_REQ_USER_DATA_2:
      return "请求2级用户数据";
    default:
      return "未知功能码";
    }
  } else { // 从动站
    switch (static_cast<Prot101FunctionCode>(func)) {
    case Prot101FunctionCode::FC_ACK:
      return "认可: 肯定认可";
    case Prot101FunctionCode::FC_NACK:
      return "认可: 否定认可";
    case Prot101FunctionCode::FC_USER_DATA:
      return "响应: 用户数据";
    case Prot101FunctionCode::FC_NACK_NO_DATA:
      return "响应: 无所请求的用户数据";
    case Prot101FunctionCode::FC_STATUS_LINK:
      return "响应: 链路状态或访问需求";
    default:
      return "未知功能码";
    }
  }
}

QString Prot101Parser::getSIQDescription(uint8_t siq) {
  QStringList list;
  if (siq & 0x01)
    list << "开";
  else
    list << "合"; // SPI
  if (siq & 0x10)
    list << "被闭锁"; // BL
  if (siq & 0x20)
    list << "被取代"; // SB
  if (siq & 0x40)
    list << "非当前值"; // NT
  if (siq & 0x80)
    list << "无效"; // IV
  return list.join(", ");
}

QString Prot101Parser::getDIQDescription(uint8_t diq) {
  QStringList list;
  uint8_t dpi = diq & 0x03;
  switch (dpi) {
  case 0:
    list << "中间状态";
    break;
  case 1:
    list << "分";
    break;
  case 2:
    list << "合";
    break;
  case 3:
    list << "不确定状态";
    break;
  }
  if (diq & 0x10)
    list << "被闭锁";
  if (diq & 0x20)
    list << "被取代";
  if (diq & 0x40)
    list << "非当前值";
  if (diq & 0x80)
    list << "无效";
  return list.join(", ");
}

QString Prot101Parser::getQDSDescription(uint8_t qds) {
  QStringList list;
  if (qds & 0x01)
    list << "溢出"; // OV
  if (qds & 0x10)
    list << "被闭锁"; // BL
  if (qds & 0x20)
    list << "被取代"; // SB
  if (qds & 0x40)
    list << "非当前值"; // NT
  if (qds & 0x80)
    list << "无效"; // IV
  return list.join(", ");
}

QString Prot101Parser::getSCODescription(uint8_t sco) {
  QStringList list;
  if (sco & 0x01)
    list << "合";
  else
    list << "分"; // SCS
  uint8_t qu = (sco >> 2) & 0x1F;
  list << QString("限定词:%1").arg(qu);
  if (sco & 0x80)
    list << "选择";
  else
    list << "执行"; // S/E
  return list.join(", ");
}
