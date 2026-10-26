#include "Prot698Parser.h"
#include "Prot698OadManager.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtEndian>
#include <QtMath>
#include <cmath>
#include <cstring>
#include <QDate>
#include <QTime>
#include <QScopedValueRollback>
#include <cstdint>

using uint8_t = quint8;
using uint16_t = quint16;
using int32_t = qint32;
using uint32_t = quint32;
using int8_t = qint8;
using int16_t = qint16;
using int64_t = qint64;
using uint64_t = quint64;

Prot698Parser::Prot698Parser() {
}

// 静态方法：检查 698 帧是否有效
// 简化校验：只验证 68开头、16结尾、长度匹配、FCS正确
bool Prot698Parser::isValidFrame(const QByteArray &data) {
  // Minimal length: 68 L L C ... FCS(2) 16
  if (data.size() < 13)
    return false;

  // 1. Start Byte
  if ((quint8)data[0] != 0x68)
    return false;

  // 2. Length Field (Little Endian)
  // 长度域 L 是指整个帧的字节数，不包括起始字符和结束字符。
  // 整个帧长度 = 1(Start) + L + 1(End) = L + 2
  quint16 rawLen = (quint8)data[1] | ((quint8)data[2] << 8);
  // 长度域只有低14位有效，高2位保留
  int lenVal = rawLen & 0x3FFF;
  if ((rawLen & 0xc000) || lenVal < 11) return false;
  const int addressLength = (static_cast<quint8>(data[4]) & 0xf) + 1;
  const int hcsOffset = 6 + addressLength;
  if (hcsOffset + 2 >= lenVal - 1) return false;

  // 关键修改：在日志解析场景中，如果数据长度小于期望长度，
  // 说明帧不完整，应该拒绝并触发滑动窗口继续寻找有效帧
  if (data.size() < lenVal + 2)
    return false;

  // 3. End Byte - 重要修改：检查计算出的位置，而非数据末尾
  // 结束字节应该在 offset = lenVal + 1 的位置（0-indexed）
  // 例如：lenVal=33，则结束字节在offset 34（第35个字节）
  if ((quint8)data[lenVal + 1] != 0x16)
    return false;

  const auto bytes = reinterpret_cast<const quint8 *>(data.constData());
  if ((bytes[hcsOffset] | bytes[hcsOffset + 1] << 8) != calcCRC16(bytes + 1, hcsOffset - 1)) return false;

  // 4. FCS Validation
  // FCS is at End-3 (2 bytes).
  // FCS calculates CRC16 over [L, L, C, Addr, HCS, APDU]
  // Range: index 1 to lenVal - 2
  int fcsOffset = lenVal - 1; // FCS在结束字节之前2个字节的位置
  quint16 expectedFcs =
      calcCRC16((const quint8 *)data.constData() + 1, fcsOffset - 1);
  quint16 actualFcs =
      (quint8)data[fcsOffset] | ((quint8)data[fcsOffset + 1] << 8);

  if (expectedFcs != actualFcs)
    return false;

  return true;
}

ProtocolSummary Prot698Parser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;

  // Basic length check: 68 L L C SA(min 1+1) HCS(2) ...
  if (data.size() < 10)
    return summary;

  // 1. Direction (Control Field)
  uint8_t controlByte = static_cast<uint8_t>(data[3]);
  bool dir = (controlByte & 0x80); // Bit 7: 0=Client->Server, 1=Server->Client
  bool prm = (controlByte & 0x40); // Bit 6: 0=Responder, 1=Initiator
  bool split = (controlByte & 0x20);    // Bit 5: Split Flag
  bool scramble = (controlByte & 0x08); // Bit 3: Scramble Flag

  if (dir) {
    summary.direction = "S->C"; // Server to Client
  } else {
    summary.direction = "C->S"; // Client to Server
  }
  summary.direction += (prm ? "(主动)" : "(从动)");

  // 2. Address (SA)
  int offset = 4;
  uint8_t saInd = data[offset];
  int addrLen = (saInd & 0x0F) + 1;

  if (data.size() >= offset + 1 + addrLen) {
    QByteArray addr = data.mid(offset + 1, addrLen);
    summary.address = addr.toHex().toUpper();
  }

  // 3. APDU / Message Type
  // Structure: 68 L L C SA(1+N) CA(1) HCS(2) [Split(2)] APDU ...
  int apduOffset = offset + 1 + addrLen + 1 + 2; // +1 for CA, +2 for HCS
  if (split) {
    apduOffset += 2; // Skip Split Info
  }

  // Check for Scrambled Frame
  // 扰码(Scramble)处理: bit3=1时，链路用户数据(APDU)按字节加33H
  // 解析时需要减去33H

  if (data.size() > apduOffset) {
    // Special handling: Security tags (16/144) might NOT be scrambled even if
    // the bit is set
    bool effectiveScramble = scramble;
    if (scramble) {
      uint8_t rawTag = data[apduOffset];
      if (rawTag == 16 || rawTag == 144) {
        effectiveScramble = false;
      }
    }

    // Helper to get APDU byte with scramble handling
    auto getApduByte = [&](int idx) -> uint8_t {
      if (idx >= data.size())
        return 0;
      uint8_t b = data[idx];
      return effectiveScramble ? (b - 0x33) : b;
    };

    uint8_t tag = getApduByte(apduOffset);
    QString typeName;
    int piidOffset = -1;

    switch (tag) {
    case 1:
      typeName = "预连接请求(LINK-Request)";
      piidOffset = 1;
      break;
    case 129:
      typeName = "预连接响应(LINK-Response)";
      piidOffset = 1;
      break;
    case 2:
      typeName = "建立应用连接请求(CONNECT-Request)";
      piidOffset = 1;
      break;
    case 130:
      typeName = "建立应用连接响应(CONNECT-Response)";
      piidOffset = 1;
      break;
    case 3:
      typeName = "断开应用连接请求(RELEASE-Request)";
      piidOffset = 1;
      break;
    case 131:
      typeName = "断开应用连接响应(RELEASE-Response)";
      piidOffset = 1;
      break;
    case 132:
      typeName = "断开应用连接通知(RELEASE-Notification)";
      piidOffset = 1;
      break;

    case 5: { // GET-Request
      typeName = "读取请求(GET-Request)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-读取一个(Normal)";
          break;
        case 2:
          typeName += "-读取若干(NormalList)";
          break;
        case 3:
          typeName += "-读取记录(Record)";
          break;
        case 4:
          typeName += "-读取若干记录(RecordList)";
          break;
        case 5:
          typeName += "-读取分帧(Next)";
          break;
        case 6:
          typeName += "-读取MD5(MD5)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 133: { // GET-Response
      typeName = "读取响应(GET-Response)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-读取一个(Normal)";
          break;
        case 2:
          typeName += "-读取若干(NormalList)";
          break;
        case 3:
          typeName += "-读取记录(Record)";
          break;
        case 4:
          typeName += "-读取若干记录(RecordList)";
          break;
        case 5:
          typeName += "-读取分帧(Next)";
          break;
        case 6:
          typeName += "-读取MD5(MD5)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 6: { // SET-Request
      typeName = "设置请求(SET-Request)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-设置一个(Normal)";
          break;
        case 2:
          typeName += "-设置若干(NormalList)";
          break;
        case 3:
          typeName += "-设置后读取(ThenGet)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 134: { // SET-Response
      typeName = "设置响应(SET-Response)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-设置一个(Normal)";
          break;
        case 2:
          typeName += "-设置若干(NormalList)";
          break;
        case 3:
          typeName += "-设置后读取(ThenGet)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 7: { // ACTION-Request
      typeName = "操作请求(ACTION-Request)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-操作一个(Normal)";
          break;
        case 2:
          typeName += "-操作若干(NormalList)";
          break;
        case 3:
          typeName += "-操作后读取(ThenGet)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 135: { // ACTION-Response
      typeName = "操作响应(ACTION-Response)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-操作一个(Normal)";
          break;
        case 2:
          typeName += "-操作若干(NormalList)";
          break;
        case 3:
          typeName += "-操作后读取(ThenGet)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 8: { // REPORT-Response
      typeName = "上报响应(REPORT-Response)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-上报一个(Normal)";
          break;
        case 2:
          typeName += "-上报若干(NormalList)";
          break;
        case 3:
          typeName += "-上报记录(Record)";
          break;
        case 4:
          typeName += "-上报若干记录(RecordList)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 136: { // REPORT-Notification
      typeName = "上报通知(REPORT-Notification)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-上报一个(Normal)";
          break;
        case 2:
          typeName += "-上报若干(NormalList)";
          break;
        case 3:
          typeName += "-上报记录(Record)";
          break;
        case 4:
          typeName += "-上报若干记录(RecordList)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 9: { // PROXY-Request
      typeName = "代理请求(PROXY-Request)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-代理一个(Normal)";
          break;
        case 2:
          typeName += "-代理若干(NormalList)";
          break;
        case 3:
          typeName += "-代理记录(Record)";
          break;
        case 4:
          typeName += "-代理若干记录(RecordList)";
          break;
        case 5:
          typeName += "-代理分帧(Next)";
          break;
        case 6:
          typeName += "-代理MD5(MD5)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 137: { // PROXY-Response
      typeName = "代理响应(PROXY-Response)";
      if (data.size() > apduOffset + 1) {
        uint8_t choice = getApduByte(apduOffset + 1);
        switch (choice) {
        case 1:
          typeName += "-代理一个(Normal)";
          break;
        case 2:
          typeName += "-代理若干(NormalList)";
          break;
        case 3:
          typeName += "-代理记录(Record)";
          break;
        case 4:
          typeName += "-代理若干记录(RecordList)";
          break;
        case 5:
          typeName += "-代理分帧(Next)";
          break;
        case 6:
          typeName += "-代理MD5(MD5)";
          break;
        }
      }
      piidOffset = 2;
      break;
    }
    case 16: { // SECURITY-Request
      typeName = "安全请求(SECURITY-Request)";
      // User requirement: No sub-service byte for encrypted frames
      piidOffset = -1; // No PIID at standard offset
      break;
    }
    case 144: { // SECURITY-Response
      typeName = "安全响应(SECURITY-Response)";
      // User requirement: No sub-service byte for encrypted frames
      piidOffset = -1; // No PIID at standard offset
      break;
    }
    case 110:
      typeName = "异常响应(ERROR-Response)";
      piidOffset = 1;
      break;

    default:
      typeName = QString("未知APDU(Tag:%1)").arg(tag);
      break;
    }

    summary.messageType = typeName;

    // 4. Frame Sequence (PIID)
    if (piidOffset > 0 && data.size() > apduOffset + piidOffset) {
      uint8_t piid = getApduByte(apduOffset + piidOffset);
      summary.frameSeq = QString::number(piid);
    }
  }

  return summary;
}

quint16 Prot698Parser::calcCRC16(const quint8 *data, int len) {
  // PPP FCS (RFC 1662) Polynomial: x^16 + x^12 + x^5 + 1 (0x8408 reversed)
  // Initial value: 0xFFFF
  // Final XOR: 0xFFFF
  quint16 crc = 0xFFFF;
  for (int i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0x8408;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc ^ 0xFFFF;
}

QString Prot698Parser::getFuncCodeDesc(quint8 funcCode) {
  switch (funcCode) {
  case 0:
    return "保留";
  case 1:
    return "确认/否认";
  case 2:
    return "链路请求"; // 实际上是 1=确认, 2=请求?
                       // 表2 功能码定义: 0=保留, 1=确认, 2=再次确认(Link
                       // Response?), 3=通知(Link Request?) 让我们看图: 表2:
                       // 0=保留, 1=确认, 2=再次确认, 3=通知? 实际上标准是: 1:
                       // 确认 (Confirm) 2: 链路响应 (Response) ? No, user code
                       // says "CF_LINK_MANAGER" 3: 链路请求 (Request) ? User
                       // code: 0=请求, 1=确认, 2=再次确认, 3=通知
  case 3:
    return "通知";
  default:
    return "未知";
  }
}

QString Prot698Parser::getDataTypeName(Prot698DataType type) {
  switch (type) {
  case Prot698DataType::Null:
    return "NULL";
  case Prot698DataType::Array:
    return "array";
  case Prot698DataType::Structure:
    return "structure";
  case Prot698DataType::Bool:
    return "bool";
  case Prot698DataType::BitString:
    return "bit-string";
  case Prot698DataType::DoubleLong:
    return "double-long";
  case Prot698DataType::DoubleLongUnsigned:
    return "double-long-unsigned";
  case Prot698DataType::OctetString:
    return "octet-string";
  case Prot698DataType::VisibleString:
    return "visible-string";
  case Prot698DataType::Utf8String:
    return "UTF8-string";
  case Prot698DataType::Integer:
    return "integer";
  case Prot698DataType::Long:
    return "long";
  case Prot698DataType::Unsigned:
    return "unsigned";
  case Prot698DataType::LongUnsigned:
    return "long-unsigned";
  case Prot698DataType::Long64:
    return "long64";
  case Prot698DataType::Long64Unsigned:
    return "long64-unsigned";
  case Prot698DataType::Enum:
    return "enum";
  case Prot698DataType::Float32:
    return "float32";
  case Prot698DataType::Float64:
    return "float64";
  case Prot698DataType::DateTime:
    return "date_time";
  case Prot698DataType::Date:
    return "date";
  case Prot698DataType::Time:
    return "time";
  case Prot698DataType::DateTimeS:
    return "date_time_s";
  case Prot698DataType::OI:
    return "OI";
  case Prot698DataType::OAD:
    return "OAD";
  case Prot698DataType::ROAD:
    return "ROAD";
  case Prot698DataType::OMD:
    return "OMD";
  case Prot698DataType::TI:
    return "TI";
  case Prot698DataType::TSA:
    return "TSA";
  case Prot698DataType::MAC:
    return "MAC";
  case Prot698DataType::RN:
    return "RN";
  case Prot698DataType::Region:
    return "Region";
  case Prot698DataType::ScalerUnit:
    return "Scaler_Unit";
  case Prot698DataType::RSD:
    return "RSD";
  case Prot698DataType::CSD:
    return "CSD";
  case Prot698DataType::MS:
    return "MS";
  case Prot698DataType::SID:
    return "SID";
  case Prot698DataType::SID_MAC:
    return "SID_MAC";
  case Prot698DataType::COMDCB:
    return "COMDCB";
  case Prot698DataType::RCSD:
    return "RCSD";
  case Prot698DataType::VQDS:
    return "VQDS";
  case Prot698DataType::Checksum:
    return "Checksum";
  default:
    return QString("Unknown(%1)").arg((int)type);
  }
}

int Prot698Parser::decodeLength(const QByteArray &data, int &offset) {
  if (offset >= data.size())
    return 0;
  uint8_t byte = (uint8_t)data[offset++];
  if (byte < 0x80) {
    return byte;
  } else {
    int numBytes = byte & 0x7F;
    int len = 0;
    for (int i = 0; i < numBytes; ++i) {
      if (offset >= data.size())
        return 0;
      len = (len << 8) | (uint8_t)data[offset++];
    }
    return len;
  }
}

void Prot698Parser::parseData(const QByteArray &data, int &offset,
                              QList<Prot698Field> &fields, int indent,
                              const Prot698SubItem *def) {
  if (offset >= data.size())
    return;

  if (m_dataDepth >= 32) {
    fields.append({"Error", "", "Data递归深度超过限制", offset, 0, indent});
    offset = data.size(); return;
  }
  QScopedValueRollback<int> depthGuard(m_dataDepth, m_dataDepth + 1);
  int initialFieldCount = fields.size();

  uint8_t tag = (uint8_t)data[offset];
  Prot698DataType type = (Prot698DataType)tag;

  QString typeName = getDataTypeName(type);
  QString desc = "Data Type Tag";
  if (def) {
    desc += " (" + def->name + ")";
  }

  fields.append(Prot698Field(typeName, QString::asprintf("%02X", tag), desc,
                             offset, 1, indent));
  offset++;

  switch (type) {
  case Prot698DataType::Null:
    // No value
    // User requirement: 00 means NULL, data does not exist.
    if (!fields.isEmpty()) {
      fields.last().description = "NULL (空数据)";
    }
    break;
  case Prot698DataType::Array:
  case Prot698DataType::Structure:
    if (offset < data.size()) {
      uint8_t count = (uint8_t)data[offset];

      // Merge count into the Tag field
      if (!fields.isEmpty()) {
        Prot698Field &tagField = fields.last();
        tagField.value += " " + QString::asprintf("%02X", count);
        tagField.length += 1;
        tagField.description += QString(", Count: %1").arg(count);
      }

      offset++;
      for (int i = 0; i < count; ++i) {
        const Prot698SubItem *childDef = nullptr;
        if (def && i < def->children.size()) {
          childDef = &def->children[i];
        }
        // For Array, usually children definition is repeated or single
        // definition for all items? In 698 XML, Array usually has one child
        // SubItem defining the element type. However, for ROAD, we use
        // "structure" type in definition but data might be "array" tag. So we
        // only force single definition if the definition type is also "array".
        if (type == Prot698DataType::Array && def && !def->children.isEmpty()) {
          if (def->type == "array") {
            childDef = &def->children[0];
          }
        }

        parseData(data, offset, fields, indent + 1, childDef);
      }
    }
    break;
  case Prot698DataType::Bool:
    if (offset < data.size()) {
      uint8_t val = data[offset];
      fields.append(Prot698Field("Value", QString::asprintf("%02X", val),
                                 val ? "true" : "false", offset, 1,
                                 indent + 1));
      offset++;
    }
    break;
  case Prot698DataType::BitString: {
    int bitLen = decodeLength(data, offset);
    int byteLen = (bitLen + 7) / 8;
    if (offset + byteLen <= data.size()) {
      QByteArray val = data.mid(offset, byteLen);
      QString bitsStr;
      // Convert to bit string for display if short, else hex
      if (bitLen <= 64) {
        for (int i = 0; i < bitLen; ++i) {
          int byteIdx = i / 8;
          int bitIdx = 7 - (i % 8);
          bitsStr += ((val[byteIdx] >> bitIdx) & 1) ? '1' : '0';
          if ((i + 1) % 8 == 0 && i != bitLen - 1)
            bitsStr += ' ';
        }
      } else {
        bitsStr = val.toHex().toUpper();
      }
      fields.append(Prot698Field(
          "Value", QString(val.toHex().toUpper()),
          QString("BitString(%1 bits): %2").arg(bitLen).arg(bitsStr), offset,
          byteLen, indent + 1));
      offset += byteLen;
    }
  } break;
  case Prot698DataType::DoubleLong:
    if (offset + 4 <= data.size()) {
      int32_t val = qFromBigEndian<int32_t>(data.mid(offset, 4).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 4).toHex().toUpper()),
                                 valStr, offset, 4, indent + 1));
      offset += 4;
    }
    break;
  case Prot698DataType::DoubleLongUnsigned:
    if (offset + 4 <= data.size()) {
      uint32_t val = qFromBigEndian<uint32_t>(data.mid(offset, 4).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 4).toHex().toUpper()),
                                 valStr, offset, 4, indent + 1));
      offset += 4;
    }
    break;
  case Prot698DataType::OctetString:
  case Prot698DataType::Utf8String: {
    int len = decodeLength(data, offset);
    if (offset + len <= data.size()) {
      QByteArray val = data.mid(offset, len);
      QString display = (type == Prot698DataType::Utf8String)
                            ? QString::fromUtf8(val)
                            : QString(val.toHex().toUpper());
      fields.append(Prot698Field("Value", QString(val.toHex().toUpper()),
                                 display, offset, len, indent + 1));
      offset += len;
    }
  } break;
  case Prot698DataType::VisibleString: {
    int len = decodeLength(data, offset);
    if (offset + len <= data.size()) {
      QByteArray val = data.mid(offset, len);
      // VisibleString is ASCII
      QString display = QString::fromLatin1(val);
      fields.append(Prot698Field("Value", QString(val.toHex().toUpper()),
                                 display, offset, len, indent + 1));
      offset += len;
    }
  } break;
  case Prot698DataType::Integer:
    if (offset < data.size()) {
      int8_t val = (int8_t)data[offset];
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString::asprintf("%02X", (uint8_t)val),
                                 valStr, offset, 1, indent + 1));
      offset++;
    }
    break;
  case Prot698DataType::Long:
    if (offset + 2 <= data.size()) {
      int16_t val = qFromBigEndian<int16_t>(data.mid(offset, 2).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 valStr, offset, 2, indent + 1));
      offset += 2;
    }
    break;
  case Prot698DataType::Unsigned:
  case Prot698DataType::Enum:
    if (offset < data.size()) {
      uint8_t val = data[offset];
      QString valStr;
      if (type == Prot698DataType::Enum && def &&
          def->enumValues.contains(val)) {
        valStr = QString::number(val) + " (" + def->enumValues[val] + ")";
      } else {
        if (def && def->scaler != 0) {
          double dVal = val * qPow(10.0, def->scaler);
          int decimals = (def->scaler < 0) ? -def->scaler : 0;
          valStr = QString::number(dVal, 'f', decimals);
        } else {
          valStr = QString::number(val);
        }
        if (def && !def->unit.isEmpty()) {
          valStr += " " + def->unit;
        }
      }
      fields.append(Prot698Field("Value", QString::asprintf("%02X", val),
                                 valStr, offset, 1, indent + 1));
      offset++;
    }
    break;
  case Prot698DataType::LongUnsigned:
    if (offset + 2 <= data.size()) {
      uint16_t val = qFromBigEndian<uint16_t>(data.mid(offset, 2).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 valStr, offset, 2, indent + 1));
      offset += 2;
    }
    break;
  case Prot698DataType::Long64:
    if (offset + 8 <= data.size()) {
      int64_t val = qFromBigEndian<int64_t>(data.mid(offset, 8).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 8).toHex().toUpper()),
                                 valStr, offset, 8, indent + 1));
      offset += 8;
    }
    break;
  case Prot698DataType::Long64Unsigned:
    if (offset + 8 <= data.size()) {
      uint64_t val = qFromBigEndian<uint64_t>(data.mid(offset, 8).constData());
      QString valStr;
      if (def && def->scaler != 0) {
        double dVal = val * qPow(10.0, def->scaler);
        int decimals = (def->scaler < 0) ? -def->scaler : 0;
        valStr = QString::number(dVal, 'f', decimals);
      } else {
        valStr = QString::number(val);
      }
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value",
                                 QString(data.mid(offset, 8).toHex().toUpper()),
                                 valStr, offset, 8, indent + 1));
      offset += 8;
    }
    break;
  case Prot698DataType::Float32:
    if (offset + 4 <= data.size()) {
      float val = 0;
      const quint32 bits = qFromBigEndian<quint32>(data.constData() + offset);
      memcpy(&val, &bits, 4);
      QString valStr = QString::number(val);
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value", data.mid(offset, 4).toHex().toUpper(),
                                 valStr, offset, 4, indent + 1));
      offset += 4;
    }
    break;
  case Prot698DataType::Float64:
    if (offset + 8 <= data.size()) {
      double val = 0;
      const quint64 bits = qFromBigEndian<quint64>(data.constData() + offset);
      memcpy(&val, &bits, 8);
      QString valStr = QString::number(val);
      if (def && !def->unit.isEmpty()) {
        valStr += " " + def->unit;
      }
      fields.append(Prot698Field("Value", data.mid(offset, 8).toHex().toUpper(),
                                 valStr, offset, 8, indent + 1));
      offset += 8;
    }
    break;
  case Prot698DataType::OAD:
    if (offset + 4 <= data.size()) {
      // OI(2) + Attrib(1) + Index(1)
      parseOAD(data, offset, fields, indent + 1);
    }
    break;
  case Prot698DataType::Date:
    if (offset + 5 <= data.size()) {
      // Year(2) Month(1) Day(1) Week(1)
      uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      uint8_t month = data[offset + 2];
      uint8_t day = data[offset + 3];
      uint8_t week = data[offset + 4];
      fields.append(Prot698Field(
          "Value", data.mid(offset, 5).toHex().toUpper(),
          QString::asprintf("%04d-%02d-%02d W%d", year, month, day, week),
          offset, 5, indent + 1));
      offset += 5;
    }
    break;
  case Prot698DataType::Time:
    if (offset + 3 <= data.size()) {
      // Hour(1) Min(1) Sec(1)
      uint8_t h = data[offset];
      uint8_t m = data[offset + 1];
      uint8_t s = data[offset + 2];
      fields.append(Prot698Field("Value", data.mid(offset, 3).toHex().toUpper(),
                                 QString::asprintf("%02d:%02d:%02d", h, m, s),
                                 offset, 3, indent + 1));
      offset += 3;
    }
    break;
  case Prot698DataType::DateTime:
    if (offset + 10 <= data.size()) {
      // Year(2) Month(1) Day(1) Week(1) Hour(1) Min(1) Sec(1) MS(2)
      uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      uint8_t month = data[offset + 2];
      uint8_t day = data[offset + 3];
      uint8_t week = data[offset + 4];
      uint8_t h = data[offset + 5];
      uint8_t m = data[offset + 6];
      uint8_t s = data[offset + 7];
      uint16_t ms =
          ((uint8_t)data[offset + 8] << 8) | (uint8_t)data[offset + 9];
      fields.append(Prot698Field(
          "Value", data.mid(offset, 10).toHex().toUpper(),
          QString::asprintf("%04d-%02d-%02d W%d %02d:%02d:%02d.%03d", year,
                            month, day, week, h, m, s, ms),
          offset, 10, indent + 1));
      offset += 10;
    }
    break;
  case Prot698DataType::DateTimeS:
    if (offset + 7 <= data.size()) {
      // Year(2) Month(1) Day(1) Hour(1) Min(1) Sec(1)
      uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      uint8_t month = data[offset + 2];
      uint8_t day = data[offset + 3];
      uint8_t h = data[offset + 4];
      uint8_t m = data[offset + 5];
      uint8_t s = data[offset + 6];

      QString valStr = QString::asprintf("%04d-%02d-%02d %02d:%02d:%02d", year,
                                         month, day, h, m, s);

      // If we have a definition (e.g. from OAD), append it to the description
      if (def) {
        // For DateTimeS, usually no unit, but maybe description
        // valStr += " (" + def->name + ")"; // Already in desc
      }

      fields.append(Prot698Field("Value", data.mid(offset, 7).toHex().toUpper(),
                                 valStr, offset, 7, indent + 1));
      offset += 7;
    }
    break;
  case Prot698DataType::OI:
    if (offset + 2 <= data.size()) {
      uint16_t oi = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("Value", data.mid(offset, 2).toHex().toUpper(),
                                 QString::asprintf("%04X", oi), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    break;
  case Prot698DataType::TI:
    if (offset + 3 <= data.size()) {
      uint8_t unit = data[offset];
      uint16_t val =
          ((uint8_t)data[offset + 1] << 8) | (uint8_t)data[offset + 2];
      QString unitStr;
      switch (unit) {
      case 0:
        unitStr = "秒";
        break;
      case 1:
        unitStr = "分";
        break;
      case 2:
        unitStr = "时";
        break;
      case 3:
        unitStr = "日";
        break;
      case 4:
        unitStr = "月";
        break;
      case 5:
        unitStr = "年";
        break;
      default:
        unitStr = "未知";
        break;
      }
      fields.append(Prot698Field("Value", data.mid(offset, 3).toHex().toUpper(),
                                 QString("%1 %2").arg(val).arg(unitStr), offset,
                                 3, indent + 1));
      offset += 3;
    }
    break;
  case Prot698DataType::TSA:
    parseTSA(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::MAC:
  case Prot698DataType::RN: {
    int len = decodeLength(data, offset);
    if (offset + len <= data.size()) {
      QByteArray val = data.mid(offset, len);
      fields.append(Prot698Field("Value", QString(val.toHex().toUpper()),
                                 "OctetString", offset, len, indent + 1));
      offset += len;
    }
  } break;
  case Prot698DataType::Region:
    parseRegion(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::ScalerUnit:
    if (offset + 2 <= data.size()) {
      int8_t scaler = (int8_t)data[offset];
      uint8_t unit = (uint8_t)data[offset + 1];
      fields.append(
          Prot698Field("换算", QString::asprintf("%02X", (uint8_t)scaler),
                       QString::number(scaler), offset, 1, indent + 1));
      fields.append(Prot698Field("单位", QString::asprintf("%02X", unit),
                                 QString::number(unit), offset + 1, 1,
                                 indent + 1));
      offset += 2;
    }
    break;
  case Prot698DataType::RSD:
    parseRSD(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::CSD:
    parseCSD(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::MS:
    parseMS(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::SID:
    if (offset + 4 <= data.size()) {
      uint32_t id = qFromBigEndian<uint32_t>(data.mid(offset, 4).constData());
      fields.append(Prot698Field("标识", data.mid(offset, 4).toHex().toUpper(),
                                 QString::number(id), offset, 4, indent + 1));
      offset += 4;

      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        QByteArray val = data.mid(offset, len);
        fields.append(Prot698Field("附加数据", QString(val.toHex().toUpper()),
                                   "OctetString", offset, len, indent + 1));
        offset += len;
      }
    }
    break;
  case Prot698DataType::SID_MAC:
    // SID
    if (offset + 4 <= data.size()) {
      uint32_t id = qFromBigEndian<uint32_t>(data.mid(offset, 4).constData());
      fields.append(Prot698Field("标识(SID)",
                                 data.mid(offset, 4).toHex().toUpper(),
                                 QString::number(id), offset, 4, indent + 1));
      offset += 4;

      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        QByteArray val = data.mid(offset, len);
        fields.append(Prot698Field("附加数据(SID)",
                                   QString(val.toHex().toUpper()),
                                   "OctetString", offset, len, indent + 1));
        offset += len;
      }
    }
    // MAC
    {
      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        QByteArray val = data.mid(offset, len);
        fields.append(Prot698Field("消息鉴别码(MAC)",
                                   QString(val.toHex().toUpper()),
                                   "OctetString", offset, len, indent + 1));
        offset += len;
      }
    }
    break;
  case Prot698DataType::COMDCB:
    parseCOMDCB(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::RCSD:
    parseRCSD(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::VQDS:
    // Value Data
    parseData(data, offset, fields, indent + 1);
    // QDS bit-string(8)
    if (offset < data.size()) {
      uint8_t qds = data[offset];
      QString desc = QString("QDS: %1").arg(qds, 2, 16, QChar('0'));
      if ((qds & 0x01) == 0)
        desc += " (有效)";
      else
        desc += " (无效)";
      fields.append(Prot698Field("QDS", QString::asprintf("%02X", qds), desc,
                                 offset, 1, indent + 1));
      offset++;
    }
    break;
  case Prot698DataType::Checksum:
    if (offset < data.size()) {
      uint8_t type = data[offset];
      QString typeStr;
      switch (type) {
      case 0:
        typeStr = "CRC校验(默认)";
        break;
      case 1:
        typeStr = "md5校验";
        break;
      case 2:
        typeStr = "SHA1校验";
        break;
      case 3:
        typeStr = "SM3校验";
        break;
      default:
        typeStr = "其他";
        break;
      }
      fields.append(Prot698Field("校验类型", QString::asprintf("%02X", type),
                                 typeStr, offset, 1, indent + 1));
      offset++;

      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        QByteArray val = data.mid(offset, len);
        fields.append(Prot698Field("校验值", QString(val.toHex().toUpper()),
                                   "OctetString", offset, len, indent + 1));
        offset += len;
      }
    }
    break;
  case Prot698DataType::ROAD:
    parseROAD(data, offset, fields, indent + 1);
    break;
  case Prot698DataType::OMD:
    parseOMD(data, offset, fields, indent + 1);
    break;
  default:
    fields.append(Prot698Field("Value", "...", "Not Implemented Yet", offset, 0,
                               indent + 1));
    break;
  }

  // Merge Tag and Value fields for simple types
  if (type != Prot698DataType::Array && type != Prot698DataType::Structure) {
    if (fields.size() == initialFieldCount + 2) {
      Prot698Field valField = fields.takeLast(); // Remove value field
      Prot698Field &tagField = fields.last();    // Access tag field (now last)

      tagField.value += " " + valField.value;
      tagField.length += valField.length;

      // Use value description
      QString newDesc = valField.description;
      if (def) {
        // If the value desc doesn't already contain the name (some might),
        // append it. Usually value desc is just the value. But if
        // valField.description is "2016-..." and def->name is "日期时间", we
        // want "2016-... (日期时间)"
        newDesc += " (" + def->name + ")";
      }
      tagField.description = newDesc;
      tagField.summary = valField.summary.isEmpty() ? valField.description : valField.summary;
      tagField.detail = typeName;
      if (!valField.detail.isEmpty()) tagField.detail += " · " + valField.detail;
      if (def && !def->name.isEmpty()) tagField.detail += " · " + def->name;
    }
  }
}

QString Prot698Parser::getApduTagName(Prot698ApduTag tag) {
  switch (tag) {
  case Prot698ApduTag::LinkRequest:
    return "LINK-Request";
  case Prot698ApduTag::ConnectRequest:
    return "CONNECT-Request";
  case Prot698ApduTag::ReleaseRequest:
    return "RELEASE-Request";
  case Prot698ApduTag::GetRequest:
    return "GET-Request";
  case Prot698ApduTag::SetRequest:
    return "SET-Request";
  case Prot698ApduTag::ActionRequest:
    return "ACTION-Request";
  case Prot698ApduTag::ReportResponse:
    return "REPORT-Response";
  case Prot698ApduTag::ProxyRequest:
    return "PROXY-Request";
  case Prot698ApduTag::SecurityRequest:
    return "SECURITY-Request";
  case Prot698ApduTag::CompactGetRequest:
    return "COMPACT-GET-Request";
  case Prot698ApduTag::CompactSetRequest:
    return "COMPACT-SET-Request";
  case Prot698ApduTag::CompactProxyRequest:
    return "COMPACT-PROXY-Request";
  case Prot698ApduTag::ErrorResponse:
    return "ERROR-Response";
  case Prot698ApduTag::LinkResponse:
    return "LINK-Response";
  case Prot698ApduTag::ConnectResponse:
    return "CONNECT-Response";
  case Prot698ApduTag::ReleaseResponse:
    return "RELEASE-Response";
  case Prot698ApduTag::ReleaseNotification:
    return "RELEASE-Notification";
  case Prot698ApduTag::GetResponse:
    return "GET-Response";
  case Prot698ApduTag::SetResponse:
    return "SET-Response";
  case Prot698ApduTag::ActionResponse:
    return "ACTION-Response";
  case Prot698ApduTag::ReportNotification:
    return "REPORT-Notification";
  case Prot698ApduTag::ProxyResponse:
    return "PROXY-Response";
  case Prot698ApduTag::SecurityResponse:
    return "SECURITY-Response";
  case Prot698ApduTag::CompactGetResponse:
    return "COMPACT-GET-Response";
  case Prot698ApduTag::CompactSetResponse:
    return "COMPACT-SET-Response";
  case Prot698ApduTag::CompactProxyResponse:
    return "COMPACT-PROXY-Response";
  case Prot698ApduTag::ServerErrorResponse:
    return "ERROR-Response";
  default:
    return QString("Unknown APDU(%1)").arg((int)tag);
  }
}

void Prot698Parser::parseApdu(const QByteArray &data, int &offset,
                              QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;

  uint8_t tagVal = (uint8_t)data[offset];
  Prot698ApduTag tag = (Prot698ApduTag)tagVal;

  fields.append(Prot698Field(getApduTagName(tag),
                             QString::asprintf("%02X", tagVal), "APDU Tag",
                             offset, 1, indent));
  offset++;

  switch (tag) {
  case Prot698ApduTag::LinkRequest: {
    // PIID-ACD (1 byte)
    if (offset < data.size()) {
      uint8_t piid = (uint8_t)data[offset];
      uint8_t serial = piid & 0x3F;
      uint8_t prio = (piid >> 6) & 0x01;
      uint8_t acd = (piid >> 7) & 0x01;
      QString desc = QString("序号:%1, 优先级:%2(%3), ACD:%4(%5)")
                         .arg(serial)
                         .arg(prio)
                         .arg(prio ? "高" : "一般")
                         .arg(acd)
                         .arg(acd ? "请求" : "不请求");
      fields.append(Prot698Field("PIID-ACD", QString::asprintf("%02X", piid),
                                 desc, offset, 1, indent + 1));
      offset++;
    }
    // Request Type (Enum, 1 byte)
    if (offset < data.size()) {
      uint8_t reqType = (uint8_t)data[offset];
      QString reqDesc;
      switch (reqType) {
      case 0:
        reqDesc = "登录";
        break;
      case 1:
        reqDesc = "心跳";
        break;
      case 2:
        reqDesc = "退出登录";
        break;
      default:
        reqDesc = "Unknown";
        break;
      }
      fields.append(Prot698Field("请求类型", QString::asprintf("%02X", reqType),
                                 reqDesc, offset, 1, indent + 1));
      offset++;
    }
    // Heartbeat Cycle (LongUnsigned, 2 bytes)
    if (offset + 2 <= data.size()) {
      // Big Endian
      uint16_t cycle = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field(
          "心跳周期", QString(data.mid(offset, 2).toHex().toUpper()),
          QString("%1 单位: 秒").arg(cycle), offset, 2, indent + 1));
      offset += 2;
    }
    // Request Time (DateTime, 10 bytes)
    if (offset + 10 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTime(data, offset);
      fields.append(Prot698Field(
          "请求时间", QString(data.mid(startOffset, 10).toHex().toUpper()),
          timeStr, startOffset, 10, indent + 1));
    }
  } break;
  case Prot698ApduTag::LinkResponse: {
    // PIID (1 byte)
    if (offset < data.size()) {
      uint8_t piid = (uint8_t)data[offset];
      uint8_t serial = piid & 0x3F;
      uint8_t prio = (piid >> 6) & 0x01;
      // ACD bit is reserved or not used in Response PIID usually, but structure
      // is same
      QString desc = QString("序号:%1, 优先级:%2(%3)")
                         .arg(serial)
                         .arg(prio)
                         .arg(prio ? "高" : "一般");
      fields.append(Prot698Field("PIID", QString::asprintf("%02X", piid), desc,
                                 offset, 1, indent + 1));
      offset++;
    }
    // Result (BitString 8, 1 byte)
    if (offset < data.size()) {
      uint8_t resByte = (uint8_t)data[offset];
      bool clockCredible = (resByte >> 7) & 0x01;
      uint8_t resCode = resByte & 0x07; // bit0-2
      QString resStr;
      switch (resCode) {
      case 0:
        resStr = "成功";
        break;
      case 1:
        resStr = "地址重复";
        break;
      case 2:
        resStr = "非法设备";
        break;
      case 3:
        resStr = "容量不足";
        break;
      default:
        resStr = "保留";
        break;
      }
      QString desc = QString("结果: %1, 时钟: %2")
                         .arg(resStr)
                         .arg(clockCredible ? "可信" : "不可信");
      fields.append(Prot698Field("结果", QString::asprintf("%02X", resByte),
                                 desc, offset, 1, indent + 1));
      offset++;
    }
    // Request Time (DateTime, 10 bytes)
    if (offset + 10 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTime(data, offset);
      fields.append(Prot698Field(
          "请求时间", QString(data.mid(startOffset, 10).toHex().toUpper()),
          timeStr, startOffset, 10, indent + 1));
    }
    // Receive Time (DateTime, 10 bytes)
    if (offset + 10 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTime(data, offset);
      fields.append(Prot698Field(
          "收到时间", QString(data.mid(startOffset, 10).toHex().toUpper()),
          timeStr, startOffset, 10, indent + 1));
    }
    // Response Time (DateTime, 10 bytes)
    if (offset + 10 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTime(data, offset);
      fields.append(Prot698Field(
          "响应时间", QString(data.mid(startOffset, 10).toHex().toUpper()),
          timeStr, startOffset, 10, indent + 1));
    }
  } break;
  case Prot698ApduTag::ConnectRequest: {
    // PIID
    fields.append(Prot698Field("PIID", readPIID(data, offset), "...",
                               offset - 1, 1, indent + 1));

    // Expected Protocol Version (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t ver = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(
          Prot698Field("期望协议版本号", QString::asprintf("%04X", ver),
                       QString("Value: %1").arg(ver), offset, 2, indent + 1));
      offset += 2;
    }

    // Protocol Conformance
    parseProtocolConformance("期望协议一致性", data, offset, fields, indent + 1,
                             64);

    // Function Conformance
    parseFunctionConformance("期望功能一致性", data, offset, fields, indent + 1,
                             128);

    // Client Max Send (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("客户机发送帧最大尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Client Max Recv (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("客户机接收帧最大尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Client Max Window (1 byte)
    if (offset < data.size()) {
      uint8_t val = data[offset];
      fields.append(Prot698Field(
          "客户机接收帧最大窗口尺寸", QString::asprintf("%02X", val),
          QString("%1 个").arg(val), offset, 1, indent + 1));
      offset++;
    }
    // Client Max Process (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("客户机最大可处理帧尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Expected Timeout (4 bytes)
    if (offset + 4 <= data.size()) {
      uint32_t val =
          ((uint8_t)data[offset] << 24) | ((uint8_t)data[offset + 1] << 16) |
          ((uint8_t)data[offset + 2] << 8) | (uint8_t)data[offset + 3];
      fields.append(Prot698Field("期望的应用连接超时时间",
                                 QString(data.mid(offset, 4).toHex().toUpper()),
                                 QString("%1 秒").arg(val), offset, 4,
                                 indent + 1));
      offset += 4;
    }
    // Certification Request Object
    parseConnectMechanismInfo(data, offset, fields, indent + 1);

    // TimeTag (Optional, usually 1 byte if null)
    parseTimeTag(data, offset, fields, indent + 1);
  } break;
  case Prot698ApduTag::GetRequest:
    parseGetRequest(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::SetRequest:
    parseSetRequest(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::GetResponse:
    parseGetResponse(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::SetResponse:
    parseSetResponse(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ActionRequest:
    parseActionRequest(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ActionResponse:
    parseActionResponse(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ReportNotification:
    parseReportNotification(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ReportResponse:
    parseReportResponse(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ProxyRequest:
    parseProxyRequest(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ProxyResponse:
    parseProxyResponse(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::SecurityRequest:
    parseSecurityRequest(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::SecurityResponse:
    parseSecurityResponse(data, offset, fields, indent + 1);
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ConnectResponse: {
    // PIID-ACD
    if (offset < data.size()) {
      uint8_t piid = (uint8_t)data[offset];
      fields.append(Prot698Field("PIID-ACD", QString::asprintf("%02X", piid),
                                 "...", offset, 1, indent + 1));
      offset++;
    }
    // Factory Version (Sequence of VisibleString)
    const char *names[] = {"厂商代码",   "软件版本号",   "软件版本日期",
                           "硬件版本号", "硬件版本日期", "厂商扩展信息"};
    for (int i = 0; i < 6; ++i) {
      int len = decodeLength(data, offset);
      if (offset + len <= data.size()) {
        QString val = QString::fromLatin1(data.mid(offset, len));
        fields.append(Prot698Field(names[i], val, "VisibleString", offset, len,
                                   indent + 1));
        offset += len;
      }
    }

    // Negotiated Protocol Version (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t ver = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(
          Prot698Field("商定的协议版本号", QString::asprintf("%04X", ver),
                       QString("Value: %1").arg(ver), offset, 2, indent + 1));
      offset += 2;
    }

    // Negotiated Protocol Conformance
    parseProtocolConformance("商定的协议一致性", data, offset, fields,
                             indent + 1, 64);

    // Negotiated Function Conformance
    parseFunctionConformance("商定的功能一致性", data, offset, fields,
                             indent + 1, 128);

    // Server Max Send (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("服务器发送帧最大尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Server Max Recv (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("服务器接收帧最大尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Server Max Window (1 byte)
    if (offset < data.size()) {
      uint8_t val = data[offset];
      fields.append(Prot698Field(
          "服务器接收帧最大窗口尺寸", QString::asprintf("%02X", val),
          QString("%1 个").arg(val), offset, 1, indent + 1));
      offset++;
    }
    // Server Max Process (2 bytes)
    if (offset + 2 <= data.size()) {
      uint16_t val = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      fields.append(Prot698Field("服务器最大可处理帧尺寸",
                                 QString(data.mid(offset, 2).toHex().toUpper()),
                                 QString("%1 字节").arg(val), offset, 2,
                                 indent + 1));
      offset += 2;
    }
    // Negotiated Timeout (4 bytes)
    if (offset + 4 <= data.size()) {
      uint32_t val =
          ((uint8_t)data[offset] << 24) | ((uint8_t)data[offset + 1] << 16) |
          ((uint8_t)data[offset + 2] << 8) | (uint8_t)data[offset + 3];
      fields.append(Prot698Field("商定的应用连接超时时间",
                                 QString(data.mid(offset, 4).toHex().toUpper()),
                                 QString("%1 秒").arg(val), offset, 4,
                                 indent + 1));
      offset += 4;
    }
    // Connection Response Object
    fields.append(Prot698Field("连接响应对象", "...", "Remaining Data", offset,
                               (int)(data.size() - offset), indent + 1));
    // offset = data.size(); // Don't consume all, let FollowReport/TimeTag
    // parse But wait, ConnectResponse usually has FactoryVersion etc. The code
    // above already parsed them. The "Connection Response Object" is likely the
    // last part of ConnectResponse specific data. Actually, ConnectResponse
    // structure in 698.45: PIID-ACD, FactoryVersion, ProtocolVer, ProtocolConf,
    // FuncConf, MaxSend, MaxRecv, MaxWindow, MaxProcess, Timeout,
    // ConnectResponseObj. After ConnectResponseObj, we are back to Server-APDU
    // sequence. So we should parse FollowReport and TimeTag here. However,
    // "Connection Response Object" parsing is not fully implemented ("..."). If
    // we don't know the length of "Connection Response Object", we can't parse
    // what follows. Assuming "Connection Response Object" is a ResultRecord or
    // similar? For now, let's assume the previous code consumed it or we can't
    // proceed. The previous code did: fields.append(..., offset,
    // data.size()-offset ...); offset = data.size(); This consumes everything.
    // I should change this if I want to parse FollowReport. But without knowing
    // the structure of Connection Response Object, I can't skip it correctly.
    // Let's leave ConnectResponse as is for now, or try to parse FollowReport
    // if offset is not at end? The previous code forced offset = data.size(). I
    // will remove that force if I can. But "Connection Response Object" is
    // likely complex. Let's just add the calls, but if offset is already at
    // end, they will do nothing. I will NOT modify the offset=data.size() line
    // in this specific edit block to avoid breaking existing logic if it relies
    // on it. Wait, the user wants FollowReport for Server-APDU. ConnectResponse
    // is one of them. If I don't fix ConnectResponse, it won't work for it. But
    // I don't have the definition of "Connection Response Object". Let's look
    // at the code I am replacing. It ends with: fields.append({"连接响应对象",
    // "...", "Remaining Data", offset, (int)(data.size() - offset), indent +
    // 1}); offset = data.size();
    // }
    // break;

    // I will replace it to try to parse FollowReport/TimeTag IF I can.
    // But since I don't know how to parse "Connection Response Object", I can't
    // really continue. So I will skip adding FollowReport/TimeTag for
    // ConnectResponse for now, or just add it after the block? The user
    // screenshot shows ConnectResponse is [130]. I will add it but I know it
    // might not work if the previous part consumes all.

    // Actually, I'll skip ConnectResponse modification in this tool call to be
    // safe, or I should modify the "offset = data.size()" part. I'll skip
    // ConnectResponse for now and focus on others which are more standard. The
    // user screenshot lists many.
  }
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ReleaseRequest: {
    fields.append(Prot698Field("PIID", readPIID(data, offset), "...",
                               offset - 1, 1, indent + 1));
    parseTimeTag(data, offset, fields, indent + 1);
  } break;
  case Prot698ApduTag::ReleaseResponse: {
    // PIID-ACD
    if (offset < data.size()) {
      uint8_t piid = (uint8_t)data[offset];
      fields.append(Prot698Field("PIID-ACD", QString::asprintf("%02X", piid),
                                 "...", offset, 1, indent + 1));
      offset++;
    }
    // Result (Enum)
    if (offset < data.size()) {
      uint8_t res = data[offset];
      fields.append(Prot698Field("结果", QString::asprintf("%02X", res),
                                 res == 0 ? "成功" : "失败", offset, 1,
                                 indent + 1));
      offset++;
    }
  }
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  case Prot698ApduTag::ReleaseNotification: {
    // PIID-ACD
    if (offset < data.size()) {
      uint8_t piid = (uint8_t)data[offset];
      fields.append(Prot698Field("PIID-ACD", QString::asprintf("%02X", piid),
                                 "...", offset, 1, indent + 1));
      offset++;
    }
    // Connect Time (DateTimeS)
    if (offset + 7 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTimeS(data, offset);
      fields.append(
          Prot698Field("应用连接建立时间",
                       QString(data.mid(startOffset, 7).toHex().toUpper()),
                       timeStr, startOffset, 7, indent + 1));
    }
    // Current Time (DateTimeS)
    if (offset + 7 <= data.size()) {
      int startOffset = offset;
      QString timeStr = readDateTimeS(data, offset);
      fields.append(Prot698Field(
          "服务器当前时间", QString(data.mid(startOffset, 7).toHex().toUpper()),
          timeStr, startOffset, 7, indent + 1));
    }
  }
    parseFollowReport(data, offset, fields, indent + 1);
    parseTimeTag(data, offset, fields, indent + 1);
    break;
  default:
    // For now, just show the tag
    break;
  }
}

QString Prot698Parser::readDateTime(const QByteArray &data, int &offset) {
  if (offset + 10 > data.size())
    return "Error";
  // Big Endian for Year
  uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
  uint8_t month = data[offset + 2];
  uint8_t day = data[offset + 3];
  uint8_t week = data[offset + 4];
  uint8_t h = data[offset + 5];
  uint8_t m = data[offset + 6];
  uint8_t s = data[offset + 7];
  // Big Endian for MS
  uint16_t ms = ((uint8_t)data[offset + 8] << 8) | (uint8_t)data[offset + 9];
  offset += 10;
  return QString::asprintf("%04d-%02d-%02d %02d:%02d:%02d.%03d 周%d", year,
                           month, day, h, m, s, ms, week);
}

QString Prot698Parser::readDateTimeS(const QByteArray &data, int &offset) {
  if (offset + 7 > data.size())
    return "Error";
  // Big Endian for Year
  uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
  uint8_t month = data[offset + 2];
  uint8_t day = data[offset + 3];
  uint8_t h = data[offset + 4];
  uint8_t m = data[offset + 5];
  uint8_t s = data[offset + 6];
  offset += 7;
  return QString::asprintf("%04d-%02d-%02d %02d:%02d:%02d", year, month, day, h,
                           m, s);
}

QString Prot698Parser::readDate(const QByteArray &data, int &offset) {
  if (offset + 5 > data.size())
    return "Error";
  // Big Endian for Year
  uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
  uint8_t month = data[offset + 2];
  uint8_t day = data[offset + 3];
  uint8_t week = data[offset + 4];
  offset += 5;
  return QString::asprintf("%04d-%02d-%02d W%d", year, month, day, week);
}

QString Prot698Parser::readTime(const QByteArray &data, int &offset) {
  if (offset + 3 > data.size())
    return "Error";
  uint8_t h = data[offset];
  uint8_t m = data[offset + 1];
  uint8_t s = data[offset + 2];
  offset += 3;
  return QString::asprintf("%02d:%02d:%02d", h, m, s);
}

QString Prot698Parser::readPIID(const QByteArray &data, int &offset) {
  if (offset < data.size()) {
    uint8_t piid = (uint8_t)data[offset++];
    return QString::asprintf("%02X", piid);
  }
  return "";
}

void Prot698Parser::parseProtocolConformance(const QString &name,
                                             const QByteArray &data,
                                             int &offset,
                                             QList<Prot698Field> &fields,
                                             int indent, int fixedBitLen) {
  int bitLen = 0;
  int byteLen = 0;

  if (fixedBitLen > 0) {
    bitLen = fixedBitLen;
    byteLen = (bitLen + 7) / 8;
  } else {
    bitLen = decodeLength(data, offset);
    byteLen = (bitLen + 7) / 8;
  }

  if (offset + byteLen > data.size()) {
    fields.append(
        Prot698Field(name, "Error", "Not enough data", offset, 0, indent));
    return;
  }

  QByteArray bytes = data.mid(offset, byteLen);
  QString hex = bytes.toHex().toUpper();
  fields.append(Prot698Field(name, hex, QString("Bits: %1").arg(bitLen), offset,
                             byteLen, indent));

  static const QMap<int, QString> bitMap = {
      {0, "应用连接协商"},
      {1, "请求对象属性 Get Normal"},
      {2, "批量请求对象属性 Get With List"},
      {3, "请求记录型对象属性 Get Record"},
      {4, "代理请求对象属性 Get Proxy"},
      {5, "代理请求记录型对象属性 Get Proxy Record"},
      {6, "请求分帧后续帧 Get Subsequent Frame"},
      {7, "设置基本对象属性 Set Normal"},
      {8, "批量设置基本对象属性 Set With List"},
      {9, "设置后读取 Set With Get"},
      {10, "代理设置对象属性 Set Proxy"},
      {11, "代理设置后读取对象属性 Set Proxy With Get"},
      {12, "执行对象方法 Action Normal"},
      {13, "批量执行对象方法 Action With List"},
      {14, "执行方法后读取 Action Then Get"},
      {15, "代理执行对象方法 Action Proxy"},
      {16, "代理执行后读取 Action Proxy With Get"},
      {17, "事件主动上报 Active Event Report"},
      {18, "事件跟随上报"},
      {19, "事件请求访问位 ACD 上报"},
      {20, "分帧数据传输"},
      {31, "安全方式传输"},
      {32, "对象属性 ID 为 0 的读取访问"},
      {33, "对象属性 ID 为 0 的设置访问"}};

  for (int i = 0; i < bitLen; ++i) {
    int byteIdx = i / 8;
    int bitIdx = i % 8;
    if (byteIdx < bytes.size()) {
      if ((bytes[byteIdx] >> bitIdx) & 0x01) {
        QString desc = bitMap.value(i, QString("Bit %1").arg(i));
        fields.append(Prot698Field(QString("  Bit %1").arg(i), "1", desc,
                                   offset + byteIdx, 1, indent + 1));
      }
    }
  }
  offset += byteLen;
}

void Prot698Parser::parseFunctionConformance(const QString &name,
                                             const QByteArray &data,
                                             int &offset,
                                             QList<Prot698Field> &fields,
                                             int indent, int fixedBitLen) {
  int bitLen = 0;
  int byteLen = 0;

  if (fixedBitLen > 0) {
    bitLen = fixedBitLen;
    byteLen = (bitLen + 7) / 8;
  } else {
    bitLen = decodeLength(data, offset);
    byteLen = (bitLen + 7) / 8;
  }

  if (offset + byteLen > data.size()) {
    fields.append(
        Prot698Field(name, "Error", "Not enough data", offset, 0, indent));
    return;
  }

  QByteArray bytes = data.mid(offset, byteLen);
  QString hex = bytes.toHex().toUpper();
  fields.append(Prot698Field(name, hex, QString("Bits: %1").arg(bitLen), offset,
                             byteLen, indent));

  static const QMap<int, QString> bitMap = {
      {0, "电能量计量"},      {1, "双向有功计量"},  {2, "无功电能计量"},
      {3, "视在电能计量"},    {4, "有功需量"},      {5, "无功需量"},
      {6, "视在需量"},        {7, "复费率"},        {8, "阀控"},
      {9, "本地费控"},        {10, "远程费控"},     {11, "基波电能"},
      {12, "谐波电能"},       {13, "谐波含量"},     {14, "波形失真度"},
      {15, "多功能端子输出"}, {16, "事件记录"},     {17, "事件上报"},
      {18, "温度测量"},       {19, "状态量监测"},   {20, "以太网通信"},
      {21, "红外通信"},       {22, "蓝牙通信"},     {23, "多媒体采集"},
      {24, "保留"},           {25, "直流模拟量"},   {26, "弱电端子 12V 输出"},
      {27, "搜表"},           {28, "三相负载平衡"}, {29, "升级"}};

  for (int i = 0; i < bitLen; ++i) {
    int byteIdx = i / 8;
    int bitIdx = i % 8;
    if (byteIdx < bytes.size()) {
      if ((bytes[byteIdx] >> bitIdx) & 0x01) {
        QString desc = bitMap.value(i, QString("Bit %1").arg(i));
        fields.append(Prot698Field(QString("  Bit %1").arg(i), "1", desc,
                                   offset + byteIdx, 1, indent + 1));
      }
    }
  }
  offset += byteLen;
}

void Prot698Parser::parseConnectMechanismInfo(const QByteArray &data,
                                              int &offset,
                                              QList<Prot698Field> &fields,
                                              int indent) {
  if (offset >= data.size())
    return;

  uint8_t type = data[offset];
  QString typeStr;
  switch (type) {
  case 0:
    typeStr = "公共连接(0)";
    break;
  case 1:
    typeStr = "一般密码(1)";
    break;
  case 2:
    typeStr = "对称加密(2)";
    break;
  case 3:
    typeStr = "数字签名(3)";
    break;
  case 4:
    typeStr = "RSA(4)";
    break;
  default:
    typeStr = QString("Unknown(%1)").arg(type);
    break;
  }
  fields.append(Prot698Field("认证请求对象", QString::asprintf("%02X", type),
                             typeStr, offset, 1, indent));
  offset++;

  if (type == 0) {
    // [0] NullSecurity: NULL
    // No data
  } else if (type == 1) {
    // [1] PasswordSecurity: visible-string
    int len = decodeLength(data, offset);
    if (offset + len <= data.size()) {
      QString val = QString::fromLatin1(data.mid(offset, len));
      fields.append(
          Prot698Field("密码", QString(data.mid(offset, len).toHex().toUpper()),
                       QString("Value: %1").arg(val), offset, len, indent + 1));
      offset += len;
    }
  } else if (type == 2) {
    // [2] SymmetrySecurity: SEQUENCE { 密文1 octet-string, 客户机签名1
    // octet-string }

    // 密文1 octet-string
    int len1 = decodeLength(data, offset);
    if (offset + len1 <= data.size()) {
      fields.append(Prot698Field(
          "密文1", QString(data.mid(offset, len1).toHex().toUpper()),
          "OctetString", offset, len1, indent + 1));
      offset += len1;
    }

    // 客户机签名1 octet-string
    int len2 = decodeLength(data, offset);
    if (offset + len2 <= data.size()) {
      fields.append(Prot698Field(
          "客户机签名1", QString(data.mid(offset, len2).toHex().toUpper()),
          "OctetString", offset, len2, indent + 1));
      offset += len2;
    }
  } else if (type == 3) {
    // [3] SignatureSecurity: SEQUENCE { 密文2 octet-string, 客户机签名2
    // octet-string }

    // 密文2 octet-string
    int len1 = decodeLength(data, offset);
    if (offset + len1 <= data.size()) {
      fields.append(Prot698Field(
          "密文2", QString(data.mid(offset, len1).toHex().toUpper()),
          "OctetString", offset, len1, indent + 1));
      offset += len1;
    }

    // 客户机签名2 octet-string
    int len2 = decodeLength(data, offset);
    if (offset + len2 <= data.size()) {
      fields.append(Prot698Field(
          "客户机签名2", QString(data.mid(offset, len2).toHex().toUpper()),
          "OctetString", offset, len2, indent + 1));
      offset += len2;
    }
  } else if (type == 4) {
    // [4] RSAPublicKey: SEQUENCE { 模数 visible-string, 公钥 visible-string }

    // 模数 visible-string
    int len1 = decodeLength(data, offset);
    if (offset + len1 <= data.size()) {
      QString val = QString::fromLatin1(data.mid(offset, len1));
      fields.append(Prot698Field(
          "模数", QString(data.mid(offset, len1).toHex().toUpper()),
          QString("Value: %1").arg(val), offset, len1, indent + 1));
      offset += len1;
    }

    // 公钥 visible-string
    int len2 = decodeLength(data, offset);
    if (offset + len2 <= data.size()) {
      QString val = QString::fromLatin1(data.mid(offset, len2));
      fields.append(Prot698Field(
          "公钥", QString(data.mid(offset, len2).toHex().toUpper()),
          QString("Value: %1").arg(val), offset, len2, indent + 1));
      offset += len2;
    }
  }
}

void Prot698Parser::parseTimeTag(const QByteArray &data, int &offset,
                                 QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;

  uint8_t flag = data[offset];
  if (flag == 0) {
    fields.append(
        Prot698Field("时间标签", "00", "无时间标签", offset, 1, indent));
    offset++;
  } else if (flag == 1) {
    fields.append(
        Prot698Field("时间标签", "01", "有时间标签", offset, 1, indent));
    offset++;
    // Time (DateTimeS)
    int startOffset = offset;
    QString timeStr = readDateTimeS(data, offset);
    fields.append(Prot698Field(
        "  时间", QString(data.mid(startOffset, 7).toHex().toUpper()), timeStr,
        startOffset, 7, indent + 1));
    // TI (3 bytes)
    parseTI(data, offset, fields, indent + 1, "  允许传输延时时间");
  }
}

void Prot698Parser::parseFollowReport(const QByteArray &data, int &offset,
                                      QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;

  uint8_t flag = data[offset];
  if (flag == 0) {
    fields.append(
        Prot698Field("跟随上报信息域", "00", "无跟随上报", offset, 1, indent));
    offset++;
  } else if (flag == 1) {
    fields.append(
        Prot698Field("跟随上报信息域", "01", "有跟随上报", offset, 1, indent));
    offset++;

    if (offset >= data.size())
      return;
    uint8_t tag = data[offset];
    QString tagName;
    if (tag == 1)
      tagName = "若干个对象属性及数据 (SEQUENCE OF A-ResultNormal)";
    else if (tag == 2)
      tagName = "若干个记录型对象属性及数据 (SEQUENCE OF A-ResultRecord)";
    else
      tagName = QString("Unknown Tag %1").arg(tag);

    fields.append(Prot698Field("跟随上报类型", QString::asprintf("%02X", tag),
                               tagName, offset, 1, indent + 1));
    offset++;

    // SEQUENCE OF ...
    // Usually starts with a count?
    // 698 SEQUENCE OF usually has a count byte.
    if (offset < data.size()) {
      uint8_t count = data[offset];
      fields.append(Prot698Field("数量", QString::asprintf("%02X", count),
                                 QString::number(count), offset, 1,
                                 indent + 1));
      offset++;

      for (int i = 0; i < count; ++i) {
        if (tag == 1) {
          parseAResultNormal(data, offset, fields, indent + 2);
        } else if (tag == 2) {
          parseAResultRecord(data, offset, fields, indent + 2);
        }
      }
    }
  }
}

QList<Prot698Field> Prot698Parser::parse(const QByteArray &data) {
  QList<Prot698Field> fields;
  int offset = 0;
  const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data.constData());
  int totalLen = data.size();

  // 0. 帧扫描逻辑 (支持非68开头，或纯APDU)
  int foundFrameOffset = -1;
  for (int i = 0; i < totalLen; ++i) {
    if (ptr[i] == 0x68) {
      // 检查潜在的长度域
      if (i + 2 < totalLen) {
        uint16_t rawLen = ptr[i + 1] | (ptr[i + 2] << 8);
        int lenVal = rawLen & 0x3FFF;
        if ((rawLen & 0x4000))
          lenVal *= 1024; // Unit bit

        // 长度合法性检查 (最小帧 ~10 bytes?)
        // L includes full frame length? No, usually Length field value excludes
        // 68(1) and L(2) in some protocols, but in 698:
        // "长度域L由2字节组成...表示帧的长度" Standard says: Length L
        // represents the length of the frame. Usually L = count of bytes from
        // (and including) L to the end of frame? Or full frame? DL/T 698.45:
        // "长度域L...字节数...不包括起始字符(68H)和长度域(L)本身" or "include"?
        // Let's verify with the existing code:
        // fields.append({"长度域 L", ...
        // int frameLen = lenField.bits.len;
        // if (totalLen < frameLen + 2) ...
        // So the existing code expects total size >= frameLen + 2.
        // This implies frameLen is the size of Bytes FOLLOWING the Length
        // Field? Or frameLen + 2 (Start+LengthField?) -> No, Start(1) +
        // Length(2) = 3 bytes prefix. If exiting code checks `totalLen <
        // frameLen + 2`, and it has already consumed Start(1) + Length(2)
        // (offset=3). Wait, in existing code: offset=0 (Start), offset=1 (L1),
        // offset=2 (L2). offset += 2 (now 3). if (totalLen < frameLen + 2) ->
        // This check is slightly weird if frameLen is "bytes after L". Usually
        // in 698, L = Length of (Control + Address + ... + FCS + 16). So Total
        // Frame Size = 1 (68) + 2 (L) + L_Value. So if L_Value is frameLen,
        // then Total Size = frameLen + 3. Existing code: `if (totalLen <
        // frameLen + 2)` might be lenient or correct if L was read at offset 1.
        // Let's ignore existing check nuances and look for 16H at `i + frameLen
        // + 2`.

        // Standard 698.45: Length L is the number of frame bytes excluding
        // Start(68) and End(16). So Total Frame Size = L + 1(Start) + 1(End) =
        // L + 2. Last Byte Index = StartIndex + TotalSize - 1 = i + (L + 2) - 1
        // = i + L + 1.
        int expectedEndIndex = i + lenVal + 1;

        if (expectedEndIndex < totalLen) {
          if (ptr[expectedEndIndex] == 0x16) {
            foundFrameOffset = i;
            break; // Found a valid frame candidate
          }
        }
      }
    }
  }

  if (foundFrameOffset != -1) {
    // --- 找到完整帧 ---

    // 记录跳过的数据
    if (foundFrameOffset > 0) {
      fields.append(
          Prot698Field("Skipped Data",
                       QString(data.mid(0, foundFrameOffset).toHex().toUpper()),
                       "Ignored Prefix/Noise", 0, foundFrameOffset));
    }

    if (foundFrameOffset > 0) {
      const auto nested = parse(data.mid(foundFrameOffset));
      for (auto field : nested) { field.offset += foundFrameOffset; fields.append(field); }
      return fields;
    }
    offset = foundFrameOffset;

    // 1. 起始字符 (68H)
    fields.append(Prot698Field("起始字符", "68", "起始字符", offset, 1));
    offset++;

    // Continue with normal parsing...
  } else {
    // --- 未找到完整帧，尝试解析为 APDU ---
    // 尝试解析 APDU，假设从 offset 0 开始
    int apduOffset = 0;
    parseApdu(data, apduOffset, fields, 0); // Indent 0

    return fields;
  }

  // 2. 长度域 L
  if (offset + 2 > totalLen)
    return fields;
  Prot698Length lenField;
  lenField.val = ptr[offset] | (ptr[offset + 1] << 8);

  QString lenDesc = QString("长度: %1").arg(lenField.bits.len);
  if (lenField.bits.unit)
    lenDesc += " (KB)";
  else
    lenDesc += " 字节";

  fields.append(Prot698Field(
      "长度域 L", QString::asprintf("%04X", lenField.val), lenDesc, offset, 2));
  offset += 2;

  int frameLen = lenField.bits.len;
  if (lenField.bits.unit)
    frameLen *= 1024;

  // 检查缓冲区长度
  if (totalLen < frameLen + 2) {
    fields.append(Prot698Field("Error", "",
                               QString("数据不完整，需要 %1 字节，实际 %2 字节")
                                   .arg(frameLen + 2)
                                   .arg(totalLen),
                               0, 0));
  }

  // 3. 控制域 C
  if (offset + 1 > totalLen)
    return fields;
  Prot698Control ctrl;
  ctrl.val = ptr[offset];

  // 功能码定义（表3）
  QString funcDesc;
  switch (ctrl.bits.func) {
  case 0:
    funcDesc = "保留";
    break;
  case 1:
    funcDesc = "链路管理";
    break;
  case 2:
    funcDesc = "保留";
    break;
  case 3:
    funcDesc = "用户数据";
    break;
  default:
    funcDesc = "保留";
    break;
  }

  QString ctrlDesc =
      QString("DIR=%1(%2), PRM=%3(%4), 分帧=%5, 加扰=%6, 功能码=%7(%8)")
          .arg(ctrl.bits.dir)
          .arg(ctrl.bits.dir ? "服务器→客户机" : "客户机→服务器")
          .arg(ctrl.bits.prm)
          .arg(ctrl.bits.prm ? "启动站" : "从动站")
          .arg(ctrl.bits.split)
          .arg(ctrl.bits.scramble)
          .arg(ctrl.bits.func)
          .arg(funcDesc);

  fields.append(Prot698Field("控制域 C", QString::asprintf("%02X", ctrl.val),
                             ctrlDesc, offset, 1));
  fields.last().summary = QString("%1 · %2")
      .arg(ctrl.bits.dir ? "服务器→客户机" : "客户机→服务器", funcDesc);
  fields.last().detail = ctrlDesc;
  offset++;

  // 4. 地址域 A (SA + CA)
  // 4.1 服务器地址 SA
  if (offset + 1 > totalLen)
    return fields;
  int saStart = offset;
  Prot698SAIndicator saInd;
  saInd.val = ptr[offset];
  int addrLen = saInd.bits.len_mask + 1;
  bool hasExtLogicAddr = (saInd.val >> 5) & 0x01;
  int logicAddrVal = (saInd.val >> 4) & 0x01;

  QString saTypeStr;
  switch (saInd.bits.type) {
  case 0:
    saTypeStr = "单地址";
    break;
  case 1:
    saTypeStr = "通配地址";
    break;
  case 2:
    saTypeStr = "组地址";
    break;
  case 3:
    saTypeStr = "广播地址";
    break;
  }

  fields.append(
      Prot698Field("服务器地址 SA", "", "服务器地址", offset, addrLen + 1));
  fields.append(Prot698Field("  地址标识", QString::asprintf("%02X", saInd.val),
                             QString("类型: %1, 地址长度: %2, 扩展逻辑地址: %3")
                                 .arg(saTypeStr)
                                 .arg(addrLen)
                                 .arg(hasExtLogicAddr ? "有" : "无"),
                             offset, 1, 1));
  offset++;

  if (offset + addrLen > totalLen)
    return fields;

  // 细化地址解析
  if (addrLen > 0) {
    int phyLen = addrLen;
    if (hasExtLogicAddr) {
      phyLen = addrLen - 1;
    }

    // 逻辑地址
    if (hasExtLogicAddr) {
      uint8_t logicAddr = ptr[offset];
      fields.append(
          Prot698Field("  逻辑地址", QString::asprintf("%02X", logicAddr),
                       QString("Value: %1").arg(logicAddr), offset, 1, 1));
      offset++;
    } else {
      fields.append(Prot698Field("  逻辑地址", "",
                                 QString("Value: %1").arg(logicAddrVal),
                                 saStart, 1, 1));
    }

    // 物理地址
    if (phyLen > 0) {
      QByteArray phyBytes = data.mid(offset, phyLen);
      QString phyStr;
      // 物理地址为压缩BCD码，低位在前
      for (int i = phyLen - 1; i >= 0; i--) {
        phyStr += QString::asprintf("%02X", (uint8_t)phyBytes[i]);
      }
      fields.append(
          Prot698Field("  物理地址", QString(phyBytes.toHex().toUpper()),
                       QString("BCD: %1").arg(phyStr), offset, phyLen, 1));
      offset += phyLen;
    }
  }

  // 4.2 客户机地址 CA
  if (offset + 1 > totalLen)
    return fields;
  uint8_t ca = ptr[offset];
  fields.append(Prot698Field("客户机地址 CA", QString::asprintf("%02X", ca),
                             QString("CA: %1").arg(ca), offset, 1));
  offset++;

  // 5. 帧头校验 HCS
  if (offset + 2 > totalLen)
    return fields;
  uint16_t hcs = ptr[offset] | (ptr[offset + 1] << 8);

  // 计算 HCS
  // Standard 698.45: HCS includes Length field (starts at offset 1)
  uint16_t calcHcs = calcCRC16(ptr + 1, offset - 1);

  QString hcsDesc =
      (hcs == calcHcs)
          ? "OK"
          : QString("Fail (Calc: %1)").arg(QString::asprintf("%04X", calcHcs));
  fields.append(Prot698Field("帧头校验 HCS", QString::asprintf("%04X", hcs),
                             hcsDesc, offset, 2));
  offset += 2;

  // 6. 分帧信息 (如果存在)
  if (ctrl.bits.split) {
    if (offset + 2 > totalLen)
      return fields;
    Prot698SplitInfo split;
    split.val = ptr[offset] | (ptr[offset + 1] << 8);

    QString splitType;
    switch (split.bits.type) {
    case 0:
      splitType = "起始帧";
      break;
    case 1:
      splitType = "最后帧";
      break; // Binary 01
    case 2:
      splitType = "确认帧";
      break; // Binary 10
    case 3:
      splitType = "中间帧";
      break; // Binary 11
    }

    fields.append(Prot698Field(
        "分帧信息", QString::asprintf("%04X", split.val),
        QString("Seq: %1, Type: %2").arg(split.bits.seq).arg(splitType), offset,
        2));
    offset += 2;
  }

  // 7. APDU
  // APDU 长度 = FrameLen - (Offset - 1) - 2 (FCS) ?
  // FrameLen is length from Length field to FCS end.
  // Current Offset is after HCS (and Split Info).
  // Start index was 0.
  // FrameLen includes: Length(2) + Control(1) + SA(1+N) + CA(1) + HCS(2) +
  // [Split(2)] + APDU + FCS(2). So APDU Size = FrameLen - (CurrentOffset - 1)
  // - 2. (CurrentOffset - 1) is size of (Length + ... + Split).

  int apduSize = frameLen - (offset - 1) - 2;
  if (apduSize > 0 && offset + apduSize <= totalLen) {
    QByteArray apduData = data.mid(offset, apduSize);
    fields.append(Prot698Field(
        "应用数据单元 APDU", QString(apduData.toHex().toUpper()),
        QString("应用数据单元, %1 字节").arg(apduSize), offset, apduSize));

    // 解析 APDU
    int apduOffset = 0;
    QList<Prot698Field> apduFields;
    parseApdu(apduData, apduOffset, apduFields, 1);
    for (auto field : apduFields) { field.offset += offset; fields.append(field); }
    if (apduOffset != apduSize) fields.append({"Error", "", "APDU未完整消费", offset + apduOffset, 0});

    // 如果 parseApdu 没有消耗完所有字节 (例如 TimeTag), 这里可以继续处理
    // 但 parseApdu 目前只处理了 Tag。
    // 我们需要确保 offset 前进到 APDU 结束
    offset += apduSize;
  }

  // 8. 帧校验 FCS
  if (offset + 2 > totalLen)
    return fields;
  uint16_t fcs = ptr[offset] | (ptr[offset + 1] << 8);

  // 计算 FCS
  // Standard 698.45: FCS includes Length field (starts at offset 1)
  uint16_t calcFcs = calcCRC16(ptr + 1, offset - 1);

  QString fcsDesc =
      (fcs == calcFcs)
          ? "OK"
          : QString("Fail (Calc: %1)").arg(QString::asprintf("%04X", calcFcs));
  fields.append(Prot698Field("帧校验 FCS", QString::asprintf("%04X", fcs),
                             fcsDesc, offset, 2));
  offset += 2;

  // 9. 结束字符 (16H)
  if (offset < totalLen) {
    if (ptr[offset] == 0x16) {
      fields.append(Prot698Field("结束字符", "16", "结束字符", offset, 1));
    } else {
      fields.append(Prot698Field("结束字符",
                                 QString::asprintf("%02X", ptr[offset]),
                                 "Error: Not 16H", offset, 1));
    }
  }

  return fields;
}

Prot698ParseResult Prot698Parser::parseWithAddress(const QByteArray &data) {
  Prot698ParseResult result;
  result.fields = parse(data);

  // Log display accepts protocol services beyond the framing tool's 18 services.
  result.isValid = isValidFrame(data) && data.size() ==
      ((static_cast<quint8>(data[1]) | static_cast<quint8>(data[2]) << 8) & 0x3fff) + 2;

  // 从解析结果中提取物理地址和逻辑地址
  for (const auto &f : result.fields) {
    // 物理地址字段的 description 格式为 "BCD: XXXXXXXX"
    if (f.name.trimmed() == "物理地址") {
      QString desc = f.description;
      if (desc.startsWith("BCD: ")) {
        result.physicalAddress = desc.mid(5); // 提取 "BCD: " 之后的内容
      }
    }
    // 逻辑地址字段的 description 格式为 "Value: X" 或 "Value: X (From
    // Indicator)"
    if (f.name.trimmed() == "逻辑地址") {
      QString desc = f.description;
      if (desc.startsWith("Value: ")) {
        QString valPart = desc.mid(7);
        int spaceIdx = valPart.indexOf(' ');
        if (spaceIdx > 0) {
          result.logicAddress = valPart.left(spaceIdx);
        } else {
          result.logicAddress = valPart;
        }
      } else {
        // 兜底逻辑：如果不以 Value:
        // 开头，尝试直接使用整个描述（可能是旧版本或特定情况）
        result.logicAddress = desc.trimmed();
      }
    }
  }

  return result;
}

// --- Sub-structures ---

Prot698AttrDef *Prot698Parser::parseOAD(const QByteArray &data, int &offset,
                                        QList<Prot698Field> &fields, int indent,
                                        const QString &expectedOp) {
  Prot698AttrDef *def = nullptr;
  if (offset + 4 <= data.size()) {
    QByteArray oad = data.mid(offset, 4);
    // OAD: OI(2) + Attrib(1) + Index(1)
    uint16_t oi = (uint8_t)oad[0] << 8 | (uint8_t)oad[1];
    uint8_t attr = (uint8_t)oad[2];
    uint8_t idx = (uint8_t)oad[3];

    QString oiStr = QString("%1").arg(oi, 4, 16, QChar('0')).toUpper();
    QString desc =
        QString("OI:%1, Attr:%2, Idx:%3").arg(oiStr).arg(attr).arg(idx);
    QString summary;
    QString detail;

    // Lookup
    def = Prot698OadManager::instance().getAttrDef(oiStr, attr);
    if (def) {
      // Check OP
      if (!expectedOp.isEmpty()) {
        QStringList ops = def->op.split('|');
        if (!ops.contains(expectedOp)) {
          desc += " [OP Mismatch: Expected " + expectedOp + ", Found " +
                  def->op + "]";
        }
      }

      // Also check Object Name?
      detail = desc;
      Prot698ObjDef *obj = Prot698OadManager::instance().getObjDef(oiStr);
      if (obj) {
        summary = obj->name;
        if (!def->name.isEmpty() && def->name != obj->name) detail = def->name + " · " + detail;
        desc = obj->name + " - " + def->name + " (" + desc + ")";
      } else {
        summary = def->name;
        desc += " (" + def->name + ")";
      }
    } else {
      Prot698ObjDef *obj = Prot698OadManager::instance().getObjDef(oiStr);
      if (obj) {
        summary = obj->name;
        detail = desc;
        desc = obj->name + " (" + desc + ")";
      }
    }

    fields.append(Prot698Field("OAD", QString(oad.toHex().toUpper()), desc,
                               offset, 4, indent));
    fields.last().summary = summary;
    fields.last().detail = detail;
    offset += 4;
  }
  return def;
}

// --- Placeholders ---

void Prot698Parser::parseRSD(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;
  uint8_t type = data[offset];
  QString typeStr;
  switch (type) {
  case 0:
    typeStr = "不选择(0)";
    break;
  case 1:
    typeStr = "选择方法1(1)";
    break;
  case 2:
    typeStr = "选择方法2(2)";
    break;
  case 3:
    typeStr = "选择方法3(3)";
    break;
  case 4:
    typeStr = "选择方法4(4)";
    break;
  case 5:
    typeStr = "选择方法5(5)";
    break;
  case 6:
    typeStr = "选择方法6(6)";
    break;
  case 7:
    typeStr = "选择方法7(7)";
    break;
  case 8:
    typeStr = "选择方法8(8)";
    break;
  case 9:
    typeStr = "选择方法9(9)";
    break;
  case 10:
    typeStr = "选择方法10(10)";
    break;
  case 11:
    typeStr = "选择方法11(11)";
    break;
  default:
    typeStr = QString("Unknown(%1)").arg(type);
    break;
  }
  fields.append(Prot698Field("RSD", QString::asprintf("%02X", type), typeStr,
                             offset, 1, indent));
  offset++;

  switch (type) {
  case 0: // NULL
    break;
  case 1: // Selector1: SEQUENCE { OAD, Data }
    parseOAD(data, offset, fields, indent + 1);
    parseData(data, offset, fields, indent + 1);
    break;
  case 2: // Selector2: SEQUENCE { OAD, Start(Data), End(Data), Interval(Data) }
    parseOAD(data, offset, fields, indent + 1);
    parseData(data, offset, fields, indent + 1); // Start
    parseData(data, offset, fields, indent + 1); // End
    parseData(data, offset, fields, indent + 1); // Interval
    break;
  case 3: // Selector3: SEQUENCE OF Selector2
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt),
                                 "Selector2 Count", offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        // Selector2
        parseOAD(data, offset, fields, indent + 2);
        parseData(data, offset, fields, indent + 2);
        parseData(data, offset, fields, indent + 2);
        parseData(data, offset, fields, indent + 2);
      }
    }
    break;
  case 4: // Selector4: SEQUENCE { Start(date_time_s), MS }
    readDateTimeS(data, offset); // Just read, fields added inside? No,
                                 // readDateTimeS returns string.
    // Need to add field manually or update readDateTimeS to add field?
    // readDateTimeS is a helper that returns string.
    // Let's implement inline or use helper.
    {
      QString dt = readDateTimeS(data, offset);
      // readDateTimeS advances offset but doesn't add field.
      // We need to add field. But readDateTimeS reads 7 bytes.
      // Let's assume readDateTimeS does NOT advance offset? No, it takes
      // &offset. Wait, readDateTimeS implementation: QString
      // Prot698Parser::readDateTimeS(const QByteArray &data, int &offset) { ...
      // offset+=7; return ... } So we need to capture offset before call.
      int tmpOff = offset - 7; // Hacky.
      // Better:
      // int startOff = offset;
      // QString dt = readDateTimeS(data, offset);
      // fields.append({"采集启动时间", data.mid(startOff, 7).toHex().toUpper(),
      // dt, startOff, 7, indent + 1}); But readDateTimeS is private and I can't
      // see it here. Let's re-implement or assume it exists. I will implement
      // parseDateTimeS helper if needed or just use logic. Standard
      // date_time_s: Year(2) Month(1) Day(1) Hour(1) Min(1) Sec(1)
      if (offset + 7 <= data.size()) {
        uint16_t year =
            ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
        uint8_t month = data[offset + 2];
        uint8_t day = data[offset + 3];
        uint8_t hour = data[offset + 4];
        uint8_t min = data[offset + 5];
        uint8_t sec = data[offset + 6];
        QString dtStr = QString("%1-%2-%3 %4:%5:%6")
                            .arg(year)
                            .arg(month, 2, 10, QChar('0'))
                            .arg(day, 2, 10, QChar('0'))
                            .arg(hour, 2, 10, QChar('0'))
                            .arg(min, 2, 10, QChar('0'))
                            .arg(sec, 2, 10, QChar('0'));
        fields.append(Prot698Field(
            "采集启动时间", QString(data.mid(offset, 7).toHex().toUpper()),
            dtStr, offset, 7, indent + 1));
        offset += 7;
      }
    }
    parseMS(data, offset, fields, indent + 1);
    break;
  case 5: // Selector5: SEQUENCE { Storage(date_time_s), MS }
  {
    if (offset + 7 <= data.size()) {
      // ... Same logic for date_time_s
      // Reuse logic
      uint16_t year = ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
      QString dtStr = QString("%1-%2-%3 %4:%5:%6")
                          .arg(year)
                          .arg((uint8_t)data[offset + 2], 2, 10, QChar('0'))
                          .arg((uint8_t)data[offset + 3], 2, 10, QChar('0'))
                          .arg((uint8_t)data[offset + 4], 2, 10, QChar('0'))
                          .arg((uint8_t)data[offset + 5], 2, 10, QChar('0'))
                          .arg((uint8_t)data[offset + 6], 2, 10, QChar('0'));
      fields.append(Prot698Field("采集存储时间",
                                 QString(data.mid(offset, 7).toHex().toUpper()),
                                 dtStr, offset, 7, indent + 1));
      offset += 7;
    }
  }
    parseMS(data, offset, fields, indent + 1);
    break;
  case 6: // Selector6: SEQUENCE { StartBegin(date_time_s),
          // StartEnd(date_time_s), MS }
    // ...
    for (int k = 0; k < 2; k++) {
      if (offset + 7 <= data.size()) {
        uint16_t year =
            ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
        QString dtStr = QString("%1-%2-%3 %4:%5:%6")
                            .arg(year)
                            .arg((uint8_t)data[offset + 2], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 3], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 4], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 5], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 6], 2, 10, QChar('0'));
        fields.append(
            Prot698Field(k == 0 ? "起始时间" : "结束时间",
                         QString(data.mid(offset, 7).toHex().toUpper()), dtStr,
                         offset, 7, indent + 1));
        offset += 7;
      }
    }
    parseMS(data, offset, fields, indent + 1);
    break;
  case 7: // Selector7: SEQUENCE { StorageBegin, StorageEnd, TI, MS }
    for (int k = 0; k < 2; k++) {
      if (offset + 7 <= data.size()) {
        uint16_t year =
            ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
        QString dtStr = QString("%1-%2-%3 %4:%5:%6")
                            .arg(year)
                            .arg((uint8_t)data[offset + 2], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 3], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 4], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 5], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 6], 2, 10, QChar('0'));
        fields.append(
            Prot698Field(k == 0 ? "存储起始时间" : "存储结束时间",
                         QString(data.mid(offset, 7).toHex().toUpper()), dtStr,
                         offset, 7, indent + 1));
        offset += 7;
      }
    }
    parseTI(data, offset, fields, indent + 1);
    parseMS(data, offset, fields, indent + 1);
    break;
  case 8: // Selector8: SEQUENCE { SuccessBegin, SuccessEnd, TI, MS }
    for (int k = 0; k < 2; k++) {
      if (offset + 7 <= data.size()) {
        uint16_t year =
            ((uint8_t)data[offset] << 8) | (uint8_t)data[offset + 1];
        QString dtStr = QString("%1-%2-%3 %4:%5:%6")
                            .arg(year)
                            .arg((uint8_t)data[offset + 2], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 3], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 4], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 5], 2, 10, QChar('0'))
                            .arg((uint8_t)data[offset + 6], 2, 10, QChar('0'));
        fields.append(
            Prot698Field(k == 0 ? "成功起始时间" : "成功结束时间",
                         QString(data.mid(offset, 7).toHex().toUpper()), dtStr,
                         offset, 7, indent + 1));
        offset += 7;
      }
    }
    parseTI(data, offset, fields, indent + 1);
    parseMS(data, offset, fields, indent + 1);
    break;
  case 9: // Selector9: SEQUENCE { n (unsigned) }
    if (offset < data.size()) {
      uint8_t n = data[offset];
      fields.append(Prot698Field("上第n次记录", QString::asprintf("%02X", n),
                                 QString::number(n), offset, 1, indent + 1));
      offset++;
    }
    break;
  case 10: // Selector10: SEQUENCE { n (unsigned), MS }
    if (offset < data.size()) {
      uint8_t n = data[offset];
      fields.append(Prot698Field("上n条记录", QString::asprintf("%02X", n),
                                 QString::number(n), offset, 1, indent + 1));
      offset++;
    }
    parseMS(data, offset, fields, indent + 1);
    break;
  case 11: // Selector11: SEQUENCE OF RSD
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt), "RSD Count",
                                 offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseRSD(data, offset, fields, indent + 2);
      }
    }
    break;
  }
}

void Prot698Parser::parseTI(const QByteArray &data, int &offset,
                            QList<Prot698Field> &fields, int indent,
                            const QString &name) {
  // TI: SEQUENCE { Unit (ENUM), Interval (long-unsigned) }
  // Total 3 bytes
  if (offset + 3 > data.size()) {
    fields.append(Prot698Field(name, "Error", "Not enough data for TI", offset,
                               0, indent));
    return;
  }

  int startOffset = offset;
  uint8_t unit = data[offset];
  QString unitStr;
  switch (unit) {
  case 0:
    unitStr = "秒";
    break;
  case 1:
    unitStr = "分";
    break;
  case 2:
    unitStr = "时";
    break;
  case 3:
    unitStr = "日";
    break;
  case 4:
    unitStr = "月";
    break;
  case 5:
    unitStr = "年";
    break;
  default:
    unitStr = QString("Unknown(%1)").arg(unit);
    break;
  }

  // Big Endian for Interval (long-unsigned in TI)
  uint16_t val = ((uint8_t)data[offset + 1] << 8) | (uint8_t)data[offset + 2];

  QString desc = QString("单位: %1, 间隔: %2").arg(unitStr).arg(val);
  fields.append(Prot698Field(name,
                             QString(data.mid(offset, 3).toHex().toUpper()),
                             desc, offset, 3, indent));

  fields.append(Prot698Field("  时间单位", QString::asprintf("%02X", unit),
                             unitStr, offset, 1, indent + 1));
  fields.append(Prot698Field("  间隔值",
                             QString(data.mid(offset + 1, 2).toHex().toUpper()),
                             QString::number(val), offset + 1, 2, indent + 1));

  offset += 3;
}

void Prot698Parser::parseRegion(const QByteArray &data, int &offset,
                                QList<Prot698Field> &fields, int indent) {
  if (offset < data.size()) {
    uint8_t unit = data[offset];
    QString unitStr;
    switch (unit) {
    case 0:
      unitStr = "前闭后开 [ )";
      break;
    case 1:
      unitStr = "前开后闭 ( ]";
      break;
    case 2:
      unitStr = "前闭后闭 [ ]";
      break;
    case 3:
      unitStr = "前开后开 ( )";
      break;
    default:
      unitStr = QString("Unknown(%1)").arg(unit);
      break;
    }
    fields.append(Prot698Field("Region Unit", QString::asprintf("%02X", unit),
                               unitStr, offset, 1, indent));
    offset++;
  }
  fields.append(Prot698Field("Start Value", "", "", offset, 0, indent));
  parseData(data, offset, fields, indent + 1);
  fields.append(Prot698Field("End Value", "", "", offset, 0, indent));
  parseData(data, offset, fields, indent + 1);
}

int Prot698Parser::parseROAD(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent,
                             QList<Prot698AttrDef> *outOads) {
  // ROAD: SEQUENCE { OAD, SEQUENCE OF OAD }
  Prot698AttrDef roadDef;
  roadDef.name = "ROAD Column";
  roadDef.dataDef.name = "ROAD Data"; // Explicitly set name for the container
  roadDef.dataDef.type = "structure"; // Synthetic structure

  int count = 1; // The first OAD
  Prot698AttrDef *def = parseOAD(data, offset, fields, indent);
  // The first OAD in ROAD is the "Table OAD" (e.g. Freeze Table).
  // The data returned in the record row corresponds to the Associated OADs
  // (Columns). So we do NOT add the first OAD to the children list for data
  // parsing.

  if (offset < data.size()) {
    uint8_t cnt = data[offset];
    fields.append(Prot698Field("Associated OAD Count", QString::number(cnt), "",
                               offset, 1, indent));
    offset++;
    count += cnt; // Add associated OADs
    for (int i = 0; i < cnt; i++) {
      def = parseOAD(data, offset, fields, indent + 1);
      if (def) {
        Prot698SubItem item = def->dataDef;

        QString oldName = item.name;
        // Only overwrite if oldName is generic/bad
        if (oldName.isEmpty() || oldName == "Data" || oldName == item.type ||
            oldName == "一条冻结记录") {
          item.name = def->name;
        }

        // Propagate name to array element if it matches the old generic name
        if (item.type == "array" && !item.children.isEmpty()) {
          if (item.children[0].name == oldName) {
            item.children[0].name = item.name;
          }
        }

        roadDef.dataDef.children.append(item);
      } else {
        roadDef.dataDef.children.append(Prot698SubItem());
      }
    }
  }

  if (outOads)
    outOads->append(roadDef);

  return count;
}

int Prot698Parser::parseCSD(const QByteArray &data, int &offset,
                            QList<Prot698Field> &fields, int indent,
                            QList<Prot698AttrDef> *outOads) {
  // CSD: CHOICE { OAD [0], ROAD [1] }
  int count = 0;
  if (offset < data.size()) {
    uint8_t choice = data[offset];
    int startOffset = offset;
    offset++; // Consume choice

    int fieldsStartIndex = fields.size();

    if (choice == 0) {
      Prot698AttrDef *def = parseOAD(data, offset, fields, indent);
      if (outOads) {
        if (def) {
          outOads->append(*def);
          // Fix for generic names (e.g. "一条冻结记录")
          Prot698AttrDef &lastDef = outOads->last();
          QString attrName = lastDef.name;
          QString dataName = lastDef.dataDef.name;

          // If attribute name is generic/bad, and data name is specific, use
          // data name. "一条冻结记录" is considered a generic name in some
          // XMLs.
          bool isAttrBad = (attrName == "一条冻结记录" || attrName == "Data");
          bool isDataBad =
              (dataName.isEmpty() || dataName == "Data" ||
               dataName == lastDef.dataDef.type || dataName == "一条冻结记录");

          if (isAttrBad && !isDataBad) {
            lastDef.name = dataName;
          }
        } else
          outOads->append(Prot698AttrDef());
      }
      count = 1;
    } else if (choice == 1) {
      count = parseROAD(data, offset, fields, indent, outOads);
    }

    // Merge choice info into the first field generated
    if (fields.size() > fieldsStartIndex) {
      Prot698Field &field = fields[fieldsStartIndex];
      field.name = (choice == 0) ? "CSD (OAD)" : "CSD (ROAD)";
      // Prepend choice byte to value
      field.value = QString("%1 %2")
                        .arg(QString::asprintf("%02X", choice))
                        .arg(field.value);
      field.offset = startOffset;
      field.length += 1;
    }
  }
  return count;
}

int Prot698Parser::parseRCSD(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent,
                             QList<Prot698AttrDef> *outOads) {
  // RCSD: SEQUENCE OF CSD
  // User requirement: The number of Data items in a row corresponds to the
  // number of CSD items, not the total number of OADs (if ROAD is used).
  int csdCount = 0;
  if (offset < data.size()) {
    uint8_t cnt = data[offset];
    csdCount = cnt;
    fields.append(Prot698Field("RCSD Count", QString::number(cnt), "CSD Count",
                               offset, 1, indent));
    offset++;
    for (int i = 0; i < cnt; i++) {
      parseCSD(data, offset, fields, indent + 1, outOads);
    }
  }
  return csdCount;
}

void Prot698Parser::parseMS(const QByteArray &data, int &offset,
                            QList<Prot698Field> &fields, int indent) {
  if (offset >= data.size())
    return;
  uint8_t type = data[offset];
  QString typeStr;
  switch (type) {
  case 0:
    typeStr = "无表计(0)";
    break;
  case 1:
    typeStr = "全部用户地址(1)";
    break;
  case 2:
    typeStr = "一组用户类型(2)";
    break;
  case 3:
    typeStr = "一组用户地址(3)";
    break;
  case 4:
    typeStr = "一组配置序号(4)";
    break;
  case 5:
    typeStr = "一组用户类型区间(5)";
    break;
  case 6:
    typeStr = "一组用户地址区间(6)";
    break;
  case 7:
    typeStr = "一组配置序号区间(7)";
    break;
  case 8:
    typeStr = "一组设备类型(8)";
    break;
  case 9:
    typeStr = "特定逻辑设备(9)";
    break;
  default:
    typeStr = QString("Unknown(%1)").arg(type);
    break;
  }
  fields.append(Prot698Field("MS", QString::asprintf("%02X", type), typeStr,
                             offset, 1, indent));
  offset++;

  switch (type) {
  case 0: // NULL
  case 1: // All User Addr
    break;
  case 2: // SeqOf unsigned (User Type)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt),
                                 "User Type Count", offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        if (offset < data.size()) {
          fields.append(Prot698Field(
              "用户类型", QString::asprintf("%02X", (uint8_t)data[offset]),
              QString::number((uint8_t)data[offset]), offset, 1, indent + 2));
          offset++;
        }
      }
    }
    break;
  case 3: // SeqOf TSA
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt), "TSA Count",
                                 offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseTSA(data, offset, fields, indent + 2);
      }
    }
    break;
  case 4: // SeqOf long-unsigned (Config No)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt),
                                 "Config No Count", offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        if (offset + 2 <= data.size()) {
          uint16_t val =
              (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
          fields.append(Prot698Field(
              "配置序号", QString(data.mid(offset, 2).toHex().toUpper()),
              QString::number(val), offset, 2, indent + 2));
          offset += 2;
        }
      }
    }
    break;
  case 5: // SeqOf Region (User Type)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt), "Region Count",
                                 offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseRegion(data, offset, fields, indent + 2);
      }
    }
    break;
  case 6: // SeqOf Region (User Addr)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt), "Region Count",
                                 offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseRegion(data, offset, fields, indent + 2);
      }
    }
    break;
  case 7: // SeqOf Region (Config No)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt), "Region Count",
                                 offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        parseRegion(data, offset, fields, indent + 2);
      }
    }
    break;
  case 8: // SeqOf long-unsigned (Device Type)
    if (offset < data.size()) {
      uint8_t cnt = data[offset];
      fields.append(Prot698Field("Count", QString::number(cnt),
                                 "Device Type Count", offset, 1, indent + 1));
      offset++;
      for (int i = 0; i < cnt; i++) {
        if (offset + 2 <= data.size()) {
          uint16_t val =
              (uint8_t)data[offset] | ((uint8_t)data[offset + 1] << 8);
          fields.append(Prot698Field(
              "设备类型", QString(data.mid(offset, 2).toHex().toUpper()),
              QString::number(val), offset, 2, indent + 2));
          offset += 2;
        }
      }
    }
    break;
  case 9: // unsigned (Specific Logic Device)
    if (offset < data.size()) {
      fields.append(Prot698Field(
          "特定逻辑设备", QString::asprintf("%02X", (uint8_t)data[offset]),
          QString::number((uint8_t)data[offset]), offset, 1, indent + 1));
      offset++;
    }
    break;
  }
}

void Prot698Parser::parseTSA(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent) {
  // TSA ::= octet-string(SIZE(2..17))
  // Special handling: If starts with 0x55, treat as Tag (User requirement)
  // Standard TSA size is 2..17, so 0x55 (85) cannot be a valid length.
  int startOffset = offset;
  if (offset < data.size() && (uint8_t)data[offset] == 0x55) {
    offset++;
  }

  // OctetString in A-XDR has length prefix.
  int len = decodeLength(data, offset);
  if (offset + len <= data.size()) {
    QString desc = "Target Server Address";

    // Parse SA inside: [Indicator(1)] + [Address(N)]
    if (len > 0) {
      uint8_t saInd = data[offset];
      int addrLen = (saInd & 0x0F) + 1;

      // Verify length matches SA structure (at least 1 + addrLen)
      if (len >= 1 + addrLen) {
        QByteArray addrBytes = data.mid(offset + 1, addrLen);
        desc = QString("Addr: %1").arg(QString(addrBytes.toHex().toUpper()));
      }
    }

    // Calculate total length including optional tag and length bytes
    int totalLen = (offset + len) - startOffset;

    fields.append(Prot698Field(
        "TSA", QString(data.mid(startOffset, totalLen).toHex().toUpper()), desc,
        startOffset, totalLen, indent));
    offset += len;
  } else {
    offset = startOffset;
  }
}

void Prot698Parser::parseDAR(const QByteArray &data, int &offset,
                             QList<Prot698Field> &fields, int indent) {
  if (offset < data.size()) {
    uint8_t val = data[offset];
    fields.append(Prot698Field("DAR", QString::asprintf("%02X", val),
                               "Device Application Result (Enum)", offset, 1,
                               indent));
    offset++;
  }
}

void Prot698Parser::parsePIID(const QByteArray &data, int &offset,
                              QList<Prot698Field> &fields, int indent) {
  // PIID 用于客户机APDU (Client-APDU) 的服务数据类型
  // bit7 (服务优先级): 0=普通优先级,
  // 1=高优先级，在应答APDU中其值与请求的APDU相同 bit6 (保留): 保留位
  // bit0...bit5 (服务序号):
  // 二进制编码表示0...63，在应答APDU中其值与请求的APDU相同
  if (offset < data.size()) {
    uint8_t val = data[offset];
    uint8_t priority = (val >> 7) & 0x01; // bit7: 服务优先级
    uint8_t reserved = (val >> 6) & 0x01; // bit6: 保留位
    uint8_t serial = val & 0x3F;          // bit0-5: 服务序号 (0-63)

    QString desc = QString("服务序号:%1, 优先级:%2(%3)")
                       .arg(serial)
                       .arg(priority)
                       .arg(priority ? "高优先级" : "普通优先级");
    if (reserved) {
      desc += QString(", 保留位:%1").arg(reserved);
    }

    fields.append(Prot698Field("PIID", QString::asprintf("%02X", val), desc,
                               offset, 1, indent));
    offset++;
  }
}

void Prot698Parser::parsePIID_ACD(const QByteArray &data, int &offset,
                                  QList<Prot698Field> &fields, int indent) {
  // PIID-ACD 用于服务器APDU (Server-APDU) 的服务数据类型
  // bit7 (服务优先级): 见PIID定义，0=普通优先级, 1=高优先级
  // bit6 (请求访问ACD): 0=不请求, 1=请求 (表示服务端请求主站读取事件数据)
  // bit0...bit5 (服务序号): 见PIID定义，二进制编码表示0...63
  if (offset < data.size()) {
    uint8_t val = data[offset];
    uint8_t priority = (val >> 7) & 0x01; // bit7: 服务优先级
    uint8_t acd = (val >> 6) & 0x01;      // bit6: 请求访问ACD标志
    uint8_t serial = val & 0x3F;          // bit0-5: 服务序号 (0-63)

    QString desc = QString("服务序号:%1, 优先级:%2(%3), ACD:%4(%5)")
                       .arg(serial)
                       .arg(priority)
                       .arg(priority ? "高优先级" : "普通优先级")
                       .arg(acd)
                       .arg(acd ? "请求访问" : "不请求");

    fields.append(Prot698Field("PIID-ACD", QString::asprintf("%02X", val), desc,
                               offset, 1, indent));
    offset++;
  }
}

QString Prot698Parser::getServiceClassDesc(quint8 sc) {
  return QString("SC=%1").arg(sc);
}

namespace {
struct ApduReader {
  const QByteArray &bytes;
  QString *error;
  int offset = 0;
  int nodes = 0;
  bool fail(const QString &message) {
    if (error && error->isEmpty()) *error = QString("APDU[%1]: %2").arg(offset).arg(message);
    return false;
  }
  bool need(int n) {
    return n >= 0 && n <= bytes.size() - offset ? true : fail("字段截断");
  }
  bool skip(int n) { if (!need(n)) return false; offset += n; return true; }
  int byte() { if (!need(1)) return -1; return static_cast<quint8>(bytes.at(offset++)); }
  int word() { if (!need(2)) return -1; const int high = byte(); return high * 256 + byte(); }
  bool flag() { const int value = byte(); return value == 0 || value == 1 ? true : fail("optional/bool标志只能是0或1"); }
  int length() {
    int count = byte();
    if (count < 0) return -1;
    if (count < 128) return count;
    const int width = count & 127;
    if (width < 1 || width > 2 || !need(width)) { fail("长度编码超出支持范围"); return -1; }
    int value = 0;
    for (int i = 0; i < width; ++i) value = value * 256 + byte();
    if (value < 128 || (width == 2 && value <= 255)) { fail("非最短长度编码"); return -1; }
    return value;
  }
  bool dateTimeS() {
    if (!need(7)) return false;
    const int year = word(); const int month = byte(); const int day = byte();
    const int hour = byte(); const int minute = byte(); const int second = byte();
    return QDate(year, month, day).isValid() && QTime(hour, minute, second).isValid() ? true : fail("无效日期时间");
  }
  bool ti() {
    const int unit = byte();
    if (unit < 0 || unit > 5) return fail("不支持的TI单位");
    return skip(2);
  }
  bool tsa() {
    const int n = length();
    if (n < 2 || n > 17 || !need(n)) return fail("TSA长度不合法");
    if (n != (static_cast<quint8>(bytes.at(offset)) & 0xf) + 2) return fail("TSA地址描述长度与实际字节数不一致");
    return skip(n);
  }
  bool data(int depth = 0) {
    if (depth > 32 || ++nodes > 4096) return fail("Data深度或数量超过限制");
    const int tag = byte(); if (tag < 0) return false;
    switch (static_cast<Prot698DataType>(tag)) {
    case Prot698DataType::Null: return true;
    case Prot698DataType::Array: case Prot698DataType::Structure: {
      const int count = byte(); if (count < 0) return false;
      for (int i = 0; i < count; ++i) if (!data(depth + 1)) return false;
      return true;
    }
    case Prot698DataType::Bool: return flag();
    case Prot698DataType::Integer: case Prot698DataType::Unsigned: case Prot698DataType::Enum: return skip(1);
    case Prot698DataType::Long: case Prot698DataType::LongUnsigned: case Prot698DataType::OI: case Prot698DataType::ScalerUnit: return skip(2);
    case Prot698DataType::DoubleLong: case Prot698DataType::DoubleLongUnsigned:
    case Prot698DataType::OAD: case Prot698DataType::OMD: return skip(4);
    case Prot698DataType::Long64: case Prot698DataType::Long64Unsigned: return skip(8);
    case Prot698DataType::Float32: {
      if (!need(4)) return false;
      quint32 bits = qFromBigEndian<quint32>(bytes.constData() + offset); float number;
      std::memcpy(&number, &bits, 4); offset += 4;
      return std::isfinite(number) ? true : fail("浮点必须有限");
    }
    case Prot698DataType::Float64: {
      if (!need(8)) return false;
      quint64 bits = qFromBigEndian<quint64>(bytes.constData() + offset); double number;
      std::memcpy(&number, &bits, 8); offset += 8;
      return std::isfinite(number) ? true : fail("浮点必须有限");
    }
    case Prot698DataType::BitString: {
      int n = length(); if (n < 0 || !need((n + 7) / 8)) return false;
      if (n % 8 && (static_cast<quint8>(bytes.at(offset + n / 8)) & ((1 << (8 - n % 8)) - 1))) return fail("位串尾部填充位必须为0");
      return skip((n + 7) / 8);
    }
    case Prot698DataType::OctetString: case Prot698DataType::VisibleString:
    case Prot698DataType::Utf8String: case Prot698DataType::MAC:
    case Prot698DataType::RN: case Prot698DataType::Checksum: { const int n = length(); return n >= 0 && skip(n); }
    case Prot698DataType::TSA: return tsa();
    case Prot698DataType::TI: return ti();
    case Prot698DataType::DateTimeS: return dateTimeS();
    case Prot698DataType::Date: {
      if (!need(5)) return false;
      int year = word(); int month = byte(); int day = byte(); int week = byte();
      return QDate(year, month, day).isValid() && (week >= 1 && week <= 7 || week == 255) ? true : fail("无效日期");
    }
    case Prot698DataType::Time: {
      if (!need(3)) return false;
      int h = byte(), m = byte(), s = byte(); return QTime(h, m, s).isValid() ? true : fail("无效时间");
    }
    case Prot698DataType::DateTime: {
      if (!need(10)) return false;
      int year = word(), month = byte(), day = byte(), week = byte();
      int h = byte(), m = byte(), s = byte(), ms = word();
      return QDate(year, month, day).isValid() && QTime(h, m, s, ms).isValid() && (week >= 1 && week <= 7 || week == 255) ? true : fail("无效日期时间");
    }
    case Prot698DataType::ROAD: {
      if (!skip(4)) return false; int count = byte(); return count >= 0 && skip(count * 4);
    }
    case Prot698DataType::RSD: return rsd(depth + 1);
    case Prot698DataType::CSD: { int columns = 0; return csd(&columns); }
    case Prot698DataType::RCSD: { int columns = 0; return rcsd(&columns); }
    case Prot698DataType::MS: return ms();
    case Prot698DataType::COMDCB: return skip(5);
    default: return fail("unsupported_definition: 未实现的Data线格式");
    }
  }
  bool ms() {
    const int type = byte(); if (type < 0) return false;
    if (type == 0 || type == 1) return true;
    if (type == 9) return skip(1);
    if (type == 2 || type == 3 || type == 4 || type == 8) {
      const int count = byte(); if (count < 0) return false;
      if (type == 3) { for (int i = 0; i < count; ++i) if (!tsa()) return false; return true; }
      return skip(count * (type == 2 ? 1 : 2));
    }
    return fail("unsupported_definition: MS范围分支尚未结构化");
  }
  bool selector2(int depth) { return skip(4) && data(depth) && data(depth) && data(depth); }
  bool rsd(int depth = 0) {
    if (depth > 16 || ++nodes > 4096) return fail("RSD深度或数量超过限制");
    int type = byte();
    switch (type) {
    case 0: return true;
    case 1: return skip(4) && data(depth + 1);
    case 2: return selector2(depth + 1);
    case 3: { int n = byte(); if (n < 0) return false; for (int i = 0; i < n; ++i) if (!selector2(depth + 1)) return false; return true; }
    case 4: case 5: return dateTimeS() && ms();
    case 6: return dateTimeS() && dateTimeS() && ms();
    case 7: case 8: return dateTimeS() && dateTimeS() && ti() && ms();
    case 9: return skip(1);
    case 10: return skip(1) && ms();
    case 11: { int n = byte(); if (n < 0) return false; for (int i = 0; i < n; ++i) if (!rsd(depth + 1)) return false; return true; }
    default: return fail("无效RSD选择");
    }
  }
  bool csd(int *columns) {
    int choice = byte(); if ((choice != 0 && choice != 1) || !skip(4)) return fail("无效CSD");
    if (choice == 0) { ++*columns; return true; }
    int n = byte(); if (n < 0) return false; ++*columns; return skip(n * 4);
  }
  bool rcsd(int *columns) {
    int n = byte(); if (n < 0) return false;
    for (int i = 0; i < n; ++i) if (!csd(columns)) return false;
    return true;
  }
  bool normalResult() {
    if (!skip(4)) return false; int choice = byte();
    return choice == 0 ? skip(1) : choice == 1 ? data() : fail("无效读取结果choice");
  }
  bool recordResult() {
    int columns = 0;
    if (!skip(4) || !rcsd(&columns)) return false;
    int choice = byte(); if (choice == 0) return skip(1);
    if (choice != 1) return fail("无效记录结果choice");
    int rows = byte(); if (rows < 0 || rows * columns > 4096) return fail("记录行列数量超过限制");
    for (int i = 0; i < rows * columns; ++i) if (!data()) return false;
    return true;
  }
  template<typename Function> bool sequence(Function fn, bool nonempty = true) {
    int count = byte(); if (count < 0 || nonempty && count == 0) return fail("列表数量不合法");
    for (int i = 0; i < count; ++i) if (!fn()) return false;
    return true;
  }
  bool timeTag() {
    int present = byte(); if (present == 0) return true;
    return present == 1 ? dateTimeS() && ti() : fail("无效TimeTag标志");
  }
  bool followReport() {
    int present = byte(); if (present == 0) return true;
    if (present != 1) return fail("无效FollowReport标志");
    int choice = byte();
    if (choice == 1) return sequence([&] { return normalResult(); });
    if (choice == 2) return sequence([&] { return recordResult(); });
    return fail("无效FollowReport choice");
  }
  bool read() {
    int tag = byte(), choice = byte();
    if (tag < 0 || choice < 0 || !skip(1)) return false;
    bool ok = false;
    const auto setItem = [&] { return skip(4) && data(); };
    const auto setResult = [&] { return skip(4) && skip(1); };
    const auto actionResult = [&] {
      if (!skip(5)) return false; int present = byte();
      return present == 0 ? true : present == 1 ? data() : fail("无效ACTION Data存在位");
    };
    if (tag == 5) {
      if (choice == 1) ok = skip(4);
      else if (choice == 2) ok = sequence([&] { return skip(4); });
      else if (choice == 3) { int columns = 0; ok = skip(4) && rsd() && rcsd(&columns); }
      else if (choice == 5) ok = skip(2);
    } else if (tag == 6 || tag == 7) {
      if (choice == 1) ok = setItem();
      else if (choice == 2) ok = sequence(setItem);
    } else if (tag == 9 && choice == 1) {
      ok = skip(2) && sequence([&] { return tsa() && skip(2) && sequence([&] { return skip(4); }); });
    } else if (tag == 0x85) {
      if (choice == 1) ok = normalResult();
      else if (choice == 2) ok = sequence([&] { return normalResult(); });
      else if (choice == 3) ok = recordResult();
      else if (choice == 5) {
        ok = flag() && skip(2);
        if (ok) {
          int resultChoice = byte();
          ok = resultChoice == 0 ? skip(1) : resultChoice == 1 ? sequence([&] { return normalResult(); }) : resultChoice == 2 ? sequence([&] { return recordResult(); }) : fail("无效GET_NEXT结果choice");
        }
      }
    } else if (tag == 0x86) {
      if (choice == 1) ok = setResult();
      else if (choice == 2) ok = sequence(setResult);
    } else if (tag == 0x87) {
      if (choice == 1) ok = actionResult();
      else if (choice == 2) ok = sequence(actionResult);
    } else if (tag == 0x89 && choice == 1) {
      ok = sequence([&] { return tsa() && sequence([&] { return normalResult(); }); });
    }
    if (!ok) return fail("字段截断、未知服务或未实现的服务分支");
    if (tag >= 0x80 && !followReport()) return false;
    if (!timeTag()) return false;
    return offset == bytes.size() ? true : fail("存在未消费的尾随APDU字节");
  }
};
}

bool Prot698Parser::validateApdu(const QByteArray &data, QString *err, int *consumed) {
  if (err) err->clear();
  ApduReader reader{data, err};
  const bool valid = reader.read();
  if (consumed) *consumed = reader.offset;
  return valid;
}

bool Prot698Parser::validateSingleFrame(const QByteArray &data, QString *err, bool checkApdu) {
  if (err) err->clear();
  const auto fail = [&](const QString &message) { if (err) *err = message; return false; };
  if (data.size() < 13 || static_cast<quint8>(data.front()) != 0x68) return fail("帧起始符或最小长度不合法");
  const auto ptr = reinterpret_cast<const quint8 *>(data.constData());
  const int rawLength = ptr[1] | ptr[2] << 8;
  if (rawLength & 0xc000) return fail("尚不支持非字节长度形式或保留长度位");
  const int length = rawLength & 0x3fff;
  if (length < 11 || data.size() != length + 2) return fail("完整帧声明长度不匹配或存在尾随字节");
  if (ptr[length + 1] != 0x16) return fail("帧结束符不合法");
  if (ptr[3] & 0x28) return fail("尚不支持分帧或加扰帧");
  if (ptr[3] & 0x10) return fail("控制域保留位不为0");
  const int addressLength = (ptr[4] & 0xf) + 1;
  if (ptr[4] & 0x20) return fail("尚不支持扩展逻辑地址");
  const int hcsOffset = 6 + addressLength;
  const int apduOffset = hcsOffset + 2;
  const int fcsOffset = length - 1;
  if (apduOffset >= fcsOffset) return fail("地址长度超过帧边界或APDU为空");
  const quint16 hcs = ptr[hcsOffset] | ptr[hcsOffset + 1] << 8;
  const quint16 fcs = ptr[fcsOffset] | ptr[fcsOffset + 1] << 8;
  if (hcs != calcCRC16(ptr + 1, hcsOffset - 1)) return fail("HCS校验失败");
  if (fcs != calcCRC16(ptr + 1, fcsOffset - 1)) return fail("FCS校验失败");
  if (checkApdu) {
    if ((ptr[3] & 7) != 3) return fail("普通APDU服务功能码必须为3");
    if (!validateApdu(data.mid(apduOffset, fcsOffset - apduOffset), err)) return false;
  }
  return true;
}
