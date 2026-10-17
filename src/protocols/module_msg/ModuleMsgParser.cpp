#include "ModuleMsgParser.h"
#include "ModuleMsgLoader.h"
#include "ProtocolDetector.h"
#include "common/DataParser.h"
#include <QDebug>
#include <QtEndian>
#include <cstdint>


using namespace Common;

namespace ModuleMsg {

ModuleMsgParser::ModuleMsgParser() {}

// 静态方法：检查 ModuleMsg 帧是否有效（用于协议自动检测）
// 帧结构: 68(1) + L(2) + Control(2) + APDU(L) + FCS(2) + 16(1) = L + 8
// 最小帧长度: 当 L=0 时为 8 字节
bool ModuleMsgParser::isValidFrame(const QByteArray &data) {
  if (data.size() < 8)
    return false;
  if ((uint8_t)data[0] != 0x68)
    return false;
  if ((uint8_t)data[data.size() - 1] != 0x16)
    return false;

  uint16_t crc = 0xFFFF;
  for (int i = 1; i < data.size() - 3; ++i) {
    crc ^= (uint8_t)data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0x8408;
      else
        crc >>= 1;
    }
  }
  uint16_t expectedFcs = crc ^ 0xFFFF;
  int fcsOffset = data.size() - 3;
  uint16_t actualFcs =
      (uint8_t)data[fcsOffset] | ((uint8_t)data[fcsOffset + 1] << 8);

  return expectedFcs == actualFcs;
}

#include <cassert>
#include <cstdio>

static const int OFF_FID = 4;
static const int OFF_APDU = 5;

ProtocolSummary ModuleMsgParser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;
  if (data.size() < 8)
    return summary;

  uint8_t fid = (uint8_t)data[OFF_FID];
  uint8_t apduType = (uint8_t)data[OFF_APDU];
  uint16_t dt = 0;

  if (apduType == 1 || apduType == 129) {
    dt = 0x0001;
  } else if (data.size() >= 8) {
    dt = ((uint8_t)data[OFF_APDU + 1] << 8) | (uint8_t)data[OFF_APDU + 2];
  }

  if (dt != 0) {
    uint16_t dta1 = (dt >> 12) & 0x000F;
    uint16_t lookupDt = dt & 0x0FFF;

    QString ifName;
    const InterfaceDef *ifDef =
        ModuleMsgLoader::instance().getInterface(dta1, lookupDt);
    if (ifDef)
      ifName = ifDef->name;

    summary.messageType = QString("DT: 0x%1 %2(FID:%3) Type:%4")
                              .arg(dt, 4, 16, QChar('0'))
                              .arg(ifName.isEmpty() ? "" : "(" + ifName + ") ")
                              .arg(fid)
                              .arg(apduType);
  } else {
    summary.messageType =
        QString("Unknown Type:%1 (FID:%2)").arg(apduType).arg(fid);
  }

  summary.messageType = summary.messageType.toUpper();
  summary.address = "ModuleMsg";
  return summary;
}

ParsedPacket ModuleMsgParser::parse(const QByteArray &data) {
  ParsedPacket packet;
  int cursor = 0;

  if (data.size() < 8) {
    packet.errorString = "Data too short";
    return packet;
  }

  // 1. Start Byte
  if ((uint8_t)data[cursor] != 0x68) {
    packet.errorString = "Invalid Start Byte";
    return packet;
  }
  ParsedField startField;
  startField.name = "帧头";
  startField.valueStr = "68";
  startField.rawHex = "68";
  startField.description = "起始符";
  startField.offset = 0;
  startField.length = 1;
  packet.payload.append(startField);
  cursor++;

  // 2. Length
  uint16_t apduLen = (uint8_t)data[cursor] | ((uint8_t)data[cursor + 1] << 8);
  packet.msgLength = apduLen;
  ParsedField lenField;
  lenField.name = "长度";
  lenField.valueStr = QString::number(apduLen);
  lenField.rawHex = data.mid(cursor, 2).toHex().toUpper();
  lenField.description = QString("APDU长度: %1").arg(apduLen);
  lenField.offset = 1;
  lenField.length = 2;
  packet.payload.append(lenField);
  cursor += 2;

  if (data.size() < apduLen + 8) {
    packet.errorString = QString("Data too short. Expected %1, got %2")
                             .arg(apduLen + 8)
                             .arg(data.size());
    return packet;
  }

  // 3. Control Field (2 Bytes -> Control + FID)
  uint8_t ctrlByte = (uint8_t)data[cursor];
  uint8_t fidByte = (uint8_t)data[cursor + 1];

  packet.prm = (ctrlByte >> 6) & 0x01;
  packet.fid = fidByte;

  ParsedField ctrlField;
  ctrlField.name = "控制域";
  ctrlField.rawHex = data.mid(cursor, 2).toHex().toUpper();
  ctrlField.valueStr = ctrlField.rawHex;
  ctrlField.description = "Control Field";
  ctrlField.offset = 3;
  ctrlField.length = 2;

  // Sub-fields for Control
  ParsedField dirField;
  dirField.name = "传输方向";
  dirField.valueStr = (ctrlByte & 0x80) ? "1" : "0";
  dirField.rawHex = QString("%1").arg((ctrlByte & 0x80) >> 7);
  dirField.description = (ctrlByte & 0x80) ? "模块 -> 主站" : "主站 -> 模块";
  dirField.offset = 3;
  dirField.length = 1; // Bit within Byte 3
  ctrlField.children.append(dirField);

  ParsedField prmField;
  prmField.name = "启动标志";
  prmField.valueStr = (ctrlByte & 0x40) ? "1" : "0";
  prmField.rawHex = QString("%1").arg((ctrlByte & 0x40) >> 6);
  prmField.description = (ctrlByte & 0x40) ? "启动站" : "从动站";
  prmField.offset = 3;
  prmField.length = 1; // Bit within Byte 3
  ctrlField.children.append(prmField);

  ParsedField fidField;
  fidField.name = "帧序号";
  fidField.valueStr = QString::number(fidByte);
  fidField.rawHex = QString("%1").arg(fidByte, 2, 16, QChar('0')).toUpper();
  fidField.description = QString("FID: %1").arg(fidByte); // 显示十进制值
  fidField.offset = 4;
  fidField.length = 1;
  ctrlField.children.append(fidField);

  packet.payload.append(ctrlField);
  cursor += 2; // Skipped Ctrl + FID

  // 5. APDU Start
  packet.apduType = (uint8_t)data[cursor];
  ParsedField typeField;
  typeField.name = "应用协议数据单元类型";
  typeField.valueStr = QString::number(packet.apduType);
  typeField.rawHex = data.mid(cursor, 1).toHex().toUpper();
  typeField.offset = 5;
  typeField.length = 1;
  // Map Type to Name
  QString typeName;
  switch (packet.apduType) {
  case 1:
    typeName = "LINK_REQUEST";
    break;
  case 2:
    typeName = "GET_REQUEST";
    break;
  case 3:
    typeName = "SET_REQUEST";
    break;
  case 4:
    typeName = "REPORT_NOTIFICATION";
    break;
  case 129:
    typeName = "LINK_RESPONSE";
    break;
  case 130:
    typeName = "GET_RESPONSE";
    break;
  case 131:
    typeName = "SET_RESPONSE";
    break;
  case 132:
    typeName = "REPORT_RESPONSE";
    break;
  default:
    typeName = "UNKNOWN";
    break;
  }
  typeField.description = typeName;
  packet.payload.append(typeField);
  cursor++;

  bool hasDtInPayload = true;
  if (packet.apduType == 1 || packet.apduType == 129) {
    hasDtInPayload = false;
    packet.dt = 0x0001;
  }

  if (hasDtInPayload) {
    if (cursor + 2 > data.size()) {
      packet.errorString = "Payload too short for DT";
      return packet;
    }
    packet.dt = ((uint8_t)data[cursor] << 8) | (uint8_t)data[cursor + 1];

    ParsedField dtField;
    dtField.name = "信息类标识";
    dtField.rawHex = data.mid(cursor, 2).toHex().toUpper();
    dtField.valueStr = dtField.rawHex;
    dtField.offset = 6;
    dtField.length = 2;

    packet.dta1 = (packet.dt >> 12) & 0x000F;
    uint16_t lookupDt = packet.dt & 0x0FFF;
    const InterfaceDef *ifDef =
        ModuleMsgLoader::instance().getInterface(packet.dta1, lookupDt);
    if (ifDef)
      dtField.description = ifDef->name;
    else
      dtField.description = "Unknown DT";

    packet.payload.append(dtField);
    cursor += 2;
  } else {
    packet.dta1 = (packet.dt >> 12) & 0x000F;
  }

  uint16_t lookupDt = packet.dt & 0x0FFF;

  int remainingApduBytes = apduLen - 1 - (hasDtInPayload ? 2 : 0);

  if (remainingApduBytes > 0) {
    int payloadStartOffset = cursor; // This IS the absolute offset in `data`
    QByteArray payloadData = data.mid(cursor, remainingApduBytes);
    int pCursor = 0;

    const InterfaceDef *ifDef =
        ModuleMsgLoader::instance().getInterface(packet.dta1, lookupDt);

    if (!ifDef) {
      ParsedField unknown;
      unknown.name = "Payload";
      unknown.description =
          QString("Unknown DT: 0x%1").arg(packet.dt, 4, 16, QChar('0'));
      unknown.valueStr = payloadData.toHex().toUpper();
      packet.payload.append(unknown);
    } else {
      const MessageDef *msgDef = nullptr;

      // Map APDU Type to XML Definitions
      switch (packet.apduType) {
      case 1:
        msgDef = &ifDef->request;
        break;
      case 129:
        msgDef = &ifDef->response;
        break;
      case 2:
        msgDef = nullptr;
        break;
      case 3:
        msgDef = &ifDef->request;
        break;
      case 4:
        msgDef =
            (!ifDef->event.fields.isEmpty()) ? &ifDef->event : &ifDef->response;
        break;
      case 130:
        msgDef = &ifDef->response;
        break; // GetResponse -> Response
      case 131:
        msgDef = nullptr;
        break;
      default:
        msgDef = (packet.prm == 1) ? &ifDef->request : &ifDef->response;
        break;
      }

      if (packet.apduType == 2) {
        ParsedField info;
        info.name = "Request Info";
        info.description = "Get Request (Only DT)";
        packet.payload.append(info);
      } else if (packet.apduType == 131) {
        if (pCursor < payloadData.size()) {
          uint8_t dar = (uint8_t)payloadData[pCursor++];
          ParsedField darField;
          darField.name = "DAR";
          darField.valueStr =
              QString("%1").arg(dar, 2, 16, QChar('0')).toUpper();
          darField.description = "Set Result Code";
          darField.rawHex = darField.valueStr;
          darField.offset = payloadStartOffset + (pCursor - 1);
          darField.length = 1;
          packet.payload.append(darField);
        }
      } else if (msgDef) {
        bool canParseFields = true;

        // Type 1/129/3: Sequence Wrapper
        if (packet.apduType == 1 || packet.apduType == 129 ||
            packet.apduType == 3) {
          if (pCursor < payloadData.size() &&
              (uint8_t)payloadData[pCursor] == 0x02) {
            pCursor++;
            DataParser::decodeLength(payloadData, pCursor);
          }
        }

        // Type 4/130: A-ResultNormal Wrapper
        if (packet.apduType == 4 || packet.apduType == 130) {
          if (pCursor < payloadData.size()) {
            int startPCursor = pCursor;
            uint8_t resultTag = (uint8_t)payloadData[pCursor++];
            ParsedField resultField;
            resultField.name = "Get-Result";
            resultField.rawHex =
                QString("%1").arg(resultTag, 2, 16, QChar('0')).toUpper();
            resultField.offset = payloadStartOffset + startPCursor;
            resultField.length = 1;

            if (resultTag == 0x01) { // Data
              resultField.description = "Data Present";
              resultField.valueStr = "01";
              packet.payload.append(resultField);

              if (pCursor < payloadData.size()) {
                int seqStart = pCursor;
                uint8_t seqTag = (uint8_t)payloadData[pCursor];
                if (seqTag == 0x02) {
                  pCursor++;
                  ParsedField seqField;
                  seqField.name = "Structure";
                  seqField.rawHex = "02";

                  if (pCursor < payloadData.size()) {
                    uint8_t count = (uint8_t)payloadData[pCursor++];
                    seqField.valueStr = "Struct Tag";
                    seqField.description = QString("Count: %1").arg(count);
                    seqField.rawHex +=
                        QString("%1").arg(count, 2, 16, QChar('0')).toUpper();

                    seqField.offset = payloadStartOffset + seqStart;
                    seqField.length = pCursor - seqStart;

                    packet.payload.append(seqField);
                  }
                }
              }
            } else if (resultTag == 0x00) { // DAR
              resultField.description = "Error (DAR)";
              resultField.valueStr = "00";
              packet.payload.append(resultField);
              if (pCursor < payloadData.size()) {
                int darStart = pCursor;
                uint8_t dar = (uint8_t)payloadData[pCursor++];
                ParsedField darField;
                darField.name = "DAR";
                darField.valueStr =
                    QString("%1").arg(dar, 2, 16, QChar('0')).toUpper();
                darField.description = "Error Code";
                darField.offset = payloadStartOffset + darStart;
                darField.length = 1;
                packet.payload.append(darField);
              }
              canParseFields = false;
            } else {
              canParseFields = false;
            }
          } else {
            canParseFields = false;
          }
        }

        if (canParseFields) {
          // Check if we should use tagged parsing based on APDU Type
          bool useTags = (packet.apduType == 4 || packet.apduType == 130 ||
                          packet.apduType == 3 || packet.apduType == 131);
          for (const auto &fieldDef : msgDef->fields) {
            packet.payload.append(
                parseFieldRecursive(payloadData, pCursor, fieldDef, 0,
                                    payloadStartOffset, useTags));
          }
        }
      }
    }
  }

  // Add FCS and End Byte
  ParsedField fcsField;
  fcsField.name = "帧校验序列(FCS)";
  int fcsOffset = data.size() - 3;
  fcsField.rawHex = data.mid(fcsOffset, 2).toHex().toUpper();
  fcsField.valueStr = fcsField.rawHex;
  // 计算期望 FCS
  uint16_t crc = 0xFFFF;
  for (int i = 1; i < data.size() - 3; ++i) {
    crc ^= (uint8_t)data[i];
    for (int j = 0; j < 8; ++j) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0x8408;
      else
        crc >>= 1;
    }
  }
  uint16_t expectedFcs = crc ^ 0xFFFF;
  uint16_t actualFcs =
      (uint8_t)data[fcsOffset] | ((uint8_t)data[fcsOffset + 1] << 8);
  QString expectedStr =
      QString("%1").arg(expectedFcs, 4, 16, QChar('0')).toUpper();
  fcsField.description = (expectedFcs == actualFcs)
                             ? "OK"
                             : QString("Fail (Calc: %1)").arg(expectedStr);
  fcsField.offset = fcsOffset;
  fcsField.length = 2;
  packet.payload.append(fcsField);

  ParsedField endField;
  endField.name = "结束符";
  endField.rawHex = "16";
  endField.valueStr = "16";
  endField.offset = data.size() - 1;
  endField.length = 1;
  packet.payload.append(endField);

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

ParsedField ModuleMsgParser::parseFieldRecursive(const QByteArray &data,
                                                 int &cursor,
                                                 const FieldDef &def, int level,
                                                 int baseOffset,
                                                 bool expectTag) {
  ParsedField result;
  result.name = def.name;
  result.level = level;
  result.offset = baseOffset + cursor; // Absolute offset
  result.description = def.description;
  int startCursor = cursor; // Store local start cursor

  if (cursor >= data.size()) {
    result.valueStr = "EOS";
    return result;
  }

  uint8_t tag = (uint8_t)data[cursor];

  switch (def.type) {
  case FieldType::Sequence:
    if (expectTag) {
      if ((tag == 0x02) ||
          (tag ==
           0x30)) { // 0x30 is standard ASN.1 Sequence, 0x02 is 698 Structure
        cursor++;   // consume tag
        if (cursor < data.size()) {
          // If 0x02 (Structure), next byte is count.
          // If 0x30 (Sequence), next is length.
          // 698 uses 0x02 for Structure.
          if (tag == 0x02) {
            uint8_t cnt = (uint8_t)data[cursor++];
            result.valueStr = QString("Struct[%1]").arg(cnt);
          } else {
            int seqLen = DataParser::decodeLength(data, cursor);
            result.valueStr = QString("SeqLen:%1").arg(seqLen);
          }
        }
      } else {
        // Not a sequence tag?
        // If we expect tag but don't find it, maybe specific field is missing
        // or untagged? But for now assume structure tag matches.
      }
    } else {
      if (tag == 0x02) {
        cursor++;
        if (cursor < data.size()) {
          uint8_t cnt = (uint8_t)data[cursor++];
          result.valueStr = QString("Struct[%1]").arg(cnt);
        }
      } else {
        result.valueStr = "{Structure}";
      }
    }

    for (const auto &childDef : def.children) {
      ParsedField child = parseFieldRecursive(data, cursor, childDef, level + 1,
                                              baseOffset, expectTag);
      result.children.append(child);
    }
    break;

  case FieldType::SequenceOf: {
    int count = 0;
    if (expectTag) {
      if (tag == 0x01) { // Array Tag
        cursor++;
        if (cursor < data.size())
          count = DataParser::decodeLength(
              data, cursor); // Typically just byte for count in 698
        // Actually, 698 Array: Tag(01) Length(L) Elements...
        // Wait, standard 698 says: "Array: tag 01, followed by length L,
        // followed by L elements" ?? No. 698.45: "Array: 01 + NumElements (1-n
        // bytes length encoding)" decodeLength handles the length encoding.
      } else {
        // If tag is not 01, maybe it is implicitly encoded? or blindly read
        // length?
        count = DataParser::decodeLength(data, cursor);
      }
    } else {
      if (tag == 0x01) { // Array Tag
        cursor++;
        if (cursor < data.size())
          count = (uint8_t)data[cursor++];
      } else {
        count = DataParser::decodeLength(data, cursor);
      }
    }
    result.description = QString("Array[%1]").arg(count);
    if (!def.children.isEmpty()) {
      const FieldDef &elemDef = def.children.first();
      if (count > 100)
        count = 100;
      for (int i = 0; i < count; i++) {
        ParsedField child = parseFieldRecursive(
            data, cursor, elemDef, level + 1, baseOffset, expectTag);
        child.name = QString("%1[%2]").arg(elemDef.name).arg(i);
        result.children.append(child);
      }
    }
    break;
  }

  default:
    // Use Common::DataParser
    {
      DataType commonType = mapToCommonType(def.type);
      if (commonType != DataType::Null) {
        // 传递 fixedSize 用于固定长度字符串解析
        Common::ParsedField commonField = DataParser::parse(
            data, cursor, commonType, expectTag, def.fixedSize);

        result.rawHex = commonField.rawHex; // Must be set before valueStr

        // For ALL common types, we want Raw Hex in Data column (valueStr)
        // And human-readable value in Description column.

        // 1. Set Data column to Hex
        result.valueStr = result.rawHex;

        // 2. Prepare Description
        QString displayVal = commonField.valueStr; // Default: Decimal or String

        if (def.type == FieldType::Enum &&
            def.enumMap.contains(commonField.value.toInt())) {
          displayVal = def.enumMap[commonField.value.toInt()];
        } else if (def.type == FieldType::BitString) {
          displayVal =
              commonField.description; // Binary digits from previous step
        }

        // 3. Set Description
        if (result.description.isEmpty()) {
          result.description = displayVal;
        } else {
          // If XML defines a description (e.g. "Unit: ms"), stick value to it?
          // User wants meaningful value.
          // e.g. "Unit: ms: 100" or just "100 (Unit: ms)"?
          // Usually description in XML is static help text.
          // Let's format as: "Value (StaticDesc)" or "StaticDesc: Value"
          // If static looks like unit "Unit: ms", "100" -> "100 (Unit: ms)"
          // seems readable. But if Static is "Status Bit", "Value" -> "Enabled
          // (Status Bit)". Let's append: "StaticDesc: Value"
          result.description = result.description + ": " + displayVal;
        }
        result.length = commonField.length;
        // offset of result is already set to baseOffset + startCursor
      } else {
        result.valueStr = "Unsupported / Map Error";
        // Fallback or leave as is?
      }
    }
    break;
  }

  // Update length if not set by DataParser block (e.g. Sequence)
  if (result.length == 0) {
    result.length = cursor - startCursor;
  }

  if (result.rawHex.isEmpty() && result.length > 0)
    result.rawHex =
        data.mid(result.offset - baseOffset, result.length).toHex().toUpper();
  return result;
}

int ModuleMsgParser::parseAXDRLength(const QByteArray &data, int &cursor) {
  return DataParser::decodeLength(data, cursor);
}

QString ModuleMsgParser::parseString(const QByteArray &data, int &cursor) {
  int end = data.indexOf('\0', cursor);
  if (end == -1) {
    QString s = QString::fromUtf8(data.mid(cursor));
    cursor = data.size();
    return s;
  }
  QString s = QString::fromUtf8(data.mid(cursor, end - cursor));
  cursor = end + 1;
  return s;
}

QString ModuleMsgParser::parseAXDRString(const QByteArray &data, int &cursor) {
  // This is essentially VisibleString/Utf8String parsing in DataParser
  // Can just call DataParser with VisibleString?
  // But this returns string, DataParser returns field.
  // Let's iterate using DataParser logic
  Common::ParsedField f =
      DataParser::parse(data, cursor, DataType::Utf8String, false);
  return f.value.toString();
}

QList<ModuleMsgField> ModuleMsgParser::parseToFields(const QByteArray &data) {
  QList<ModuleMsgField> fields;

  // 先解析获取 ParsedPacket
  ParsedPacket packet = parse(data);

  // 如果有错误信息，添加错误字段
  if (!packet.errorString.isEmpty()) {
    fields.append(ModuleMsgField("Error", "", packet.errorString, -1, -1, 0));
  }

  // 即使 isValid 为 false，也尝试添加已解析的 payload 字段（可能有部分结果）

  // 递归函数：将嵌套的 ParsedField 展开为扁平的 ModuleMsgField 列表
  std::function<void(const ParsedField &, int)> addFieldRecursive;
  addFieldRecursive = [&](const ParsedField &f, int indent) {
    // 数据列始终使用 rawHex（原始字节串）
    QString val = f.rawHex.toUpper();

    // 说明列使用 description，如果为空则使用 valueStr
    QString desc = f.description;
    if (desc.isEmpty() && !f.valueStr.isEmpty()) {
      desc = f.valueStr;
    }

    fields.append(
        ModuleMsgField(f.name, val, desc, f.offset, f.length, indent));

    // 递归处理子字段
    for (const auto &child : f.children) {
      addFieldRecursive(child, indent + 1);
    }
  };

  // 将 payload 中的所有字段展开到 fields
  for (const auto &f : packet.payload) {
    addFieldRecursive(f, 0);
  }

  return fields;
}

} // namespace ModuleMsg
