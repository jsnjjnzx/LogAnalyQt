#include "Prot698DataEncoder.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <limits>
#include <QHash>
#include <QMetaType>
#include <QtEndian>
#include <cmath>
#include <cstring>

QString Prot698DataEncoder::normalizeTypeName(const QString &typeName) {
  QString normalized = typeName.trimmed().toLower();
  normalized.replace('-', '_');
  return normalized;
}

Prot698DataType Prot698DataEncoder::typeFromName(const QString &typeName) {
  static const QHash<QString, Prot698DataType> typeMap = {
      {"null", Prot698DataType::Null},
      {"array", Prot698DataType::Array},
      {"structure", Prot698DataType::Structure},
      {"bool", Prot698DataType::Bool},
      {"boolean", Prot698DataType::Bool},
      {"bit_string", Prot698DataType::BitString},
      {"double_long", Prot698DataType::DoubleLong},
      {"double_long_unsigned", Prot698DataType::DoubleLongUnsigned},
      {"octet_string", Prot698DataType::OctetString},
      {"visible_string", Prot698DataType::VisibleString},
      {"utf8_string", Prot698DataType::Utf8String},
      {"integer", Prot698DataType::Integer},
      {"long", Prot698DataType::Long},
      {"unsigned", Prot698DataType::Unsigned},
      {"long_unsigned", Prot698DataType::LongUnsigned},
      {"long64", Prot698DataType::Long64},
      {"long64_unsigned", Prot698DataType::Long64Unsigned},
      {"enum", Prot698DataType::Enum},
      {"float32", Prot698DataType::Float32},
      {"float64", Prot698DataType::Float64},
      {"date_time", Prot698DataType::DateTime},
      {"date", Prot698DataType::Date},
      {"time", Prot698DataType::Time},
      {"date_time_s", Prot698DataType::DateTimeS},
      {"oi", Prot698DataType::OI},
      {"oad", Prot698DataType::OAD},
      {"road", Prot698DataType::ROAD},
      {"omd", Prot698DataType::OMD},
      {"ti", Prot698DataType::TI},
      {"tsa", Prot698DataType::TSA},
      {"mac", Prot698DataType::MAC},
      {"rn", Prot698DataType::RN},
      {"region", Prot698DataType::Region},
      {"scaler_unit", Prot698DataType::ScalerUnit},
      {"scalerunit", Prot698DataType::ScalerUnit},
      {"rsd", Prot698DataType::RSD},
      {"csd", Prot698DataType::CSD},
      {"ms", Prot698DataType::MS},
      {"sid", Prot698DataType::SID},
      {"sid_mac", Prot698DataType::SID_MAC},
      {"comdcb", Prot698DataType::COMDCB},
      {"rcsd", Prot698DataType::RCSD},
      {"vqds", Prot698DataType::VQDS},
      {"checksum", Prot698DataType::Checksum},
  };
  return typeMap.value(normalizeTypeName(typeName), Prot698DataType::Unknown);
}

bool Prot698DataEncoder::parseHexBytes(const QString &text, QByteArray *bytes,
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

QByteArray Prot698DataEncoder::encodeLength(int len) {
  QByteArray output;
  if (len < 0 || len > 65535)
    return {};
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

QByteArray Prot698DataEncoder::encodeOAD(const QString &oiHex, int attrId,
                                         int index, QString *err) {
  bool ok = false;
  int oiValue = oiHex.toUInt(&ok, 16);
  if (!QRegularExpression("^[0-9A-Fa-f]{4}$").match(oiHex).hasMatch() || !ok || oiValue < 0 || oiValue > 0xFFFF || attrId < 0 || attrId > 0xFF ||
      index < 0 || index > 0xFF) {
    if (err)
      *err = "OAD 参数不合法";
    return {};
  }
  QByteArray output;
  output.append(static_cast<char>((oiValue >> 8) & 0xFF));
  output.append(static_cast<char>(oiValue & 0xFF));
  output.append(static_cast<char>(attrId & 0xFF));
  output.append(static_cast<char>(index & 0xFF));
  return output;
}

QByteArray Prot698DataEncoder::encodeOMD(const QString &oiHex, int methodId,
                                         int mode, QString *err) {
  bool ok = false;
  int oiValue = oiHex.toUInt(&ok, 16);
  if (!QRegularExpression("^[0-9A-Fa-f]{4}$").match(oiHex).hasMatch() || !ok || oiValue < 0 || oiValue > 0xFFFF || methodId < 0 ||
      methodId > 0xFF || mode < 0 || mode > 0xFF) {
    if (err)
      *err = "OMD 参数不合法";
    return {};
  }
  QByteArray output;
  output.append(static_cast<char>((oiValue >> 8) & 0xFF));
  output.append(static_cast<char>(oiValue & 0xFF));
  output.append(static_cast<char>(methodId & 0xFF));
  output.append(static_cast<char>(mode & 0xFF));
  return output;
}

QByteArray Prot698DataEncoder::encodePIID(int seqNo, bool highPriority) {
  if (seqNo < 0 || seqNo > 63) return {};
  QByteArray output;
  output.append(static_cast<char>((highPriority ? 0x80 : 0x00) |
                                  (seqNo & 0x3F)));
  return output;
}

QByteArray Prot698DataEncoder::encodeDateTimeS(const QVariantMap &value,
                                               QString *err) {
  if (err) err->clear();
  QVariantMap fields;
  for (const auto &key : {"year", "month", "day", "hour", "minute", "second"}) {
    const QString field = QString::fromLatin1(key);
    const QString alias = field == "minute" ? "min" : field == "second" ? "sec" : field;
    if (!value.contains(field) && !value.contains(alias)) {
      if (err) *err = "date_time_s." + field + ": 缺少必填字段";
      return {};
    }
    fields[field] = value.value(field, value.value(alias));
  }
  Prot698SubItem definition; definition.type = "date_time_s";
  QVariant raw;
  if (!validateJsonValue(definition, toExactJson(fields), &raw, err, "$.date_time_s", false)) return {};
  int year = value.value("year").toInt();
  int month = value.value("month").toInt();
  int day = value.value("day").toInt();
  int hour = value.value("hour").toInt();
  int minute = value.value("minute", value.value("min")).toInt();
  int second = value.value("second", value.value("sec")).toInt();
  if (!QDate(year, month, day).isValid() || !QTime(hour, minute, second).isValid()) {
    if (err)
      *err = "date_time_s 字段范围不合法";
    return {};
  }
  QByteArray output;
  output.append(static_cast<char>((year >> 8) & 0xFF));
  output.append(static_cast<char>(year & 0xFF));
  output.append(static_cast<char>(month));
  output.append(static_cast<char>(day));
  output.append(static_cast<char>(hour));
  output.append(static_cast<char>(minute));
  output.append(static_cast<char>(second));
  return output;
}

QByteArray Prot698DataEncoder::encodeTI(int unit, int value, QString *err) {
  if (unit < 0 || unit > 5 || value < 0 || value > 0xFFFF) {
    if (err)
      *err = "TI 参数不合法";
    return {};
  }
  QByteArray output;
  output.append(static_cast<char>(unit & 0xFF));
  output.append(static_cast<char>((value >> 8) & 0xFF));
  output.append(static_cast<char>(value & 0xFF));
  return output;
}

namespace {
QString normalizedType(QString type) {
  return type.trimmed().toLower().replace('-', '_');
}
bool integerType(Prot698DataType type) {
  switch (type) {
  case Prot698DataType::Integer: case Prot698DataType::Unsigned:
  case Prot698DataType::Long: case Prot698DataType::LongUnsigned:
  case Prot698DataType::DoubleLong: case Prot698DataType::DoubleLongUnsigned:
  case Prot698DataType::Long64: case Prot698DataType::Long64Unsigned:
  case Prot698DataType::Enum: return true;
  default: return false;
  }
}
bool opaqueType(Prot698DataType type) {
  switch (type) {
  case Prot698DataType::RSD: case Prot698DataType::CSD:
  case Prot698DataType::MS: case Prot698DataType::RCSD:
  case Prot698DataType::COMDCB: case Prot698DataType::Region:
  case Prot698DataType::SID: case Prot698DataType::SID_MAC:
  case Prot698DataType::VQDS: case Prot698DataType::ROAD: return true;
  default: return false;
  }
}
Prot698SubItem arrayElement(const Prot698SubItem &def) {
  if (def.children.isEmpty()) return {};
  Prot698SubItem element = def.children.first();
  if (normalizedType(element.type) == "option") {
    element.type = "option";
    element.typeOption.clear();
    element.children = def.children;
  }
  return element;
}
QJsonObject objectSchema(const QJsonObject &properties, const QJsonArray &required) {
  return {{"type", "object"}, {"properties", properties},
          {"required", required}, {"additionalProperties", false}};
}
QJsonObject dynamicDataSchema();
QJsonObject inputSchema(const Prot698SubItem &def, int depth = 0) {
  if (depth > 32) return {{"not", QJsonObject{}}};
  const QString name = normalizedType(def.type);
  const auto type = Prot698DataEncoder::typeFromName(name);
  QJsonObject raw;
  if (name == "data") {
    raw = {{"$ref", "#/$defs/typedData"}};
  } else if (name == "option") {
    QJsonArray branches;
    const auto candidates = def.children.isEmpty() ? QList<Prot698SubItem>{def} : def.children;
    for (int i = 0; i < candidates.size(); ++i) {
      auto candidate = candidates.at(i);
      candidate.type = candidate.typeOption;
      branches.append(objectSchema({{"optionIndex", QJsonObject{{"const", i}}},
           {"value", inputSchema(candidate, depth + 1)}}, {"optionIndex", "value"}));
    }
    raw["oneOf"] = branches;
  } else if (type == Prot698DataType::Array) {
    raw = {{"type", "array"}, {"maxItems", 255},
           {"items", inputSchema(arrayElement(def), depth + 1)}};
  } else if (type == Prot698DataType::Structure) {
    QJsonArray items;
    for (const auto &child : def.children) items.append(inputSchema(child, depth + 1));
    raw = {{"type", "array"}, {"prefixItems", items},
           {"minItems", def.children.size()}, {"maxItems", def.children.size()}};
  } else if (integerType(type)) {
    const bool unsignedValue = type == Prot698DataType::Unsigned || type == Prot698DataType::LongUnsigned || type == Prot698DataType::DoubleLongUnsigned || type == Prot698DataType::Long64Unsigned || type == Prot698DataType::Enum;
    const bool wide = type == Prot698DataType::Long64 || type == Prot698DataType::Long64Unsigned;
    const double minimum = unsignedValue ? 0 : type == Prot698DataType::Integer ? -128 : type == Prot698DataType::Long ? -32768 : type == Prot698DataType::DoubleLong ? -2147483648.0 : -9007199254740991.0;
    const double maximum = type == Prot698DataType::Unsigned || type == Prot698DataType::Enum ? 255 : type == Prot698DataType::LongUnsigned ? 65535 : type == Prot698DataType::DoubleLongUnsigned ? 4294967295.0 : type == Prot698DataType::Integer ? 127 : type == Prot698DataType::Long ? 32767 : type == Prot698DataType::DoubleLong ? 2147483647.0 : 9007199254740991.0;
    QJsonObject numeric{{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
    QJsonObject decimal{{"type", "string"}, {"pattern", unsignedValue ? "^(0|[1-9][0-9]*)$" : "^-?(0|[1-9][0-9]*)$"}, {"maxLength", wide ? 20 : 11}};
    decimal["description"] = type == Prot698DataType::Long64 ? "-9223372036854775808..9223372036854775807 exact integer"
        : type == Prot698DataType::Long64Unsigned ? "0..18446744073709551615 exact integer" : "Must fit the integer wire range shown in the numeric alternative";
    raw = type == Prot698DataType::Enum ? numeric : QJsonObject{{"oneOf", QJsonArray{numeric, decimal}}};
    if (type == Prot698DataType::Enum && !def.enumValues.isEmpty()) {
      QJsonArray values;
      for (auto it = def.enumValues.cbegin(); it != def.enumValues.cend(); ++it) values.append(it.key());
      raw = {{"type", "integer"}, {"enum", values}};
    } else if (type != Prot698DataType::Enum && (!def.unit.isEmpty() || def.hasScaler)) {
      const auto valueSchema = raw;
      raw = {{"oneOf", QJsonArray{
          objectSchema({{"representation", QJsonObject{{"const", "raw"}}},
              {"value", valueSchema}, {"unit", QJsonObject{{"const", def.unit}}}},
              {"representation", "value", "unit"}),
          objectSchema({{"representation", QJsonObject{{"const", "physical"}}},
              {"value", QJsonObject{{"type", "string"}, {"pattern", "^-?[0-9]+(\\.[0-9]+)?$"}}},
              {"unit", QJsonObject{{"const", def.unit}}}},
              {"representation", "value", "unit"})}}};
    }
  } else if (type == Prot698DataType::Null) raw = {{"type", "null"}};
  else if (type == Prot698DataType::Bool) raw = {{"type", "boolean"}};
  else if (type == Prot698DataType::Float32 || type == Prot698DataType::Float64) {
    raw = {{"type", "number"}};
    if (!def.unit.isEmpty() || def.hasScaler)
      raw = objectSchema({{"representation", QJsonObject{{"const", "raw"}}},
          {"value", raw}, {"unit", QJsonObject{{"const", def.unit}}}},
          {"representation", "value", "unit"});
  } else if (type == Prot698DataType::Unknown || opaqueType(type))
    raw = {{"not", QJsonObject{}}, {"description", "unsupported_definition: wire format not established"}};
  else if (type == Prot698DataType::DateTime || type == Prot698DataType::DateTimeS ||
           type == Prot698DataType::Date || type == Prot698DataType::Time) {
    QJsonObject props; QJsonArray required;
    if (type != Prot698DataType::Time)
      for (const QString &key : {QString("year"), QString("month"), QString("day")}) {
        props[key] = QJsonObject{{"type", "integer"}, {"minimum", 1},
            {"maximum", key == "year" ? 9999 : key == "month" ? 12 : 31}}; required.append(key);
      }
    if (type != Prot698DataType::Date)
      for (const QString &key : {QString("hour"), QString("minute"), QString("second")}) {
        props[key] = QJsonObject{{"type", "integer"}, {"minimum", 0},
            {"maximum", key == "hour" ? 23 : 59}}; required.append(key);
      }
    if (type == Prot698DataType::Date || type == Prot698DataType::DateTime)
      props["week"] = QJsonObject{{"type", "integer"}, {"enum", QJsonArray{1, 2, 3, 4, 5, 6, 7, 255}}};
    if (type == Prot698DataType::DateTime) props["ms"] = QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", 999}};
    raw = objectSchema(props, required);
  } else if (type == Prot698DataType::TI || type == Prot698DataType::ScalerUnit)
    raw = objectSchema({{"unit", QJsonObject{{"type", "integer"}, {"minimum", 0}, {"maximum", type == Prot698DataType::TI ? 5 : 255}}},
        {type == Prot698DataType::TI ? "value" : "scaler", QJsonObject{{"type", "integer"}, {"minimum", type == Prot698DataType::TI ? 0 : -128}, {"maximum", type == Prot698DataType::TI ? 65535 : 127}}}},
        {"unit", type == Prot698DataType::TI ? "value" : "scaler"});
  else {
    raw = {{"type", "string"}, {"maxLength", 131070}};
    if (type == Prot698DataType::BitString) { raw["pattern"] = "^[01 ]*$"; raw["maxLength"] = 65535; }
    else if (type == Prot698DataType::OI) raw["description"] = "Exactly two hexadecimal bytes";
    else if (type == Prot698DataType::OAD || type == Prot698DataType::OMD) raw["description"] = "Exactly four hexadecimal bytes";
    else if (type == Prot698DataType::TSA) raw["description"] = "2..17 hexadecimal bytes; address descriptor length must match payload";
    if (type != Prot698DataType::VisibleString && type != Prot698DataType::Utf8String && type != Prot698DataType::BitString) {
      raw["pattern"] = "^(0[xX])?([0-9a-fA-F]{2})*$"; raw["maxLength"] = 131072;
      if (type == Prot698DataType::OI) raw["pattern"] = "^(0[xX])?[0-9a-fA-F]{4}$";
      if (type == Prot698DataType::OAD || type == Prot698DataType::OMD) raw["pattern"] = "^(0[xX])?[0-9a-fA-F]{8}$";
    }
  }
  return raw;
}

QJsonObject dynamicDataSchema() {
  static const QJsonObject cached = [] {
  QJsonArray branches;
  const QStringList names{"null", "array", "structure", "bool", "bit_string",
      "double_long", "double_long_unsigned", "octet_string", "visible_string",
      "utf8_string", "integer", "long", "unsigned", "long_unsigned", "long64",
      "long64_unsigned", "enum", "float32", "float64", "date_time", "date", "time",
      "date_time_s", "oi", "oad", "omd", "ti", "tsa", "mac", "rn", "scaler_unit", "checksum"};
  for (const auto &name : names) {
    Prot698SubItem def; def.type = name;
    QJsonObject value = name == "array" || name == "structure"
        ? QJsonObject{{"type", "array"}, {"maxItems", 255},
            {"items", QJsonObject{{"$ref", "#/$defs/typedData"}}}}
        : inputSchema(def);
    branches.append(objectSchema({{"type", QJsonObject{{"const", name}}},
        {"value", value}}, {"type", "value"}));
  }
  return QJsonObject{{"oneOf", branches}};
  }();
  return cached;
}

struct ValueValidator {
  QString *error;
  bool ai;
  int nodes = 0;
  bool fail(const QString &path, const QString &message) {
    if (error) *error = path + ": " + message;
    return false;
  }
  bool object(const QJsonValue &value, const QStringList &keys,
              const QStringList &required, const QString &path) {
    if (!value.isObject()) return fail(path, "必须是对象");
    const auto obj = value.toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it)
      if (!keys.contains(it.key())) return fail(path + "." + it.key(), "未知字段");
    for (const auto &key : required)
      if (!obj.contains(key)) return fail(path + "." + key, "缺少必填字段");
    return true;
  }
  bool smallInteger(const QJsonValue &value, qint64 min, qint64 max,
                    const QString &path, qint64 *out = nullptr) {
    if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
        std::floor(value.toDouble()) != value.toDouble() ||
        value.toDouble() < min || value.toDouble() > max)
      return fail(path, "整数类型或范围不合法");
    if (out) *out = static_cast<qint64>(value.toDouble());
    return true;
  }
  bool decimalRaw(const QJsonValue &value, const QString &path, QString *text) {
    if (value.isString()) {
      *text = value.toString();
      if (text->size() <= 80 && QRegularExpression("^-?(0|[1-9][0-9]*)$").match(*text).hasMatch()) return true;
    } else if (value.isDouble() && std::isfinite(value.toDouble()) &&
               std::floor(value.toDouble()) == value.toDouble() &&
               std::abs(value.toDouble()) <= 9007199254740991.0) {
      *text = QString::number(value.toDouble(), 'f', 0);
      return true;
    }
    return fail(path, "需要精确整数；超出安全整数范围请使用十进制字符串");
  }
  bool physicalRaw(const QString &physical, int scaler, const QString &path, QString *raw) {
    if (physical.size() > 80 || std::abs(scaler) > 40 ||
        !QRegularExpression("^-?[0-9]+(\\.[0-9]+)?$").match(physical).hasMatch())
      return fail(path, "物理值必须为有限十进制字符串");
    QString digits = physical;
    bool negative = digits.startsWith('-');
    if (negative) digits.remove(0, 1);
    int dot = digits.indexOf('.');
    int fractional = dot < 0 ? 0 : digits.size() - dot - 1;
    digits.remove('.');
    int shift = -scaler - fractional;
    if (shift >= 0) digits.append(QString(shift, '0'));
    else {
      const int remove = -shift;
      if (remove > digits.size()) {
        if (digits.contains(QRegularExpression("[1-9]"))) return fail(path, "物理值不能精确换算为整数线值");
        digits = "0";
      } else {
        if (digits.right(remove).contains(QRegularExpression("[1-9]"))) return fail(path, "物理值不能精确换算为整数线值");
        digits.chop(remove);
      }
    }
    while (digits.size() > 1 && digits.startsWith('0')) digits.remove(0, 1);
    if (digits.isEmpty()) digits = "0";
    *raw = negative && digits != "0" ? "-" + digits : digits;
    return true;
  }
  bool validate(const Prot698SubItem &def, const QJsonValue &value,
                QVariant *raw, const QString &path, int depth = 0) {
    if (++nodes > 4096 || depth > 32) return fail(path, "递归深度或节点数量超过限制");
    const QString name = normalizedType(def.type);
    const auto type = Prot698DataEncoder::typeFromName(name);
    if (name == "data") {
      if (!object(value, {"type", "value"}, {"type", "value"}, path)) return false;
      auto obj = value.toObject();
      if (!obj["type"].isString()) return fail(path + ".type", "必须是明确的类型名");
      const QString dynamicType = obj["type"].toString();
      auto dynamicEnum = Prot698DataEncoder::typeFromName(dynamicType);
      bool allowedDynamic = !ai;
      if (ai) for (const auto &branch : dynamicDataSchema()["oneOf"].toArray())
        if (branch.toObject()["properties"].toObject()["type"].toObject()["const"].toString() == dynamicType) allowedDynamic = true;
      if (dynamicType != normalizedType(dynamicType) || dynamicEnum == Prot698DataType::Unknown || !allowedDynamic)
        return fail(path + ".type", "不支持的动态 Data 类型");
      QVariant dynamicRaw;
      if (dynamicEnum == Prot698DataType::Array || dynamicEnum == Prot698DataType::Structure) {
        if (!obj["value"].isArray()) return fail(path + ".value", "必须是 typed Data 数组");
        const auto values = obj["value"].toArray();
        if (values.size() > 255) return fail(path + ".value", "容器最多255项");
        QVariantList items;
        Prot698SubItem child; child.type = "Data";
        for (int i = 0; i < values.size(); ++i) {
          QVariant item;
          if (!validate(child, values.at(i), &item, path + ".value[" + QString::number(i) + "]", depth + 1)) return false;
          items.append(item);
        }
        dynamicRaw = items;
      } else {
        Prot698SubItem dynamicDef; dynamicDef.type = dynamicType;
        if (!validate(dynamicDef, obj["value"], &dynamicRaw, path + ".value", depth + 1)) return false;
      }
      *raw = QVariantMap{{"type", dynamicType}, {"value", dynamicRaw}};
      return true;
    }
    if (name == "option") {
      if (!object(value, {"optionIndex", "value"}, {"optionIndex", "value"}, path)) return false;
      const auto candidates = def.children.isEmpty() ? QList<Prot698SubItem>{def} : def.children;
      qint64 index;
      if (!smallInteger(value.toObject()["optionIndex"], 0, candidates.size() - 1, path + ".optionIndex", &index)) return false;
      auto selected = candidates.at(index); selected.type = selected.typeOption;
      if (selected.type.isEmpty()) return fail(path, "option 缺少 TypeOption 定义");
      QVariant selectedRaw;
      if (!validate(selected, value.toObject()["value"], &selectedRaw, path + ".value", depth + 1)) return false;
      *raw = QVariantMap{{"optionIndex", index}, {"value", selectedRaw}};
      return true;
    }
    if (type == Prot698DataType::Unknown) return fail(path, "unsupported_definition: 未知或空数据类型 " + def.type);
    if (ai && opaqueType(type)) return fail(path, "unsupported_definition: 尚未结构化的原始Hex分支");
    if (type == Prot698DataType::Float32 || type == Prot698DataType::Float64) {
      QJsonValue numeric = value;
      if (ai && (!def.unit.isEmpty() || def.hasScaler)) {
        if (!object(value, {"representation", "value", "unit"}, {"representation", "value", "unit"}, path)) return false;
        const auto obj = value.toObject();
        if (obj["representation"] != "raw" || obj["unit"] != def.unit)
          return fail(path, "浮点仅允许明确raw线值及匹配单位，禁止推测物理换算");
        numeric = obj["value"];
      }
      if (!numeric.isDouble() || !std::isfinite(numeric.toDouble()) ||
          (type == Prot698DataType::Float32 && !std::isfinite(static_cast<float>(numeric.toDouble()))))
        return fail(path, "浮点必须是范围内的有限数值");
      *raw = numeric.toDouble(); return true;
    }
    if (type == Prot698DataType::Array || type == Prot698DataType::Structure) {
      if (!value.isArray()) return fail(path, "必须是数组");
      const auto values = value.toArray();
      if (values.size() > 255) return fail(path, "容器最多255项");
      if (type == Prot698DataType::Structure && values.size() != def.children.size()) return fail(path, "structure字段数必须与定义精确一致");
      if (type == Prot698DataType::Array && def.children.isEmpty() && !values.isEmpty()) return fail(path, "array缺少元素定义");
      QVariantList items;
      for (int i = 0; i < values.size(); ++i) {
        QVariant item;
        auto child = type == Prot698DataType::Array ? arrayElement(def) : def.children.at(i);
        if (!validate(child, values.at(i), &item, path + "[" + QString::number(i) + "]", depth + 1)) return false;
        items.append(item);
      }
      *raw = items;
      return true;
    }
    if (integerType(type)) {
      QJsonValue numeric = value;
      QString number;
      if (ai && type != Prot698DataType::Enum && (!def.unit.isEmpty() || def.hasScaler)) {
        if (!object(value, {"representation", "value", "unit"}, {"representation", "value", "unit"}, path)) return false;
        const auto obj = value.toObject();
        if (!obj["unit"].isString() || obj["unit"].toString() != def.unit) return fail(path + ".unit", "单位与当前字典不一致");
        if (obj["representation"] == "physical") {
          if (!obj["value"].isString()) return fail(path + ".value", "物理值必须是十进制字符串");
          if (!physicalRaw(obj["value"].toString(), def.scaler, path + ".value", &number)) return false;
        } else if (obj["representation"] == "raw") numeric = obj["value"];
        else return fail(path + ".representation", "只能是raw或physical");
      }
      if (number.isEmpty() && !decimalRaw(numeric, path, &number)) return false;
      if (type == Prot698DataType::Enum && !numeric.isDouble()) return fail(path, "枚举需要整数值");
      bool ok = false;
      const bool isUnsigned = type == Prot698DataType::Unsigned || type == Prot698DataType::LongUnsigned || type == Prot698DataType::DoubleLongUnsigned || type == Prot698DataType::Long64Unsigned || type == Prot698DataType::Enum;
      if (isUnsigned) {
        quint64 n = number.toULongLong(&ok);
        quint64 max = type == Prot698DataType::Long64Unsigned ? std::numeric_limits<quint64>::max() : type == Prot698DataType::DoubleLongUnsigned ? 0xffffffffULL : type == Prot698DataType::LongUnsigned ? 65535 : 255;
        if (!ok || number.startsWith('-') || n > max) return fail(path, "无符号整数超出范围");
        if (type == Prot698DataType::Enum && !def.enumValues.isEmpty() && !def.enumValues.contains(static_cast<int>(n))) return fail(path, "不在当前枚举定义中");
        *raw = QVariant::fromValue(n);
      } else {
        qint64 n = number.toLongLong(&ok);
        qint64 min = type == Prot698DataType::Long64 ? std::numeric_limits<qint64>::min() : type == Prot698DataType::DoubleLong ? -2147483648LL : type == Prot698DataType::Long ? -32768 : -128;
        qint64 max = type == Prot698DataType::Long64 ? std::numeric_limits<qint64>::max() : type == Prot698DataType::DoubleLong ? 2147483647LL : type == Prot698DataType::Long ? 32767 : 127;
        if (!ok || n < min || n > max) return fail(path, "有符号整数超出范围");
        *raw = QVariant::fromValue(n);
      }
      return true;
    }
    if (type == Prot698DataType::Null) {
      if (!value.isNull()) return fail(path, "Null必须显式为null");
      *raw = QVariant(); return true;
    }
    if (type == Prot698DataType::Bool) {
      if (!value.isBool()) return fail(path, "必须是bool");
      *raw = value.toBool(); return true;
    }
    if (type == Prot698DataType::DateTime || type == Prot698DataType::DateTimeS || type == Prot698DataType::Date || type == Prot698DataType::Time) {
      QStringList required, allowed;
      if (type != Prot698DataType::Time) required << "year" << "month" << "day";
      if (type != Prot698DataType::Date) required << "hour" << "minute" << "second";
      allowed = required;
      if (!ai) allowed = {"year", "month", "day", "week", "hour", "minute", "min", "second", "sec", "ms"};
      if (type == Prot698DataType::DateTime || type == Prot698DataType::Date) allowed << "week";
      if (type == Prot698DataType::DateTime) allowed << "ms";
      auto obj = value.toObject();
      if (!ai) {
        if (!obj.contains("minute") && obj.contains("min")) obj["minute"] = obj["min"];
        if (!obj.contains("second") && obj.contains("sec")) obj["second"] = obj["sec"];
      }
      if (!object(obj, allowed, required, path)) return false;
      for (const auto &key : required) if (!smallInteger(obj[key], 0, key == "year" ? 9999 : 255, path + "." + key)) return false;
      if (type != Prot698DataType::Time && !QDate(obj["year"].toInt(), obj["month"].toInt(), obj["day"].toInt()).isValid()) return fail(path, "日期不存在");
      if (type != Prot698DataType::Date && !QTime(obj["hour"].toInt(), obj["minute"].toInt(), obj["second"].toInt()).isValid()) return fail(path, "时间不存在");
      if (obj.contains("week") && (!smallInteger(obj["week"], 1, 255, path + ".week") || (obj["week"].toInt() > 7 && obj["week"].toInt() != 255))) return fail(path + ".week", "星期只能为1..7或255");
      if (obj.contains("ms") && !smallInteger(obj["ms"], 0, 999, path + ".ms")) return false;
      *raw = obj.toVariantMap(); return true;
    }
    if (type == Prot698DataType::TI || type == Prot698DataType::ScalerUnit) {
      const QString field = type == Prot698DataType::TI ? "value" : "scaler";
      if (!object(value, {"unit", field}, {"unit", field}, path)) return false;
      auto obj = value.toObject();
      if (!smallInteger(obj["unit"], 0, type == Prot698DataType::TI ? 5 : 255, path + ".unit") || !smallInteger(obj[field], type == Prot698DataType::TI ? 0 : -128, type == Prot698DataType::TI ? 65535 : 127, path + "." + field)) return false;
      *raw = obj.toVariantMap(); return true;
    }
    if (!value.isString()) return fail(path, "必须是字符串");
    const QString text = value.toString();
    if (text.size() > 131072) return fail(path, "字符串长度超过限制");
    if ((type == Prot698DataType::VisibleString || type == Prot698DataType::Utf8String) && text.toUtf8().size() > 65535) return fail(path, "字符串字节长度超过65535");
    if (type == Prot698DataType::BitString) {
      if (!QRegularExpression("^[01 ]*$").match(text).hasMatch() || text.count('0') + text.count('1') > 65535) return fail(path, "位串只能包含0和1");
    } else if (type == Prot698DataType::VisibleString) {
      for (auto ch : text) if (ch.unicode() > 0x7f) return fail(path, "visible_string只接受ASCII字符");
    } else if (type != Prot698DataType::Utf8String) {
      QString hex = text;
      if (ai) {
        if (hex.startsWith("0x", Qt::CaseInsensitive)) hex.remove(0, 2);
        if (!QRegularExpression("^[0-9A-Fa-f]*$").match(hex).hasMatch()) return fail(path, "AI Hex必须是连续十六进制，可带单个开头0x前缀");
      } else hex.remove(QRegularExpression("0[xX]|[ \\t\\r\\n:]"));
      if (hex.size() > 131070) return fail(path, "Hex内容超过65535字节");
      if (hex.size() % 2 || !QRegularExpression("^[0-9A-Fa-f]*$").match(hex).hasMatch()) return fail(path, "十六进制格式不合法");
      int expected = type == Prot698DataType::OI ? 2 : type == Prot698DataType::OAD || type == Prot698DataType::OMD ? 4 : -1;
      if (expected >= 0 && hex.size() != expected * 2) return fail(path, "标识符字节长度不合法");
      if (type == Prot698DataType::TSA) {
        if (hex.size() < 4 || hex.size() > 34) return fail(path, "TSA长度不合法");
        const auto address = QByteArray::fromHex(hex.toLatin1());
        const int indicator = static_cast<quint8>(address.front());
        if (address.size() != (indicator & 0xf) + 2) return fail(path, "TSA地址描述长度与实际字节数不一致");
        if (ai && indicator & 0x20) return fail(path, "unsupported_definition: TSA扩展逻辑地址尚未确认");
      }
      if (type == Prot698DataType::ROAD) return fail(path, "unsupported_definition: ROAD需要关联OAD列表，不能用固定4字节替代");
    }
    *raw = text; return true;
  }
};
QJsonValue exactJson(const QVariant &value) {
  if (value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::ULongLong)
    return std::abs(value.toDouble()) <= 9007199254740991.0
               ? QJsonValue(value.toDouble()) : QJsonValue(value.toString());
  if (value.metaType().id() == QMetaType::QVariantList) {
    QJsonArray result; for (const auto &item : value.toList()) result.append(exactJson(item)); return result;
  }
  if (value.metaType().id() == QMetaType::QVariantMap) {
    QJsonObject result; const auto map = value.toMap(); for (auto it = map.cbegin(); it != map.cend(); ++it) result[it.key()] = exactJson(it.value()); return result;
  }
  if (value.metaType().id() == QMetaType::QByteArray) return QString::fromLatin1(value.toByteArray().toHex());
  return QJsonValue::fromVariant(value);
}
}

QJsonValue Prot698DataEncoder::toExactJson(const QVariant &value) { return exactJson(value); }

bool Prot698DataEncoder::validateJsonValue(const Prot698SubItem &def,
    const QJsonValue &value, QVariant *rawValue, QString *err,
    const QString &path, bool aiInput) {
  if (err) err->clear();
  QVariant ignored;
  ValueValidator validator{err, aiInput};
  return validator.validate(def, value, rawValue ? rawValue : &ignored, path);
}

QVariantMap Prot698DataEncoder::subItemToSchema(const Prot698SubItem &item) {
  QVariantMap schema{{"name", item.name}, {"type", item.type}, {"unit", item.unit},
      {"scaler", item.scaler}, {"hasScaler", item.hasScaler}, {"size", item.size},
      {"referenceSize", item.size}, {"sizeConstraint", "reference_only; lengths follow Data type and service structure"},
      {"typeOption", item.typeOption}, {"selected", item.selected},
      {"remark", item.remark}, {"format", item.format}, {"defVal", item.defVal},
      {"inputSchema", inputSchema(item).toVariantMap()}};
  auto jsonSchema = inputSchema(item);
  jsonSchema["$defs"] = QJsonObject{{"typedData", dynamicDataSchema()}};
  schema["inputSchema"] = jsonSchema.toVariantMap();
  QVariantList enumItems;
  for (auto it = item.enumValues.cbegin(); it != item.enumValues.cend(); ++it)
    enumItems.append(QVariantMap{{"value", it.key()}, {"text", it.value()}});
  schema["enumValues"] = enumItems;
  QVariantList children;
  for (int i = 0; i < item.children.size(); ++i) {
    auto child = subItemToSchema(item.children.at(i)); child["optionIndex"] = i; children.append(child);
  }
  schema["children"] = children;
  const auto type = typeFromName(item.type);
  const bool supported = type != Prot698DataType::Unknown && !opaqueType(type);
  bool buildSupported = supported || normalizedType(item.type) == "data" || normalizedType(item.type) == "option";
  if (normalizedType(item.type) == "option" && item.children.isEmpty()) {
    const auto selectedType = typeFromName(item.typeOption);
    buildSupported = selectedType != Prot698DataType::Unknown && !opaqueType(selectedType);
  }
  QVariantList reasons;
  if (!buildSupported) reasons.append("unsupported_definition: " + item.type);
  for (const auto &child : children) {
    if (!child.toMap().value("buildSupported").toBool()) {
      buildSupported = false; reasons.append(child.toMap().value("unsupportedReasons"));
    }
  }
  schema["buildSupported"] = buildSupported;
  schema["unsupportedReasons"] = reasons;
  schema["verificationStatus"] = "pending";
  return schema;
}

QByteArray Prot698DataEncoder::encodeValue(Prot698DataType type,
                                           const Prot698SubItem &def,
                                           const QVariant &value,
                                           QString *err) {
  QByteArray output;
  switch (type) {
  case Prot698DataType::Null:
    break;
  case Prot698DataType::Bool:
    output.append(static_cast<char>(value.toBool() ? 1 : 0));
    break;
  case Prot698DataType::Integer:
    output.append(static_cast<char>(static_cast<qint8>(value.toInt())));
    break;
  case Prot698DataType::Unsigned:
  case Prot698DataType::Enum:
    output.append(static_cast<char>(value.toUInt() & 0xFF));
    break;
  case Prot698DataType::Long: {
    qint16 number = static_cast<qint16>(value.toInt());
    output.append(static_cast<char>((number >> 8) & 0xFF));
    output.append(static_cast<char>(number & 0xFF));
    break;
  }
  case Prot698DataType::LongUnsigned: {
    quint16 number = static_cast<quint16>(value.toUInt());
    output.append(static_cast<char>((number >> 8) & 0xFF));
    output.append(static_cast<char>(number & 0xFF));
    break;
  }
  case Prot698DataType::DoubleLong: {
    qint32 number = static_cast<qint32>(value.toLongLong());
    output.resize(4);
    qToBigEndian(number, output.data());
    break;
  }
  case Prot698DataType::DoubleLongUnsigned: {
    quint32 number = static_cast<quint32>(value.toULongLong());
    output.resize(4);
    qToBigEndian(number, output.data());
    break;
  }
  case Prot698DataType::Long64: {
    qint64 number = value.toLongLong();
    output.resize(8);
    qToBigEndian(number, output.data());
    break;
  }
  case Prot698DataType::Long64Unsigned: {
    quint64 number = value.toULongLong();
    output.resize(8);
    qToBigEndian(number, output.data());
    break;
  }
  case Prot698DataType::Float32: {
    float number = static_cast<float>(value.toDouble());
    quint32 bits; std::memcpy(&bits, &number, sizeof(bits));
    output.resize(4);
    qToBigEndian(bits, output.data());
    break;
  }
  case Prot698DataType::Float64: {
    double number = value.toDouble();
    quint64 bits; std::memcpy(&bits, &number, sizeof(bits));
    output.resize(8);
    qToBigEndian(bits, output.data());
    break;
  }
  case Prot698DataType::BitString: {
    QString bits = value.toString().remove(' ');
    int bitLen = bits.size();
    QByteArray bytes((bitLen + 7) / 8, '\0');
    for (int bitIndex = 0; bitIndex < bitLen; ++bitIndex) {
      if (bits.at(bitIndex) == '1') {
        int byteIndex = bitIndex / 8;
        int offsetInByte = 7 - (bitIndex % 8);
        bytes[byteIndex] = static_cast<char>(
            static_cast<quint8>(bytes[byteIndex]) | (1 << offsetInByte));
      }
    }
    output.append(encodeLength(bitLen));
    output.append(bytes);
    break;
  }
  case Prot698DataType::OctetString:
  case Prot698DataType::MAC:
  case Prot698DataType::RN:
  case Prot698DataType::Checksum: {
    QByteArray bytes;
    if (value.metaType().id() == QMetaType::QByteArray) {
      bytes = value.toByteArray();
    } else if (!parseHexBytes(value.toString(), &bytes, err)) {
      return {};
    }
    output.append(encodeLength(bytes.size()));
    output.append(bytes);
    break;
  }
  case Prot698DataType::VisibleString: {
    QByteArray bytes = value.toString().toLatin1();
    output.append(encodeLength(bytes.size()));
    output.append(bytes);
    break;
  }
  case Prot698DataType::Utf8String: {
    QByteArray bytes = value.toString().toUtf8();
    output.append(encodeLength(bytes.size()));
    output.append(bytes);
    break;
  }
  case Prot698DataType::DateTime: {
    QVariantMap map = value.toMap();
    QByteArray dateTimeS = encodeDateTimeS(map, err);
    if (dateTimeS.isEmpty())
      return {};
    int week = map.value("week", 0xFF).toInt();
    int ms = map.value("ms", 0).toInt();
    output.append(dateTimeS.left(4));
    output.append(static_cast<char>(week & 0xFF));
    output.append(dateTimeS.mid(4));
    output.append(static_cast<char>((ms >> 8) & 0xFF));
    output.append(static_cast<char>(ms & 0xFF));
    break;
  }
  case Prot698DataType::DateTimeS:
    output.append(encodeDateTimeS(value.toMap(), err));
    if (output.isEmpty())
      return {};
    break;
  case Prot698DataType::Date: {
    QVariantMap map = value.toMap();
    int year = map.value("year").toInt();
    output.append(static_cast<char>((year >> 8) & 0xFF));
    output.append(static_cast<char>(year & 0xFF));
    output.append(static_cast<char>(map.value("month").toInt() & 0xFF));
    output.append(static_cast<char>(map.value("day").toInt() & 0xFF));
    output.append(static_cast<char>(map.value("week", 0xFF).toInt() & 0xFF));
    break;
  }
  case Prot698DataType::Time: {
    QVariantMap map = value.toMap();
    output.append(static_cast<char>(map.value("hour").toInt() & 0xFF));
    output.append(static_cast<char>(map.value("minute", map.value("min"))
                                       .toInt() &
                                   0xFF));
    output.append(static_cast<char>(map.value("second", map.value("sec"))
                                       .toInt() &
                                   0xFF));
    break;
  }
  case Prot698DataType::OI: {
    QByteArray bytes;
    if (!parseHexBytes(value.toString(), &bytes, err, 2))
      return {};
    output.append(bytes);
    break;
  }
  case Prot698DataType::OAD:
  case Prot698DataType::ROAD:
  case Prot698DataType::OMD: {
    QByteArray bytes;
    if (!parseHexBytes(value.toString(), &bytes, err, 4))
      return {};
    output.append(bytes);
    break;
  }
  case Prot698DataType::TI: {
    QVariantMap map = value.toMap();
    output.append(encodeTI(map.value("unit").toInt(),
                           map.value("value").toInt(), err));
    if (output.isEmpty())
      return {};
    break;
  }
  case Prot698DataType::ScalerUnit: {
    QVariantMap map = value.toMap();
    output.append(static_cast<char>(map.value("scaler").toInt() & 0xFF));
    output.append(static_cast<char>(map.value("unit").toInt() & 0xFF));
    break;
  }
  case Prot698DataType::TSA: {
    QByteArray bytes;
    if (!parseHexBytes(value.toString(), &bytes, err))
      return {};
    output.append(encodeLength(bytes.size()));
    output.append(bytes);
    break;
  }
  case Prot698DataType::RSD:
  case Prot698DataType::CSD:
  case Prot698DataType::MS:
  case Prot698DataType::RCSD:
  case Prot698DataType::COMDCB:
  case Prot698DataType::Region:
  case Prot698DataType::SID:
  case Prot698DataType::SID_MAC:
  case Prot698DataType::VQDS: {
    QByteArray bytes;
    if (!parseHexBytes(value.toString(), &bytes, err))
      return {};
    output.append(bytes);
    break;
  }
  case Prot698DataType::Unknown:
    if (err) *err = "不支持的数据类型";
    return {};
  case Prot698DataType::Array:
  case Prot698DataType::Structure:
    if (err)
      *err = "复合类型应由 encode() 处理";
    return {};
  }
  Q_UNUSED(def)
  return output;
}

QByteArray Prot698DataEncoder::encode(const Prot698SubItem &def,
    const QVariant &value, QString *err, const Prot698EncodeOptions &options) {
  QVariant raw;
  if (!validateJsonValue(def, exactJson(value), &raw, err, "$", false)) return {};
  const QString name = normalizedType(def.type);
  if (name == "option") {
    const auto map = raw.toMap();
    const auto candidates = def.children.isEmpty() ? QList<Prot698SubItem>{def} : def.children;
    auto selected = candidates.at(map.value("optionIndex").toInt()); selected.type = selected.typeOption;
    return encode(selected, map.value("value"), err, options);
  }
  if (name == "data") {
    const auto map = raw.toMap();
    Prot698SubItem selected; selected.type = map.value("type").toString();
    const auto type = typeFromName(selected.type);
    if (type == Prot698DataType::Array || type == Prot698DataType::Structure) {
      QByteArray result;
      if (options.withTag) result.append(static_cast<char>(type));
      const auto values = map.value("value").toList(); result.append(static_cast<char>(values.size()));
      Prot698SubItem dynamic; dynamic.type = "Data";
      for (const auto &item : values) {
        const auto bytes = encode(dynamic, item, err, options);
        if (err && !err->isEmpty()) return {};
        result.append(bytes);
      }
      return result;
    }
    return encode(selected, map.value("value"), err, options);
  }
  const auto type = typeFromName(def.type);
  QByteArray output;
  if (options.withTag) output.append(static_cast<char>(type));
  if (type == Prot698DataType::Array || type == Prot698DataType::Structure) {
    const auto values = raw.toList(); output.append(static_cast<char>(values.size()));
    for (int i = 0; i < values.size(); ++i) {
      const auto child = type == Prot698DataType::Array ? arrayElement(def) : def.children.at(i);
      const auto bytes = encode(child, values.at(i), err, options);
      if (err && !err->isEmpty()) return {};
      output.append(bytes);
    }
    return output;
  }
  auto bytes = encodeValue(type, def, raw, err);
  if (err && !err->isEmpty()) return {};
  output.append(bytes);
  return output;
}
