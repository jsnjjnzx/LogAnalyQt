#include "DataParser.h"
#include <QDebug>
#include <QtEndian>
#include <algorithm> // for std::reverse
#include <cmath>
#include <cstdint>

namespace Common {

int DataParser::decodeLength(const QByteArray &data, int &cursor) {
  if (cursor >= data.size())
    return 0;
  uint8_t byte = (uint8_t)data[cursor];
  cursor++;

  if ((byte & 0x80) == 0) {
    return byte;
  } else {
    int numBytes = byte & 0x7F;
    if (numBytes > 4)
      return 0; // Safety check
    if (cursor + numBytes > data.size())
      return 0;

    uint32_t len = 0;
    for (int i = 0; i < numBytes; i++) {
      len = (len << 8) | (uint8_t)data[cursor++];
    }
    return len;
  }
}

QString DataParser::getDataTypeName(DataType type) {
  switch (type) {
  case DataType::Null:
    return "NULL";
  case DataType::Array:
    return "Array";
  case DataType::Structure:
    return "Structure";
  case DataType::Bool:
    return "Bool";
  case DataType::BitString:
    return "BitString";
  case DataType::DoubleLong:
    return "DoubleLong";
  case DataType::DoubleLongUnsigned:
    return "DoubleLongUnsigned";
  case DataType::Float32:
    return "Float32";
  case DataType::OctetString:
    return "OctetString";
  case DataType::VisibleString:
    return "VisibleString";
  case DataType::Utf8String:
    return "Utf8String";
  case DataType::Integer:
    return "Integer";
  case DataType::Long:
    return "Long";
  case DataType::Unsigned:
    return "Unsigned";
  case DataType::LongUnsigned:
    return "LongUnsigned";
  case DataType::Long64:
    return "Long64";
  case DataType::Long64Unsigned:
    return "Long64Unsigned";
  case DataType::Enum:
    return "Enum";
  case DataType::Float64:
    return "Float64";
  case DataType::DateTime:
    return "DateTime";
  case DataType::Date:
    return "Date";
  case DataType::Time:
    return "Time";
  case DataType::DateTimeS:
    return "DateTimeS";

  case DataType::OI:
    return "OI";
  case DataType::OAD:
    return "OAD";
  case DataType::ROAD:
    return "ROAD";
  case DataType::OMD:
    return "OMD";
  case DataType::TI:
    return "TI";
  case DataType::TSA:
    return "TSA";
  case DataType::MAC:
    return "MAC";
  case DataType::RN:
    return "RN";
  case DataType::Region:
    return "Region";
  case DataType::ScalerUnit:
    return "Scaler_Unit";
  case DataType::RSD:
    return "RSD";
  case DataType::CSD:
    return "CSD";
  case DataType::MS:
    return "MS";
  case DataType::SID:
    return "SID";
  case DataType::SID_MAC:
    return "SID_MAC";
  case DataType::COMDCB:
    return "COMDCB";
  case DataType::RCSD:
    return "RCSD";
  case DataType::VQDS:
    return "VQDS";
  case DataType::Checksum:
    return "Checksum";

  default:
    return "Unknown";
  }
}

ParsedField DataParser::parse(const QByteArray &data, int &cursor,
                              DataType type, bool expectTag, int fixedLength) {
  ParsedField field;
  field.offset = cursor;
  int startCursor = cursor;

  if (expectTag) {
    if (cursor >= data.size()) {
      field.description = "End of Data";
      return field;
    }
    uint8_t tag = (uint8_t)data[cursor++];
    type = static_cast<DataType>(tag);
  }

  if (cursor >= data.size() && type != DataType::Null) {
    field.length = cursor - startCursor;
    field.description = "Data too short";
    return field;
  }

  switch (type) {
  case DataType::Null:
    field.valueStr = "NULL";
    field.description = "空数据";
    break;

  case DataType::Unsigned: // 1 byte
  case DataType::Integer:
  case DataType::Enum:
    if (cursor < data.size()) {
      uint8_t val = (uint8_t)data[cursor++];
      if (type == DataType::Integer) {
        field.value = (int8_t)val;
        field.valueStr = QString::number((int8_t)val);
      } else {
        field.value = val;
        field.valueStr = QString::number(val);
      }
    }
    break;

  case DataType::Bool:
    if (cursor < data.size()) {
      uint8_t val = (uint8_t)data[cursor++];
      field.value = (bool)val;
      field.valueStr = val ? "True" : "False";
    }
    break;

  case DataType::LongUnsigned: // 2 bytes
  case DataType::Long:
    if (cursor + 2 <= data.size()) {
      uint16_t val = qFromBigEndian<uint16_t>(data.mid(cursor, 2).constData());
      cursor += 2;
      if (type == DataType::Long) {
        field.value = (int16_t)val;
        field.valueStr = QString::number((int16_t)val);
      } else {
        field.value = val;
        field.valueStr = QString::number(val);
      }
    }
    break;

  case DataType::DoubleLongUnsigned: // 4 bytes
  case DataType::DoubleLong:
  case DataType::Float32:
    if (cursor + 4 <= data.size()) {
      if (type == DataType::Float32) {
        float val;
        QByteArray tmp = data.mid(cursor, 4);
        if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
          std::reverse(tmp.begin(), tmp.end());
        }
        memcpy(&val, tmp.constData(), 4);
        field.value = val;
        field.valueStr = QString::number(val);
      } else {
        uint32_t val =
            qFromBigEndian<uint32_t>(data.mid(cursor, 4).constData());
        if (type == DataType::DoubleLong) {
          field.value = (int32_t)val;
          field.valueStr = QString::number((int32_t)val);
        } else {
          field.value = val;
          field.valueStr = QString::number(val);
        }
      }
      cursor += 4;
    }
    break;

  case DataType::Long64Unsigned: // 8 bytes
  case DataType::Long64:
  case DataType::Float64:
    if (cursor + 8 <= data.size()) {
      if (type == DataType::Float64) {
        double val;
        QByteArray tmp = data.mid(cursor, 8);
        if (QSysInfo::ByteOrder == QSysInfo::LittleEndian) {
          std::reverse(tmp.begin(), tmp.end());
        }
        memcpy(&val, tmp.constData(), 8);
        field.value = val;
        field.valueStr = QString::number(val);
      } else {
        uint64_t val =
            qFromBigEndian<uint64_t>(data.mid(cursor, 8).constData());
        if (type == DataType::Long64) {
          field.value = (qint64)val; // qint64 for QVariant
          field.valueStr = QString::number((qint64)val);
        } else {
          field.value = (qulonglong)val;
          field.valueStr = QString::number(val);
        }
      }
      cursor += 8;
    }
    break;

  case DataType::VisibleString:
  case DataType::Utf8String:
  case DataType::OctetString: {
    // 如果指定了固定长度，直接使用；否则从数据中读取长度字节
    int len = (fixedLength > 0) ? fixedLength : decodeLength(data, cursor);
    if (cursor + len <= data.size()) {
      QByteArray val = data.mid(cursor, len);
      cursor += len;

      if (type == DataType::OctetString) {
        field.value = val;
        field.valueStr = val.toHex().toUpper();
      } else if (type == DataType::VisibleString) {
        QString s = QString::fromLatin1(val);
        field.value = s;
        field.valueStr = s;
      } else {
        QString s = QString::fromUtf8(val);
        field.value = s;
        field.valueStr = s;
      }
    }
    break;
  }

  case DataType::BitString: {
    int bitLen = 0;
    if (expectTag) {
      bitLen = decodeLength(data, cursor);
    } else {
      // For Module/App, BitString usually has length encoded too?
      bitLen = decodeLength(data, cursor);
    }

    int byteLen = (bitLen + 7) / 8;
    if (cursor + byteLen <= data.size()) {
      QByteArray val = data.mid(cursor, byteLen);
      cursor += byteLen;
      field.value = val;
      field.valueStr = val.toHex().toUpper();

      QString binStr;
      for (int i = 0; i < bitLen; ++i) {
        int byteIdx = i / 8;
        int bitIdx = 7 - (i % 8);
        if (byteIdx < val.size()) {
          binStr += ((val[byteIdx] >> bitIdx) & 0x01) ? "1 " : "0 ";
        }
      }
      field.description = QString("%1").arg(binStr.trimmed());
    }
    break;
  }

  case DataType::DateTime: {
    if (cursor + 10 <= data.size()) {
      uint16_t year = ((uint8_t)data[cursor] << 8) | (uint8_t)data[cursor + 1];
      uint8_t month = data[cursor + 2];
      uint8_t day = data[cursor + 3];
      uint8_t week = data[cursor + 4];
      uint8_t h = data[cursor + 5];
      uint8_t m = data[cursor + 6];
      uint8_t s = data[cursor + 7];
      uint16_t ms =
          ((uint8_t)data[cursor + 8] << 8) | (uint8_t)data[cursor + 9];

      QString dateStr = QString("%1-%2-%3 W%4 %5:%6:%7.%8")
                            .arg(year, 4, 10, QChar('0'))
                            .arg(month, 2, 10, QChar('0'))
                            .arg(day, 2, 10, QChar('0'))
                            .arg(week)
                            .arg(h, 2, 10, QChar('0'))
                            .arg(m, 2, 10, QChar('0'))
                            .arg(s, 2, 10, QChar('0'))
                            .arg(ms, 3, 10, QChar('0'));
      field.value = dateStr;
      field.valueStr = data.mid(cursor, 10).toHex().toUpper();
      field.description = dateStr;
      cursor += 10;
    }
    break;
  }

  case DataType::DateTimeS: {
    if (cursor + 7 <= data.size()) {
      uint16_t year = ((uint8_t)data[cursor] << 8) | (uint8_t)data[cursor + 1];
      uint8_t month = data[cursor + 2];
      uint8_t day = data[cursor + 3];
      uint8_t h = data[cursor + 4];
      uint8_t m = data[cursor + 5];
      uint8_t s = data[cursor + 6];

      QString dateStr = QString("%1-%2-%3 %4:%5:%6")
                            .arg(year, 4, 10, QChar('0'))
                            .arg(month, 2, 10, QChar('0'))
                            .arg(day, 2, 10, QChar('0'))
                            .arg(h, 2, 10, QChar('0'))
                            .arg(m, 2, 10, QChar('0'))
                            .arg(s, 2, 10, QChar('0'));
      field.value = dateStr;
      field.valueStr = dateStr;
      field.description = dateStr;
      cursor += 7;
    }
    break;
  }

  // Extended types placeholder
  case DataType::OI:
  case DataType::OAD:
  case DataType::ROAD:
  case DataType::OMD:
  case DataType::TI:
  case DataType::TSA:
  case DataType::MAC:
  case DataType::RN:
  case DataType::Region:
  case DataType::ScalerUnit:
  case DataType::RSD:
  case DataType::CSD:
  case DataType::MS:
  case DataType::SID:
  case DataType::SID_MAC:
  case DataType::COMDCB:
  case DataType::RCSD:
  case DataType::VQDS:
  case DataType::Checksum:
    field.description = getDataTypeName(type) + " (Not Implemented)";
    break;

  case DataType::Array:
  case DataType::Structure:
    if (expectTag) {
      if (cursor < data.size()) {
        int count = (uint8_t)data[cursor++];
        field.description = QString("Count: %1").arg(count);

        for (int i = 0; i < count; i++) {
          // In 698, elements are self-describing
          ParsedField child = parse(data, cursor, DataType::Null, true);
          field.children.append(child);
        }
      }
    } else {
      field.description = "{Structure}";
    }
    break;

  default:
    field.description = "Unsupported Type";
    break;
  }

  field.length = cursor - field.offset;
  if (field.length > 0) {
    field.rawHex = data.mid(field.offset, field.length).toHex().toUpper();
  }
  return field;
}

} // namespace Common
