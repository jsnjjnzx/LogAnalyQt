#include "AppMsgDataEncoder.h"
#include <QSysInfo>
#include <QtEndian>
#include <cstring>

namespace AppMsg {

QByteArray AppMsgDataEncoder::encodeLength(int len) {
  QByteArray output;
  if (len < 0x80) {
    output.append(static_cast<char>(len));
  } else if (len <= 0xFF) {
    output.append(static_cast<char>(0x81));
    output.append(static_cast<char>(len));
  } else {
    output.append(static_cast<char>(0x82));
    output.append(static_cast<char>((len >> 8) & 0xFF));
    output.append(static_cast<char>(len & 0xFF));
  }
  return output;
}

bool AppMsgDataEncoder::parseHexBytes(const QString &text, QByteArray *bytes,
                                      QString *err, int expectedBytes) {
  QString cleanText = text;
  cleanText.remove("0x", Qt::CaseInsensitive);
  cleanText.remove(' ');
  cleanText.remove('\n');
  cleanText.remove('\r');
  cleanText.remove('\t');
  cleanText.remove(':');
  if (cleanText.size() % 2 != 0) {
    if (err)
      *err = "十六进制字符串长度必须为偶数";
    return false;
  }
  QByteArray parsed = QByteArray::fromHex(cleanText.toLatin1());
  if (parsed.size() * 2 != cleanText.size()) {
    if (err)
      *err = "十六进制字符串包含非法字符";
    return false;
  }
  if (expectedBytes >= 0 && parsed.size() != expectedBytes) {
    if (err)
      *err = QString("需要 %1 字节，实际 %2 字节")
                 .arg(expectedBytes)
                 .arg(parsed.size());
    return false;
  }
  if (bytes)
    *bytes = parsed;
  return true;
}

QByteArray AppMsgDataEncoder::encodeDateTime(const QVariantMap &value,
                                             bool shortFormat, QString *err) {
  int year = value.value("year").toInt();
  int month = value.value("month").toInt();
  int day = value.value("day").toInt();
  int hour = value.value("hour").toInt();
  int minute = value.value("minute", value.value("min")).toInt();
  int second = value.value("second", value.value("sec")).toInt();
  if (year < 0 || year > 0xFFFF || month < 0 || month > 12 || day < 0 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    if (err)
      *err = "时间字段范围不合法";
    return {};
  }

  QByteArray output;
  output.append(static_cast<char>((year >> 8) & 0xFF));
  output.append(static_cast<char>(year & 0xFF));
  output.append(static_cast<char>(month & 0xFF));
  output.append(static_cast<char>(day & 0xFF));
  if (!shortFormat)
    output.append(static_cast<char>(value.value("week", 0xFF).toInt() & 0xFF));
  output.append(static_cast<char>(hour & 0xFF));
  output.append(static_cast<char>(minute & 0xFF));
  output.append(static_cast<char>(second & 0xFF));
  if (!shortFormat) {
    int ms = value.value("ms", 0).toInt();
    output.append(static_cast<char>((ms >> 8) & 0xFF));
    output.append(static_cast<char>(ms & 0xFF));
  }
  return output;
}

QByteArray AppMsgDataEncoder::encodeField(const FieldDef &def,
                                          const QVariant &value,
                                          QString *err) {
  QVariant actualValue = value;
  if (def.isOptional) {
    QVariantMap optionalValue = value.toMap();
    bool present = optionalValue.contains("present")
                       ? optionalValue.value("present").toBool()
                       : value.isValid() && !value.isNull();
    QByteArray output;
    output.append(static_cast<char>(present ? 1 : 0));
    if (!present)
      return output;
    if (optionalValue.contains("value"))
      actualValue = optionalValue.value("value");
  }

  switch (def.type) {
  case FieldType::Sequence: {
    QVariantList values = actualValue.toList();
    QByteArray output;
    for (int childIndex = 0; childIndex < def.children.size(); ++childIndex) {
      QString childErr;
      QVariant childValue = childIndex < values.size() ? values.at(childIndex)
                                                       : QVariant();
      QByteArray childBytes =
          encodeField(def.children.at(childIndex), childValue, &childErr);
      if (!childErr.isEmpty()) {
        if (err)
          *err = QString("%1.%2 编码失败：%3")
                     .arg(def.name)
                     .arg(def.children.at(childIndex).name)
                     .arg(childErr);
        return {};
      }
      output.append(childBytes);
    }
    return output;
  }
  case FieldType::SequenceOf: {
    QVariantList values = actualValue.toList();
    if (values.size() > 0xFFFF) {
      if (err)
        *err = QString("%1 数组元素过多").arg(def.name);
      return {};
    }
    QByteArray output;
    output.append(encodeLength(values.size()));
    if (def.children.isEmpty() && !values.isEmpty()) {
      if (err)
        *err = QString("%1 缺少数组元素定义").arg(def.name);
      return {};
    }
    for (const QVariant &elementValue : values) {
      QString childErr;
      QByteArray childBytes =
          encodeField(def.children.first(), elementValue, &childErr);
      if (!childErr.isEmpty()) {
        if (err)
          *err = QString("%1 数组元素编码失败：%2")
                     .arg(def.name)
                     .arg(childErr);
        return {};
      }
      output.append(childBytes);
    }
    return output;
  }
  case FieldType::Choice: {
    QVariantMap choiceValue = actualValue.toMap();
    int tag = choiceValue.value("choiceValue", -1).toInt();
    const FieldDef *selected = nullptr;
    for (const FieldDef &child : def.children) {
      if (child.choiceValue == tag) {
        selected = &child;
        break;
      }
    }
    if (!selected && !def.children.isEmpty()) {
      selected = &def.children.first();
      tag = selected->choiceValue;
    }
    if (!selected || tag < 0 || tag > 0xFF) {
      if (err)
        *err = QString("%1 未选择有效 choice 分支").arg(def.name);
      return {};
    }
    QByteArray output;
    output.append(static_cast<char>(tag & 0xFF));
    if (selected->type != FieldType::Null && selected->type != FieldType::Unknown) {
      QString childErr;
      output.append(encodeField(*selected, choiceValue.value("value"), &childErr));
      if (!childErr.isEmpty()) {
        if (err)
          *err = QString("%1 choice 分支编码失败：%2")
                     .arg(def.name)
                     .arg(childErr);
        return {};
      }
    }
    return output;
  }
  default:
    return encodePrimitive(def, actualValue, err);
  }
}

QByteArray AppMsgDataEncoder::encodePrimitive(const FieldDef &def,
                                              const QVariant &value,
                                              QString *err) {
  QByteArray output;
  switch (def.type) {
  case FieldType::Null:
  case FieldType::Unknown:
    return output;
  case FieldType::Unsigned:
  case FieldType::Enum:
    output.append(static_cast<char>(value.toUInt() & 0xFF));
    return output;
  case FieldType::Integer:
    output.append(static_cast<char>(static_cast<qint8>(value.toInt())));
    return output;
  case FieldType::Bool:
    output.append(static_cast<char>(value.toBool() ? 1 : 0));
    return output;
  case FieldType::LongUnsigned: {
    quint16 number = static_cast<quint16>(value.toUInt());
    output.resize(2);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::Long: {
    qint16 number = static_cast<qint16>(value.toInt());
    output.resize(2);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::DoubleLongUnsigned: {
    quint32 number = static_cast<quint32>(value.toULongLong());
    output.resize(4);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::DoubleLong: {
    qint32 number = static_cast<qint32>(value.toLongLong());
    output.resize(4);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::Long64Unsigned: {
    quint64 number = value.toULongLong();
    output.resize(8);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::Long64: {
    qint64 number = value.toLongLong();
    output.resize(8);
    qToBigEndian(number, output.data());
    return output;
  }
  case FieldType::Float32: {
    float number = static_cast<float>(value.toDouble());
    quint32 bits = 0;
    std::memcpy(&bits, &number, sizeof(bits));
    output.resize(4);
    qToBigEndian(bits, output.data());
    return output;
  }
  case FieldType::Float64: {
    double number = value.toDouble();
    quint64 bits = 0;
    std::memcpy(&bits, &number, sizeof(bits));
    output.resize(8);
    qToBigEndian(bits, output.data());
    return output;
  }
  case FieldType::BitString: {
    QString text = value.toString().trimmed();
    QByteArray bytes;
    int bitLength = 0;
    QString bitText = text;
    bitText.remove(' ');
    bool isBitText = !bitText.isEmpty();
    for (const QChar &ch : bitText) {
      if (ch != '0' && ch != '1') {
        isBitText = false;
        break;
      }
    }
    if (isBitText) {
      bitLength = bitText.size();
      bytes.resize((bitLength + 7) / 8);
      bytes.fill(0);
      for (int bitIndex = 0; bitIndex < bitLength; ++bitIndex) {
        if (bitText.at(bitIndex) == '1') {
          int byteIndex = bitIndex / 8;
          int bitOffset = 7 - (bitIndex % 8);
          bytes[byteIndex] = static_cast<char>(
              static_cast<quint8>(bytes.at(byteIndex)) | (1 << bitOffset));
        }
      }
    } else if (!parseHexBytes(text, &bytes, err)) {
      return {};
    } else {
      bitLength = bytes.size() * 8;
    }
    output.append(encodeLength(bitLength));
    output.append(bytes);
    return output;
  }
  case FieldType::OctetString: {
    QByteArray bytes;
    if (!parseHexBytes(value.toString(), &bytes, err,
                       def.fixedSize > 0 ? def.fixedSize : -1))
      return {};
    if (def.fixedSize <= 0)
      output.append(encodeLength(bytes.size()));
    output.append(bytes);
    return output;
  }
  case FieldType::VisibleString:
  case FieldType::Utf8String: {
    QByteArray bytes = def.type == FieldType::VisibleString
                           ? value.toString().toLatin1()
                           : value.toString().toUtf8();
    if (def.fixedSize > 0 && bytes.size() != def.fixedSize) {
      if (err)
        *err = QString("%1 需要 %2 字节，实际 %3 字节")
                   .arg(def.name)
                   .arg(def.fixedSize)
                   .arg(bytes.size());
      return {};
    }
    if (def.fixedSize <= 0)
      output.append(encodeLength(bytes.size()));
    output.append(bytes);
    return output;
  }
  case FieldType::DateTime:
    return encodeDateTime(value.toMap(), false, err);
  case FieldType::DateTimeS:
    return encodeDateTime(value.toMap(), true, err);
  case FieldType::Sequence:
  case FieldType::SequenceOf:
  case FieldType::Choice:
    break;
  }
  if (err)
    *err = QString("暂不支持的字段类型：%1").arg(def.name);
  return {};
}

} // namespace AppMsg