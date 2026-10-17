#include "AppMsgParser.h"
#include "AppMsgLoader.h"
#include "ProtocolDetector.h"
#include "common/DataParser.h"
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include <cstdint>


using namespace Common;

namespace AppMsg {

AppMsgParser::AppMsgParser() {}

// 静态方法：检查 AppMsg 帧是否有效（用于协议自动检测）
// 进行基本的帧格式验证
bool AppMsgParser::isValidFrame(const QByteArray &data) {
  if (data.size() < 10)
    return false;

  int cursor = 0;
  // 0: PRM/Prio (1)
  // 1-2: Index (2)
  // 3-4: Label (2)
  cursor += 5;

  // 5: Source String (Null terminated, max 24 bytes check)
  // User Rule: 字符串内容小于16字节，必须在24字节内遇到0x00
  // 增强检测：字符串必须是可打印ASCII字符（0x20-0x7E）或为空
  int srcStart = cursor;
  bool foundNull = false;
  for (int i = 0; i < 24; ++i) { // Check max 24 bytes
    if (cursor >= data.size())
      return false;
    uint8_t ch = (uint8_t)data[cursor];
    if (ch == 0x00) {
      foundNull = true;
      cursor++;
      break;
    }
    // 增强：字符必须是可打印 ASCII 字符 (0x20-0x7E)
    if (ch < 0x20 || ch > 0x7E) {
      return false;
    }
    cursor++;
  }
  if (!foundNull)
    return false;
  int srcLen = cursor - srcStart - 1; // 不含 null terminator

  // Destination String (Null terminated, max 24 bytes check)
  // User Rule: 同样的逻辑
  int dstStart = cursor;
  foundNull = false;
  for (int i = 0; i < 24; ++i) {
    if (cursor >= data.size())
      return false;
    uint8_t ch = (uint8_t)data[cursor];
    if (ch == 0x00) {
      foundNull = true;
      cursor++;
      break;
    }
    // 增强：字符必须是可打印 ASCII 字符 (0x20-0x7E)
    if (ch < 0x20 || ch > 0x7E) {
      return false;
    }
    cursor++;
  }
  if (!foundNull)
    return false;
  int dstLen = cursor - dstStart - 1; // 不含 null terminator

  // 增强：Source 和 Destination 至少有一个非空
  if (srcLen == 0 && dstLen == 0) {
    return false;
  }

  // MSG-TAG (4 bytes)
  if (cursor + 4 > data.size())
    return false;
  cursor += 4;

  // MSG-Length (A-XDR)
  if (cursor >= data.size())
    return false;
  int len = DataParser::decodeLength(data, cursor);

  // Verify remaining size matches length
  // User didn't specify strict match for Payload vs Data Size, but
  // "isValidFrame" implies whole frame check. If AppMsg is usually the payload
  // of another transport, it might not have extra bytes. Let's enforce
  // strictly: cursor + len == data.size() Or at least cursor + len <=
  // data.size(). Given the strict requirement for Protocol Detection (to
  // disambiguate), strict match is better.
  return (cursor + len) == data.size();
}

int AppMsgParser::getFullFrameSize(const QByteArray &data) {
  if (data.size() < 10)
    return 0;

  int cursor = 0;
  // 0: PRM/Prio (1)
  // 1-2: Index (2)
  // 3-4: Label (2)
  cursor += 5;

  // 5: Source String (Null terminated, max 24 bytes check)
  bool foundNull = false;
  for (int i = 0; i < 24; ++i) { // Check max 24 bytes
    if (cursor >= data.size())
      return 0;
    if (data[cursor] == 0x00) {
      foundNull = true;
      cursor++;
      break;
    }
    cursor++;
  }
  if (!foundNull)
    return 0;

  // Destination String (Null terminated, max 24 bytes check)
  foundNull = false;
  for (int i = 0; i < 24; ++i) {
    if (cursor >= data.size())
      return 0;
    if (data[cursor] == 0x00) {
      foundNull = true;
      cursor++;
      break;
    }
    cursor++;
  }
  if (!foundNull)
    return 0;

  // MSG-TAG (4 bytes)
  if (cursor + 4 > data.size())
    return 0;
  cursor += 4;

  // MSG-Length (A-XDR)
  if (cursor >= data.size())
    return 0;

  // Safety check for decodeLength which assumes enough data
  quint8 firstByte = (quint8)data[cursor];
  if (firstByte & 0x80) {
    int lenBytes = firstByte & 0x7F;
    if (cursor + 1 + lenBytes > data.size()) {
      return 0;
    }
  }

  int len = DataParser::decodeLength(data, cursor);

  return cursor + len;
}

ProtocolSummary AppMsgParser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;

  if (data.size() < 10)
    return summary;

  int cursor = 5; // Skip PRM(1), Index(2), Label(2)

  // Parse Source
  int endSrc = data.indexOf('\0', cursor);
  if (endSrc != -1 && endSrc < cursor + 24) {
    QString src = QString::fromUtf8(data.mid(cursor, endSrc - cursor));
    cursor = endSrc + 1;

    // Parse Dest
    int endDst = data.indexOf('\0', cursor);
    if (endDst != -1 && endDst < cursor + 24) {
      QString dst = QString::fromUtf8(data.mid(cursor, endDst - cursor));
      cursor = endDst + 1;

      summary.address = QString("%1->%2").arg(src).arg(dst);
    }
  }

  // If cursor valid, extract IID/IOP
  // Logic from parse(): IOP (2) then IID (2)
  if (cursor + 4 <= data.size()) {
    uint16_t iop = (uint8_t)data[cursor] |
                   ((uint8_t)data[cursor + 1] << 8); // Little Endian
    uint16_t iid = (uint8_t)data[cursor + 2] |
                   ((uint8_t)data[cursor + 3] << 8); // Little Endian
    summary.messageType = QString("IID:0x%1 IOP:0x%2")
                              .arg(iid, 4, 16, QChar('0'))
                              .arg(iop, 4, 16, QChar('0'))
                              .toUpper();
  }

  return summary;
}

ParsedPacket AppMsgParser::parse(const QByteArray &data) {
  ParsedPacket packet;
  int cursor = 0;

  // 1. Header Parsing
  if (data.size() < 10) { // Minimum header size check
    packet.errorString = "Data too short for header";
    return packet;
  }

  // Byte 0: PRM (bit 0), PRIORITY (bit 1-7)
  uint8_t byte0 = (uint8_t)data[cursor++];
  packet.prm = byte0 & 0x01;
  packet.priority = (byte0 >> 1) & 0x7F;

  // Bytes 1-2: INDEX (Little Endian based on user sample 6b 15 -> 0x156b)
  if (cursor + 2 > data.size())
    return packet;
  packet.index = qFromLittleEndian<uint16_t>(data.mid(cursor, 2).constData());
  cursor += 2;

  // Bytes 3-4: LABEL (Assuming Little Endian consistent with Index)
  if (cursor + 2 > data.size())
    return packet;
  packet.label = qFromLittleEndian<uint16_t>(data.mid(cursor, 2).constData());
  cursor += 2;

  // SOURCE (Null terminated string)
  packet.source = parseString(data, cursor);

  // DESTINATION (Null terminated string)
  packet.destination = parseString(data, cursor);

  // MSG-TAG (4 bytes)
  // Example: 01 00 05 00.
  // Based on user feedback, parsing order is IOP then IID.
  // 01 00 -> IOP = 1
  // 05 00 -> IID = 5
  if (cursor + 4 > data.size()) {
    packet.errorString = "Data too short for TAG";
    return packet;
  }
  packet.iop = qFromLittleEndian<uint16_t>(data.mid(cursor, 2).constData());
  cursor += 2;
  packet.iid = qFromLittleEndian<uint16_t>(data.mid(cursor, 2).constData());
  cursor += 2;

  // MSG-Length (A-XDR)
  packet.msgLength = parseAXDRLength(data, cursor);

  // Payload
  int payloadStart = cursor;
  if (payloadStart + packet.msgLength > data.size()) {
    packet.errorString =
        QString("Payload length mismatch. Expected %1, Available %2")
            .arg(packet.msgLength)
            .arg(data.size() - payloadStart);
    // Continue parsing what we have? Or stop? Let's try to parse.
  }

  // Find definition
  const InterfaceDef *ifDef =
      AppMsgLoader::instance().getInterface(packet.iid, packet.iop);
  if (!ifDef) {
    ParsedField unknown;
    unknown.name = "Unknown Interface";
    unknown.description = QString("IID: 0x%1, IOP: 0x%2")
                              .arg(packet.iid, 4, 16, QChar('0'))
                              .arg(packet.iop, 4, 16, QChar('0'));
    unknown.rawHex = data.mid(payloadStart).toHex();
    packet.payload.append(unknown);
    packet.isValid = true;
    return packet;
  }

  // Determine message type (Request/Response/Event)
  // Usually PRM=1 means Request/Event (Initiator), PRM=0 means Response.
  // But Event is also Initiator.
  // For now, we check if 'event' definition exists and is not NULL format.
  const MessageDef *msgDef = nullptr;
  if (ifDef->type == "event" && ifDef->event.isDefined) {
    msgDef = &ifDef->event;
  } else if (packet.prm == 1 && ifDef->request.isDefined) {
    msgDef = &ifDef->request;
  } else if (packet.prm == 0 && ifDef->response.isDefined) {
    msgDef = &ifDef->response;
  } else if (ifDef->event.isDefined) {
    msgDef = &ifDef->event;
  } else if (ifDef->request.isDefined) {
    msgDef = &ifDef->request;
  } else if (ifDef->response.isDefined) {
    msgDef = &ifDef->response;
  }

  if (msgDef) {
    // 处理消息级别的 optional 标志位
    // 如果整个消息是 optional 的，需要先读取一个字节的存在标志
    if (msgDef->isOptional) {
      if (cursor >= data.size()) {
        packet.errorString = "消息 optional 标志位读取失败: 数据不足";
        return packet;
      }
      uint8_t present = (uint8_t)data[cursor];
      cursor++;
      if (present == 0) {
        // 整个消息不存在
        ParsedField emptyMsg;
        emptyMsg.name = "消息内容";
        emptyMsg.valueStr = "不存在";
        emptyMsg.rawHex = "00";
        emptyMsg.offset = cursor - 1;
        emptyMsg.length = 1;
        packet.payload.append(emptyMsg);
        packet.isValid = true;
        return packet;
      }
      // present != 0, 继续解析实际内容
    }

    QString fmt = msgDef->format.toLower();
    if (fmt == "sequence_of") {
      // format="sequence_of" 时，整个消息是一个数组
      // 先读取 count，再循环解析 element
      int count = DataParser::decodeLength(data, cursor);

      // 通常 fields 中只有一个 element 定义
      if (!msgDef->fields.isEmpty()) {
        const FieldDef &elemDef = msgDef->fields.first();
        ParsedField arrayField;
        arrayField.name = "SEQUENCE OF";
        arrayField.valueStr = QString("Array[%1]").arg(count);
        arrayField.offset = payloadStart;

        for (int i = 0; i < count; i++) {
          ParsedField child = parseFieldRecursive(data, cursor, elemDef, 1);
          child.name = QString("%1[%2]")
                           .arg(elemDef.name.isEmpty() ? "Item" : elemDef.name)
                           .arg(i);
          arrayField.children.append(child);
        }

        arrayField.length = cursor - payloadStart;
        packet.payload.append(arrayField);
      }
    } else {
      // 默认 SEQUENCE 或其它格式，直接遍历 fields
      for (const auto &fieldDef : msgDef->fields) {
        packet.payload.append(parseFieldRecursive(data, cursor, fieldDef, 0));
      }
    }
  }

  packet.isValid = true;
  return packet;
}

static DataType mapToCommonType(FieldType type) {
  switch (type) {
  case FieldType::Null:
    return DataType::Null;
  case FieldType::Unsigned:
    return DataType::Unsigned;
  case FieldType::LongUnsigned:
    return DataType::LongUnsigned;
  case FieldType::DoubleLongUnsigned:
    return DataType::DoubleLongUnsigned;
  case FieldType::DoubleLong:
    return DataType::DoubleLong;
  case FieldType::Long:
    return DataType::Long;
  case FieldType::Integer:
    return DataType::Integer;
  case FieldType::Long64:
    return DataType::Long64;
  case FieldType::Long64Unsigned:
    return DataType::Long64Unsigned;
  case FieldType::Float32:
    return DataType::Float32;
  case FieldType::Float64:
    return DataType::Float64;
  case FieldType::Bool:
    return DataType::Bool;
  case FieldType::BitString:
    return DataType::BitString;
  case FieldType::OctetString:
    return DataType::OctetString;
  case FieldType::VisibleString:
    return DataType::VisibleString;
  case FieldType::Utf8String:
    return DataType::Utf8String;
  case FieldType::DateTime:
    return DataType::DateTime;
  case FieldType::DateTimeS:
    return DataType::DateTimeS;
  case FieldType::Enum:
    return DataType::Enum;
    // Sequence/SequenceOf handled in logic
  default:
    return DataType::Null;
  }
}

ParsedField AppMsgParser::parseFieldRecursive(const QByteArray &data,
                                              int &cursor, const FieldDef &def,
                                              int level) {
  ParsedField result;
  result.name = def.name;
  result.level = level;
  result.offset = cursor;
  result.description = def.description;

  if (cursor >= data.size()) {
    result.valueStr = "End of Stream";
    return result;
  }

  // Handle OPTIONAL
  if (def.isOptional) {
    // A-XDR Optional is usually a boolean (1 byte) indicating presence.
    if (cursor + 1 > data.size())
      return result;
    uint8_t present = (uint8_t)data[cursor];
    cursor++;
    result.length++; // Count the flag byte

    if (present == 0) {
      result.valueStr = "不存在";
      result.rawHex = QString("%1").arg(present, 2, 16, QChar('0'));
      return result;
    }
    // If present, continue to parse the actual type
    // Note: The 'result' will contain the parsed value, but the offset/length
    // should include the flag? Usually we want to show the flag as a separate
    // field or just handle it transparently. Let's handle it transparently: The
    // value is the value.
  }

  switch (def.type) {
  case FieldType::Sequence:
    result.valueStr = "{Structure}";
    for (const auto &childDef : def.children) {
      ParsedField child =
          parseFieldRecursive(data, cursor, childDef, level + 1);
      result.children.append(child);
    }
    break;

  case FieldType::SequenceOf: {
    // A-XDR Sequence Of: Count (Length encoded) + Elements
    int count = DataParser::decodeLength(data, cursor);
    result.valueStr = QString("Array[%1]").arg(count);

    // The element definition is usually the first child
    if (!def.children.isEmpty()) {
      const FieldDef &elemDef = def.children.first();
      for (int i = 0; i < count; i++) {
        ParsedField child =
            parseFieldRecursive(data, cursor, elemDef, level + 1);
        child.name = QString("%1[%2]")
                         .arg(elemDef.name.isEmpty() ? "Item" : elemDef.name)
                         .arg(i);
        result.children.append(child);
      }
    }
    break;
  }

  case FieldType::Choice: {
    // A-XDR Choice: Tag (1 byte) + Value of selected branch
    if (cursor >= data.size()) {
      result.valueStr = "End of Stream";
      break;
    }

    uint8_t tag = (uint8_t)data[cursor];
    cursor++;

    // 查找匹配的 choice 分支
    const FieldDef *selectedBranch = nullptr;
    for (const auto &child : def.children) {
      if (child.choiceValue == tag) {
        selectedBranch = &child;
        break;
      }
    }

    if (selectedBranch) {
      result.valueStr =
          QString("Choice[%1]: %2").arg(tag).arg(selectedBranch->name);

      // 判断分支是否有嵌套类型需要递归解析
      if (selectedBranch->type != FieldType::Unknown &&
          selectedBranch->type != FieldType::Null) {
        // 递归解析分支内容
        ParsedField child =
            parseFieldRecursive(data, cursor, *selectedBranch, level + 1);
        child.name = selectedBranch->name;
        result.children.append(child);
      }
    } else {
      result.valueStr = QString("Choice[%1]: 未知分支").arg(tag);
      result.rawHex = QString("%1").arg(tag, 2, 16, QChar('0'));
    }
    break;
  }

  default:
    // Use Common::DataParser
    {
      DataType commonType = mapToCommonType(def.type);
      if (commonType != DataType::Null) {
        // 传递 fixedSize 用于固定长度字符串解析
        Common::ParsedField commonField =
            DataParser::parse(data, cursor, commonType, false, def.fixedSize);

        result.rawHex = commonField.rawHex;

        // 1. Set Data column for ALL types
        QString formatTag = def.format.trimmed().toLower();
        // If format="hex", show value as Hex
        if (formatTag == "hex") {
          bool ok;
          // Value could be int or uint. Convert to largest uint
          qulonglong valIdx = commonField.value.toULongLong(&ok);
          if (ok) {
            result.valueStr = QString::number(valIdx, 16).toUpper();
            // Add padding if needed? Usually not strictly required unless
            // length known But existing code uses simple conversion. If length
            // is 4, maybe we want 8 chars?
            if (commonField.length > 0) {
              result.valueStr =
                  QString("%1")
                      .arg(valIdx, commonField.length * 2, 16, QChar('0'))
                      .toUpper();
            }
          } else {
            result.valueStr = commonField.valueStr;
          }
        } else if (formatTag == "utc_seconds") {
          bool ok;
          qulonglong seconds = commonField.value.toULongLong(&ok);
          if (ok) {
            QDateTime localTime =
                QDateTime::fromSecsSinceEpoch((qint64)seconds, Qt::LocalTime);
            result.valueStr = localTime.toString("yyyy-MM-dd HH:mm:ss");
          } else {
            result.valueStr = commonField.valueStr;
          }
        } else if (formatTag == "ip") {
          QByteArray ba = commonField.value.toByteArray();
          if (ba.size() == 4) {
            result.valueStr = QString("%1.%2.%3.%4")
                                  .arg((uint8_t)ba[0])
                                  .arg((uint8_t)ba[1])
                                  .arg((uint8_t)ba[2])
                                  .arg((uint8_t)ba[3]);
          } else {
            result.valueStr = commonField.valueStr;
          }
        } else {
          result.valueStr = commonField.valueStr;
        }

        // 2. Prepare Description Value
        QString displayVal = commonField.valueStr;
        if (!def.enumMap.isEmpty() &&
            def.enumMap.contains(commonField.value.toInt())) {
          displayVal = def.enumMap[commonField.value.toInt()];
        }

        // 3. Combine with static description
        // Only append description if it's an Enum translation or unique info
        if (!def.enumMap.isEmpty()) {
          if (result.description.isEmpty()) {
            result.description = displayVal;
          } else {
            result.description = result.description + ": " + displayVal;
          }
        }
        // For other types, result.description (from def.description) is enough.
        // valueStr already contains the value.
        result.length = commonField.length;
        // offset set by assignment at top + shift needed?
        // result.offset was set to 'cursor'. DataParser updates cursor.
        // so result.length = current cursor - result.offset (excluding optional
        // flag) is correct.
      } else {
        result.valueStr = "Unsupported Type";
      }
    }
    break;
  }

  // Update length (especially for Sequence/Optional cases where startCursor
  // wasn't used inside DataParser) Result length should include whatever was
  // consumed. BUT invalidating result.length from DataParser? No. DataParser
  // returns length of the item. If Optional flag consumed 1 byte before,
  // result.length should be DataParser.length + 1. So:
  if (def.isOptional) {
    // Logic above: result.length = 1 (flag).
    // Then we parsed content. Content length needs to be added.
    result.length = (cursor - result.offset);
  } else {
    result.length = (cursor - result.offset);
  }

  if (result.rawHex.isEmpty() && result.length > 0) {
    result.rawHex = data.mid(result.offset, result.length).toHex();
  }

  return result;
}

int AppMsgParser::parseAXDRLength(const QByteArray &data, int &cursor) {
  return DataParser::decodeLength(data, cursor);
}

QString AppMsgParser::parseString(const QByteArray &data, int &cursor) {
  int end = data.indexOf('\0', cursor);
  if (end == -1) {
    // Not found, take rest?
    QString s = QString::fromUtf8(data.mid(cursor));
    cursor = data.size();
    return s;
  }
  QString s = QString::fromUtf8(data.mid(cursor, end - cursor));
  cursor = end + 1; // Skip \0
  return s;
}

QString AppMsgParser::parseAXDRString(const QByteArray &data, int &cursor) {
  Common::ParsedField f =
      DataParser::parse(data, cursor, DataType::Utf8String, false);
  return f.value.toString();
}

QList<AppMsgField> AppMsgParser::parseToFields(const QByteArray &data) {
  QList<AppMsgField> fields;

  // 先解析为内部结构
  ParsedPacket packet = parse(data);

  if (!packet.isValid) {
    if (!packet.errorString.isEmpty()) {
      fields.append(AppMsgField("错误", "", packet.errorString));
    }
    return fields;
  }

  // 帧头格式化
  // Byte 0: 首字节
  uint8_t byte0 = (uint8_t)data[0];
  fields.append(AppMsgField("首字节(1BIN)",
                            QString("%1").arg(byte0, 2, 16, QChar('0')),
                            "首字节", 0, 1, 0));
  fields.append(AppMsgField("PRM-启动标志位(PRM=1, 表示启动): D0",
                            QString("%1").arg(packet.prm),
                            "* 来自启动站(请求或广播)", 0, 1, 1));
  fields.append(
      AppMsgField("PRIORITY-优先级 D1-D7", QString("%1").arg(packet.priority),
                  "* 优先级 (数值越小优先级越高, 0为最高优先级)", 0, 1, 1));

  // Index
  fields.append(AppMsgField(
      "INDEX 消息序号(2BIN)",
      QString("%1").arg(packet.index, 4, 16, QChar('0')),
      QString("%1 * 从0循环递增，响应消息同请求消息保持一致").arg(packet.index),
      1, 2, 0));

  // Label
  fields.append(AppMsgField(
      "LABEL 消息标签(2BIN)",
      QString("%1").arg(packet.label, 4, 16, QChar('0')),
      QString("%1 * 发送方附加标签，响应时带回").arg(packet.label), 3, 2, 0));

  // 计算变长字符串的偏移
  int srcLen = packet.source.toUtf8().length() + 1;
  int dstLen = packet.destination.toUtf8().length() + 1;

  // Source
  QByteArray srcHex = data.mid(5, srcLen);
  fields.append(AppMsgField(
      "SOURCE", srcHex.toHex().toUpper(),
      QString("%1 * 消息发送方名称，字符串，以0结尾").arg(packet.source), 5,
      srcLen, 0));

  // Destination
  QByteArray dstHex = data.mid(5 + srcLen, dstLen);
  fields.append(AppMsgField(
      "DESTINATION", dstHex.toHex().toUpper(),
      QString("%1 * 消息接收方名称，字符串，以0结尾").arg(packet.destination),
      5 + srcLen, dstLen, 0));

  // TAG
  int tagOffset = 5 + srcLen + dstLen;
  QByteArray tagHex = data.mid(tagOffset, 4);
  fields.append(AppMsgField("MSG's-TAG 消息接口ID(4BIN)",
                            tagHex.toHex().toUpper(),
                            QString("IID: 0x%1, IOP: 0x%2 *")
                                .arg(packet.iid, 4, 16, QChar('0'))
                                .arg(packet.iop, 4, 16, QChar('0')),
                            tagOffset, 4, 0));

  // IID/IOP 子行，从 XML 配置中查找中文名称
  QString iidName = AppMsgLoader::instance().getIidName(packet.iid);
  QString iopName = AppMsgLoader::instance().getIopName(packet.iid, packet.iop);

  QString iidDesc = iidName.isEmpty() ? QString("* 接口组ID")
                                      : QString("* 接口组: %1").arg(iidName);
  QString iopDesc = iopName.isEmpty() ? QString("* 接口ID")
                                      : QString("* 接口: %1").arg(iopName);

  fields.append(AppMsgField("IID",
                            QString("%1").arg(packet.iid, 4, 16, QChar('0')),
                            iidDesc, tagOffset + 2, 2, 1));
  fields.append(AppMsgField("IOP",
                            QString("%1").arg(packet.iop, 4, 16, QChar('0')),
                            iopDesc, tagOffset, 2, 1));

  // Length
  int payloadStart = tagOffset + 4;
  int lengthFieldLen = 0;
  if (!packet.payload.isEmpty()) {
    lengthFieldLen = packet.payload.first().offset - payloadStart;
  } else {
    lengthFieldLen = 1;
    if (payloadStart < data.size()) {
      if ((uint8_t)data[payloadStart] < 0x80)
        lengthFieldLen = 1;
      else
        lengthFieldLen = (data[payloadStart] & 0x7F) + 1;
    }
  }

  QByteArray lenHex = data.mid(payloadStart, lengthFieldLen);
  fields.append(
      AppMsgField("MSG's-Length", lenHex.toHex().toUpper(),
                  QString("%1 * 消息有效载荷长度，采用可变长度编码 (A-XDR)")
                      .arg(packet.msgLength),
                  payloadStart, lengthFieldLen, 0));

  // Payload 头
  fields.append(AppMsgField("MSG's-Payload", "", "有效载荷，即消息数据单元",
                            payloadStart + lengthFieldLen, packet.msgLength,
                            0));

  // 递归添加 Payload 字段
  std::function<void(const ParsedField &, int)> addPayloadField;
  addPayloadField = [&](const ParsedField &field, int indent) {
    // Description Column: Value + Description
    QString descStr = field.valueStr;
    if (!field.description.isEmpty()) {
      descStr += " (" + field.description + ")";
    }

    fields.append(AppMsgField(field.name, field.rawHex.toUpper(), descStr,
                              field.offset, field.length, indent));

    for (const auto &child : field.children) {
      addPayloadField(child, indent + 1);
    }
  };

  for (const auto &field : packet.payload) {
    addPayloadField(field, 1);
  }

  return fields;
}

} // namespace AppMsg
