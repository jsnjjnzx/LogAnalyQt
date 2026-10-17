#include "CsgUpstreamServiceParser.h"

#include "CsgUpstreamConfigManager.h"
#include "CsgUpstreamFieldParser.h"
#include "CsgUpstreamValueDecoder.h"

#include <QStringList>

namespace {

constexpr int kDataUnitHeaderLength = 6;
constexpr int kPasswordLength = 16;
constexpr int kTimeTagLength = 5;

quint8 ByteAt(const QByteArray &data, int offset) {
  return static_cast<quint8>(data.at(offset));
}

QString HexBytes(const QByteArray &data) {
  return QString::fromLatin1(data.toHex()).toUpper();
}

QString HexCode(quint32 value) {
  return QString("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

QString DecodeErrorCode(quint8 value) {
  static const QStringList descriptions = {
      "正确",           "中继命令没有返回",
      "设置内容非法",   "密码权限不足",
      "无此数据项",     "命令时间失效",
      "目标地址不存在", "校验失败",
  };
  if (value < descriptions.size()) {
    return descriptions.at(value);
  }
  return QString("未知错误码 %1").arg(value);
}

quint32 ReadLittleEndian32(const QByteArray &data, int offset) {
  quint32 value = 0;
  for (int index = 0; index < 4; ++index) {
    value |= static_cast<quint32>(ByteAt(data, offset + index))
             << (index * 8);
  }
  return value;
}

quint16 ReadLittleEndian16(const QByteArray &data, int offset) {
  return static_cast<quint16>(ByteAt(data, offset)) |
         (static_cast<quint16>(ByteAt(data, offset + 1)) << 8);
}

quint64 ReadUnsigned(const QByteArray &bytes, bool littleEndian) {
  quint64 value = 0;
  if (littleEndian) {
    for (int index = bytes.size() - 1; index >= 0; --index) {
      value = (value << 8) | ByteAt(bytes, index);
    }
  } else {
    for (int index = 0; index < bytes.size(); ++index) {
      value = (value << 8) | ByteAt(bytes, index);
    }
  }
  return value;
}

QString DecodeDa(quint8 da1, quint8 da2) {
  if (da1 == 0 && da2 == 0) {
    return "p0";
  }

  QStringList points;
  for (int bit = 0; bit < 8; ++bit) {
    if ((da1 & (1U << bit)) != 0) {
      const int point = da2 == 0 ? bit + 1 : (da2 - 1) * 8 + bit + 1;
      points.append("p" + QString::number(point));
    }
  }
  if (points.isEmpty()) {
    return QString("第 %1 组，无有效信息点").arg(da2);
  }
  return QString("第 %1 组 %2").arg(da2).arg(points.join(", "));
}

struct DiLookup {
  QString key;
  QString nameSuffix;
};

DiLookup ResolveDiLookup(quint32 di) {
  const quint8 itemNumber = static_cast<quint8>(di & 0xFF);
  if ((di & 0xFFFFFF00U) == 0xE0000300U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00003XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0000400U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00004XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0001500U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00015XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0001600U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00016XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0000200U) {
    if (itemNumber <= 0x1E) {
      return {"E0000200",
              QString("（第%1路485）").arg(itemNumber + 1)};
    }
    if (itemNumber == 0x1F) {
      return {"E0000200", "（载波通道）"};
    }
    if (itemNumber == 0x20) {
      return {"E0000200", "（无线通道）"};
    }
    if (itemNumber == 0x22) {
      return {"E0000200", "（光纤通道）"};
    }
    if (itemNumber == 0x23) {
      return {"E0000200", "（混合通道）"};
    }
    if (itemNumber >= 0x24 && itemNumber <= 0x29) {
      return {"E0000200",
              QString("（备用通道%1）").arg(itemNumber - 0x23)};
    }
    if (itemNumber >= 0x30 && itemNumber <= 0x50) {
      return {"E0000230", QString::number(itemNumber - 0x2F)};
    }
  }
  const quint32 series = di & 0xFFFFFF00U;
  if ((series == 0xE0000500U || series == 0xE0000600U ||
       series == 0xE0000700U || series == 0xE0000800U) &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {HexCode(series).left(6) + "XX",
            QString::number(itemNumber)};
  }
  if (series == 0xE0000A00U && itemNumber <= 0x1E) {
    return {"E0000AXX",
            QString("（端口号 %1H）")
                .arg(itemNumber, 2, 16, QLatin1Char('0'))
                .toUpper()};
  }
  if (series == 0xE0000C00U) {
    const quint8 rangeStart = itemNumber <= 0x0E
                                  ? 0x00
                                  : itemNumber >= 0x10 && itemNumber <= 0x1E
                                        ? 0x10
                                        : itemNumber >= 0x20 && itemNumber <= 0x2E
                                              ? 0x20
                                              : itemNumber >= 0x30 && itemNumber <= 0x3E
                                                    ? 0x30
                                                    : 0xFF;
    if (rangeStart != 0xFF) {
      return {HexCode(series | rangeStart),
              QString("（开关编号 %1H）")
                  .arg(itemNumber - rangeStart, 2, 16,
                       QLatin1Char('0'))
                  .toUpper()};
    }
  }
  if (series == 0xE0000D00U) {
    const quint8 rangeStart = itemNumber >= 0x01 && itemNumber <= 0x08
                                  ? 0x01
                                  : itemNumber >= 0x11 && itemNumber <= 0x18
                                        ? 0x11
                                        : itemNumber >= 0x21 && itemNumber <= 0x28
                                              ? 0x21
                                              : itemNumber >= 0x31 && itemNumber <= 0x38
                                                    ? 0x31
                                                    : 0xFF;
    if (rangeStart != 0xFF) {
      return {HexCode(series | rangeStart),
              QString("（时段 %1）").arg(itemNumber - rangeStart + 1)};
    }
  }
  if (series == 0xE0000E00U &&
      itemNumber >= 0x21 && itemNumber <= 0x2C) {
    return {"E0000E21",
            QString("（轮次 %1）").arg(itemNumber - 0x20)};
  }
  return {HexCode(di), QString()};
}

QString ResolveDiName(const CsgUpstreamConfigManager &manager,
                      const DiLookup &lookup) {
  QString name = manager.getDiName(lookup.key);
  if (!name.isEmpty()) {
    name += lookup.nameSuffix;
  }
  return name;
}

bool AppendDataUnitHeader(const QByteArray &body, int *cursor,
                          int baseOffset,
                          QList<CsgUpstreamField> *fields,
                          quint32 *di, QString *diKey,
                          QString *errorMessage) {
  const int remaining = body.size() - *cursor;
  if (remaining < kDataUnitHeaderLength) {
    *errorMessage =
        QString("数据单元头不足，需要 6 字节，剩余 %1 字节")
            .arg(remaining);
    return false;
  }

  const int daOffset = *cursor;
  const quint8 da1 = ByteAt(body, daOffset);
  const quint8 da2 = ByteAt(body, daOffset + 1);
  fields->append({"信息点标识 DA", HexBytes(body.mid(daOffset, 2)),
                  DecodeDa(da1, da2), baseOffset + daOffset, 2, 0});
  *cursor += 2;

  const int diOffset = *cursor;
  *di = ReadLittleEndian32(body, diOffset);
  const DiLookup lookup = ResolveDiLookup(*di);
  *diKey = lookup.key;
  const auto &manager = CsgUpstreamConfigManager::instance();
  const QString name = ResolveDiName(manager, lookup);
  QString description = HexCode(*di);
  if (!name.isEmpty()) {
    description += " " + name;
  } else if (manager.isLoaded()) {
    description += " 数据项暂未配置";
  } else {
    description += " 协议配置未加载";
  }
  fields->append({"数据标识 DI", HexBytes(body.mid(diOffset, 4)),
                  description, baseOffset + diOffset, 4, 0});
  *cursor += 4;
  return true;
}

bool ParseLegacyService(const QByteArray &body,
                        const CsgUpstreamServiceDef &service,
                        int *cursor, int baseOffset,
                        QList<CsgUpstreamField> *fields,
                        QString *errorMessage) {
  quint32 di = 0;
  QString diKey;
  if (!AppendDataUnitHeader(body, cursor, baseOffset, fields,
                            &di, &diKey, errorMessage)) {
    return false;
  }

  if (service.layout == CsgUpstreamServiceLayout::Struct) {
    return CsgUpstreamFieldParser::parse(
        body, service.fields, cursor, baseOffset, QString(), 1,
        fields, errorMessage);
  }

  const CsgUpstreamFieldDef &selector = service.selector;
  if (body.size() - *cursor < selector.length) {
    *errorMessage =
        QString("字段 %1 数据不足，需要 %2 字节，剩余 %3 字节")
            .arg(selector.name)
            .arg(selector.length)
            .arg(body.size() - *cursor);
    return false;
  }
  const QByteArray selectorBytes = body.mid(*cursor, selector.length);
  const quint64 selectorValue =
      ReadUnsigned(selectorBytes, selector.littleEndian);
  const auto caseIt = service.cases.constFind(selectorValue);
  const QString selectorDescription =
      caseIt == service.cases.cend()
          ? QString("未知%1 %2").arg(selector.name).arg(selectorValue)
          : caseIt.value().name;
  fields->append({selector.name, HexBytes(selectorBytes),
                  selectorDescription, baseOffset + *cursor,
                  selector.length, 1});
  *cursor += selector.length;
  if (caseIt == service.cases.cend()) {
    const int rawLength = static_cast<int>(body.size()) - *cursor;
    QString description = "Choice 未配置当前选择值";
    if (service.passwordPolicy == CsgUpstreamTailPolicy::Optional &&
        rawLength >= kPasswordLength) {
      description += "；变长内容与可选 PW 无法唯一分界";
    }
    fields->append({"未解析应用数据", HexBytes(body.mid(*cursor)),
                    description, baseOffset + *cursor,
                    rawLength, 1});
    *cursor = static_cast<int>(body.size());
    return true;
  }
  const QList<CsgUpstreamFieldDef> &caseFields =
      caseIt.value().fields;
  const bool hasTrailingBlob =
      !caseFields.isEmpty() &&
      caseFields.constLast().type == CsgUpstreamFieldType::Blob;
  if (hasTrailingBlob &&
      service.passwordPolicy == CsgUpstreamTailPolicy::Optional &&
      body.size() - *cursor >= kPasswordLength) {
    const int rawLength = static_cast<int>(body.size()) - *cursor;
    fields->append({"未解析应用数据", HexBytes(body.mid(*cursor)),
                    "变长内容与可选 PW 无法唯一分界",
                    baseOffset + *cursor, rawLength, 1});
    *cursor = static_cast<int>(body.size());
    return true;
  }
  return CsgUpstreamFieldParser::parse(
      body, caseFields, cursor, baseOffset, QString(), 1,
      fields, errorMessage);
}

bool ParseTimedQueryFields(const QByteArray &body, int *cursor,
                           int baseOffset, bool hasDensity,
                           QList<CsgUpstreamField> *fields,
                           QString *errorMessage) {
  CsgUpstreamFieldDef start;
  start.name = "数据起始时间";
  start.type = CsgUpstreamFieldType::BcdDateTime;
  start.length = 5;
  CsgUpstreamFieldDef end = start;
  end.name = "数据结束时间";
  QList<CsgUpstreamFieldDef> definitions = {start, end};
  if (hasDensity) {
    CsgUpstreamFieldDef density;
    density.name = "数据密度";
    density.type = CsgUpstreamFieldType::Bin;
    density.length = 1;
    definitions.append(density);
  }
  return CsgUpstreamFieldParser::parse(
      body, definitions, cursor, baseOffset, QString(), 1,
      fields, errorMessage);
}

bool HasConfiguredDataUnitAt(const QByteArray &body, int cursor) {
  if (body.size() - cursor < kDataUnitHeaderLength) {
    return false;
  }
  const quint32 di = ReadLittleEndian32(body, cursor + 2);
  const DiLookup lookup = ResolveDiLookup(di);
  return CsgUpstreamConfigManager::instance().getDiDef(lookup.key) !=
         nullptr;
}

bool ParseStructContent(
    const QByteArray &body, const CsgUpstreamDiDef &definition,
    bool multipleDataUnits, bool optionalPassword, int *cursor,
    int baseOffset, bool *passwordBoundary,
    QList<CsgUpstreamField> *fields, QString *errorMessage) {
  int firstOptional = definition.fields.size();
  for (int index = 0; index < definition.fields.size(); ++index) {
    if (!definition.fields.at(index).required) {
      firstOptional = index;
      break;
    }
  }

  const QList<CsgUpstreamFieldDef> requiredFields =
      definition.fields.mid(0, firstOptional);
  if (!CsgUpstreamFieldParser::parse(
          body, requiredFields, cursor, baseOffset, QString(), 1,
          fields, errorMessage)) {
    return false;
  }
  if (firstOptional == definition.fields.size()) {
    return true;
  }
  const QList<CsgUpstreamFieldDef> optionalFields =
      definition.fields.mid(firstOptional);
  QList<int> optionalOffsets = {0};
  int totalOptionalLength = 0;
  for (const CsgUpstreamFieldDef &optionalField : optionalFields) {
    totalOptionalLength += optionalField.length;
    optionalOffsets.append(totalOptionalLength);
  }

  const int remaining = body.size() - *cursor;
  int optionalBytesToParse = -1;
  bool passwordAfterOptional = false;
  if (multipleDataUnits) {
    for (int offset : optionalOffsets) {
      if (HasConfiguredDataUnitAt(body, *cursor + offset)) {
        optionalBytesToParse = offset;
        break;
      }
    }
  }
  if (optionalBytesToParse < 0 && optionalPassword) {
    for (int offset : optionalOffsets) {
      if (remaining - offset == kPasswordLength) {
        optionalBytesToParse = offset;
        passwordAfterOptional = true;
        break;
      }
    }
  }
  if (optionalBytesToParse < 0) {
    for (int offset : optionalOffsets) {
      if (remaining == offset) {
        optionalBytesToParse = offset;
        break;
      }
    }
  }
  if (optionalBytesToParse < 0 && multipleDataUnits &&
      remaining >= kDataUnitHeaderLength) {
    fields->append({"未解析应用数据", HexBytes(body.mid(*cursor)),
                    "DI 可选字段与下一未知 DI 无法唯一分界",
                    baseOffset + *cursor, remaining, 1});
    *cursor = static_cast<int>(body.size());
    return true;
  }
  if (optionalBytesToParse < 0) {
    optionalBytesToParse = totalOptionalLength;
  }

  int parsedOptionalBytes = 0;
  for (const CsgUpstreamFieldDef &optionalField : optionalFields) {
    if (parsedOptionalBytes == optionalBytesToParse) {
      break;
    }
    if (!CsgUpstreamFieldParser::parse(
            body, {optionalField}, cursor, baseOffset, QString(), 1,
            fields, errorMessage)) {
      return false;
    }
    parsedOptionalBytes += optionalField.length;
  }
  if (passwordAfterOptional) {
    *passwordBoundary = true;
  }
  return true;
}

bool ParseTimedContents(
    const QByteArray &body, const CsgUpstreamDiDef *definition,
    bool optionalPassword, int *historyIndex, int *cursor, int baseOffset,
    QList<CsgUpstreamField> *fields, QString *errorMessage) {
  if (!definition) {
    const auto &manager = CsgUpstreamConfigManager::instance();
    for (int candidate = *cursor;
         candidate + kDataUnitHeaderLength <= body.size();
         ++candidate) {
      const quint32 candidateDi =
          ReadLittleEndian32(body, candidate + 2);
      const DiLookup lookup = ResolveDiLookup(candidateDi);
      if (manager.getDiDef(lookup.key)) {
        *errorMessage =
            "AFN0D 上行遇到中间未知 DI，无法确定历史数据内容长度和后续 DA/DI 边界";
        return false;
      }
    }
    const int rawLength = body.size() - *cursor;
    QString description = "未知 DI 的历史内容按原文保留";
    if (optionalPassword && rawLength >= kPasswordLength) {
      description += "；变长内容与可选 PW 无法唯一分界";
    }
    fields->append({"未展开服务内容", HexBytes(body.mid(*cursor)),
                    description, baseOffset + *cursor,
                    rawLength, 1});
    *cursor = body.size();
    return true;
  }
  if (definition->type != "Struct" && definition->type != "None") {
    *errorMessage = QString("AFN0D 上行 DI 类型 %1 不能确定历史数据内容长度")
                        .arg(definition->type);
    return false;
  }
  if (body.size() - *cursor < 1) {
    *errorMessage = "数据标识内容数数据不足，需要 1 字节";
    return false;
  }

  const int countOffset = *cursor;
  const quint8 contentCount = ByteAt(body, *cursor);
  fields->append({"数据标识内容数", HexBytes(body.mid(*cursor, 1)),
                  QString::number(contentCount),
                  baseOffset + *cursor, 1, 1});
  ++(*cursor);
  if (contentCount == 0) {
    *errorMessage = QString("数据标识内容数不能为 0（偏移 %1）")
                        .arg(baseOffset + countOffset);
    return false;
  }

  for (quint8 content = 0; content < contentCount; ++content) {
    ++(*historyIndex);
    const QString prefix =
        QString("历史数据[%1]/").arg(*historyIndex);
    if (body.size() - *cursor < 1) {
      *errorMessage = QString("%1数据标识内容不足").arg(prefix);
      return false;
    }

    if (ByteAt(body, *cursor) == 0xFFU) {
      fields->append({prefix + "无数据", "FF", "无数据填充",
                      baseOffset + *cursor, 1, 2});
      ++(*cursor);
    } else if (definition->type == "Struct") {
      if (!CsgUpstreamFieldParser::parse(
              body, definition->fields, cursor, baseOffset, prefix, 2,
              fields, errorMessage)) {
        *errorMessage = prefix + *errorMessage;
        return false;
      }
    }

    if (body.size() - *cursor < 5) {
      *errorMessage = QString("%1数据时间不足，需要 5 字节，剩余 %2 字节")
                          .arg(prefix)
                          .arg(body.size() - *cursor);
      return false;
    }
    CsgUpstreamFieldDef time;
    time.name = "数据时间";
    time.sourceType = "YYMMDDhhmm";
    time.type = CsgUpstreamFieldType::BcdDateTime;
    time.length = 5;
    if (!CsgUpstreamFieldParser::parse(
            body, {time}, cursor, baseOffset, prefix, 2,
            fields, errorMessage)) {
      *errorMessage = prefix + *errorMessage;
      return false;
    }
  }
  return true;
}

enum class OptionalPasswordProbe {
  Password,
  DataUnit,
  Ambiguous
};

OptionalPasswordProbe ProbeOptionalPasswordBoundary(
    const QByteArray &body, int cursor,
    const CsgUpstreamServiceDef &service) {
  if (body.size() - cursor != kPasswordLength) {
    return OptionalPasswordProbe::DataUnit;
  }
  if (body.size() - cursor < kDataUnitHeaderLength) {
    return OptionalPasswordProbe::Password;
  }

  int trialCursor = cursor + kDataUnitHeaderLength;
  QList<CsgUpstreamField> trialFields;
  QString trialError;
  switch (service.layout) {
  case CsgUpstreamServiceLayout::Content: {
    const quint32 di = ReadLittleEndian32(body, cursor + 2);
    const DiLookup lookup = ResolveDiLookup(di);
    const CsgUpstreamDiDef *definition =
        CsgUpstreamConfigManager::instance().getDiDef(lookup.key);
    if (!definition || definition->type == "ServiceDefined") {
      return OptionalPasswordProbe::Ambiguous;
    }
    if (definition->type != "Struct") {
      return OptionalPasswordProbe::Password;
    }
    if (!CsgUpstreamFieldParser::parse(
            body, definition->fields, &trialCursor, 0, QString(), 0,
            &trialFields, &trialError)) {
      return OptionalPasswordProbe::Password;
    }
    return trialCursor == body.size()
               ? OptionalPasswordProbe::DataUnit
               : OptionalPasswordProbe::Password;
  }
  case CsgUpstreamServiceLayout::TimedQuery:
    if (ParseTimedQueryFields(body, &trialCursor, 0, false,
                              &trialFields, &trialError) &&
        trialCursor == body.size()) {
      return OptionalPasswordProbe::DataUnit;
    }
    return OptionalPasswordProbe::Password;
  case CsgUpstreamServiceLayout::Opaque:
  case CsgUpstreamServiceLayout::TimedContent:
    return OptionalPasswordProbe::Ambiguous;
  case CsgUpstreamServiceLayout::Query:
  case CsgUpstreamServiceLayout::TimedQueryWithDensity:
  case CsgUpstreamServiceLayout::Error:
  case CsgUpstreamServiceLayout::Struct:
  case CsgUpstreamServiceLayout::Choice:
    return OptionalPasswordProbe::Password;
  }
  return OptionalPasswordProbe::Ambiguous;
}

void PreserveUnknownDataUnit(
    const QByteArray &body, int diOffset, int baseOffset,
    bool optionalPassword, int *cursor,
    QList<CsgUpstreamField> *fields) {
  const int rawLength = static_cast<int>(body.size()) - diOffset;
  const QString description =
      optionalPassword
          ? "未知 DI 内容与可选 PW 无法唯一分界"
          : "未知 DI 内容长度暂未配置，按原文保留";
  fields->append({"未解析应用数据", HexBytes(body.mid(diOffset)),
                  description, baseOffset + diOffset, rawLength, 1});
  *cursor = static_cast<int>(body.size());
}

bool ParseErrorCodeField(const QByteArray &body, int *cursor,
                         int baseOffset,
                         QList<CsgUpstreamField> *fields,
                         QString *errorMessage) {
  if (body.size() - *cursor < 1) {
    *errorMessage = "错误码 ERR 数据不足，需要 1 字节";
    return false;
  }
  fields->append({"错误码 ERR", HexBytes(body.mid(*cursor, 1)),
                  DecodeErrorCode(ByteAt(body, *cursor)),
                  baseOffset + *cursor, 1, 1});
  ++(*cursor);
  return true;
}

bool ParseFileSegment(const QByteArray &body, int *cursor,
                      int baseOffset,
                      QList<CsgUpstreamField> *fields,
                      QString *errorMessage) {
  if (body.size() - *cursor < 4) {
    *errorMessage = "文件段号和文件段长度数据不足，需要 4 字节";
    return false;
  }
  const quint16 segmentNumber = ReadLittleEndian16(body, *cursor);
  fields->append({"文件段号", HexBytes(body.mid(*cursor, 2)),
                  QString::number(segmentNumber),
                  baseOffset + *cursor, 2, 1});
  *cursor += 2;
  const quint16 segmentLength = ReadLittleEndian16(body, *cursor);
  fields->append({"文件段长度", HexBytes(body.mid(*cursor, 2)),
                  QString::number(segmentLength),
                  baseOffset + *cursor, 2, 1});
  *cursor += 2;
  const int remaining = body.size() - *cursor;
  if (remaining < static_cast<int>(segmentLength) + 2) {
    *errorMessage =
        QString("文件段内容数据不足，需要 %1 字节内容和 2 字节段校验，剩余 %2 字节")
            .arg(segmentLength)
            .arg(remaining);
    return false;
  }
  const QByteArray content =
      body.mid(*cursor, static_cast<int>(segmentLength));
  fields->append({"文件段内容", HexBytes(content),
                  QString("BIN，%1 字节").arg(segmentLength),
                  baseOffset + *cursor,
                  static_cast<int>(segmentLength), 1});
  *cursor += static_cast<int>(segmentLength);
  fields->append({"段校验", HexBytes(body.mid(*cursor, 2)),
                  "CRC16-CCITT（多项式 0x1021）；规范未明确初始值、反射、异或输出和覆盖范围，校验结果未判定",
                  baseOffset + *cursor, 2, 1});
  *cursor += 2;
  return true;
}

QString RelayTypeName(quint8 value) {
  static const QStringList names = {
      "普通中继", "拉闸命令", "允许合闸命令",
      "保电投入命令", "保电解除命令"};
  return value < names.size()
             ? names.at(value)
             : QString("未知中继类型 %1").arg(value);
}

bool AppendRelayByte(const QByteArray &body, int *cursor,
                     int baseOffset, const QString &name,
                     const QString &description,
                     QList<CsgUpstreamField> *fields,
                     QString *errorMessage) {
  if (*cursor >= body.size()) {
    *errorMessage = name + "数据不足，需要 1 字节";
    return false;
  }
  fields->append({name, HexBytes(body.mid(*cursor, 1)), description,
                  baseOffset + *cursor, 1, 1});
  ++(*cursor);
  return true;
}

bool ParseRelayContent(const QByteArray &body, const QString &diKey,
                       const QString &direction, int *cursor,
                       int baseOffset,
                       QList<CsgUpstreamField> *fields,
                       QString *errorMessage) {
  if (*cursor >= body.size()) {
    *errorMessage = "中继类型数据不足，需要 1 字节";
    return false;
  }
  const quint8 relayType = ByteAt(body, *cursor);
  if (!AppendRelayByte(body, cursor, baseOffset, "中继类型",
                       RelayTypeName(relayType), fields,
                       errorMessage)) {
    return false;
  }
  if (direction == "down") {
    if (!AppendRelayByte(body, cursor, baseOffset, "端口号",
                         *cursor < body.size()
                             ? QString::number(ByteAt(body, *cursor))
                             : QString(),
                         fields, errorMessage) ||
        !AppendRelayByte(body, cursor, baseOffset, "通信波特率",
                         *cursor < body.size()
                             ? QString("%1 bps")
                                   .arg(ByteAt(body, *cursor) * 300)
                             : QString(),
                         fields, errorMessage) ||
        !AppendRelayByte(body, cursor, baseOffset, "校验方式",
                         *cursor < body.size()
                             ? QString::number(ByteAt(body, *cursor))
                             : QString(),
                         fields, errorMessage) ||
        !AppendRelayByte(body, cursor, baseOffset, "数据位",
                         *cursor < body.size()
                             ? QString::number(ByteAt(body, *cursor))
                             : QString(),
                         fields, errorMessage) ||
        !AppendRelayByte(body, cursor, baseOffset, "停止位",
                         *cursor < body.size()
                             ? QString::number(ByteAt(body, *cursor))
                             : QString(),
                         fields, errorMessage) ||
        !AppendRelayByte(body, cursor, baseOffset, "超时时间",
                         *cursor < body.size()
                             ? QString("%1 s").arg(ByteAt(body, *cursor))
                             : QString(),
                         fields, errorMessage)) {
      return false;
    }
  }
  const int lengthBytes = diKey == "E3020002" ? 2 : 1;
  if (body.size() - *cursor < lengthBytes) {
    *errorMessage = QString("中继报文长度数据不足，需要 %1 字节")
                        .arg(lengthBytes);
    return false;
  }
  const quint16 length = lengthBytes == 1
                             ? ByteAt(body, *cursor)
                             : ReadLittleEndian16(body, *cursor);
  fields->append({"中继报文长度",
                  HexBytes(body.mid(*cursor, lengthBytes)),
                  QString::number(length), baseOffset + *cursor,
                  lengthBytes, 1});
  *cursor += lengthBytes;
  if (body.size() - *cursor < static_cast<int>(length)) {
    *errorMessage =
        QString("中继报文数据不足，需要 %1 字节，剩余 %2 字节")
            .arg(length)
            .arg(body.size() - *cursor);
    return false;
  }
  fields->append({"中继报文",
                  HexBytes(body.mid(*cursor, static_cast<int>(length))),
                  QString("BIN，%1 字节").arg(length),
                  baseOffset + *cursor, static_cast<int>(length), 1});
  *cursor += static_cast<int>(length);
  return true;
}

bool ParseDataUnits(const QByteArray &body, quint8 afn,
                    const CsgUpstreamServiceDef &service,
                    int *cursor, int baseOffset,
                    QList<CsgUpstreamField> *fields,
                    QString *errorMessage) {
  int unitCount = 0;
  int historyIndex = 0;
  while (*cursor < body.size()) {
    if (service.passwordPolicy == CsgUpstreamTailPolicy::Optional &&
        unitCount > 0 && body.size() - *cursor == kPasswordLength) {
      const OptionalPasswordProbe probe =
          ProbeOptionalPasswordBoundary(body, *cursor, service);
      if (probe == OptionalPasswordProbe::Password) {
        break;
      }
      if (probe == OptionalPasswordProbe::Ambiguous) {
        quint32 ambiguousDi = 0;
        QString ambiguousDiKey;
        if (!AppendDataUnitHeader(
                body, cursor, baseOffset, fields, &ambiguousDi,
                &ambiguousDiKey, errorMessage)) {
          return false;
        }
        PreserveUnknownDataUnit(
            body, *cursor - 4, baseOffset, true, cursor, fields);
        return true;
      }
    }

    quint32 di = 0;
    QString diKey;
    if (!AppendDataUnitHeader(body, cursor, baseOffset, fields,
                              &di, &diKey, errorMessage)) {
      return false;
    }
    ++unitCount;

    const CsgUpstreamDiDef *diDefinition =
        CsgUpstreamConfigManager::instance().getDiDef(diKey);
    bool passwordBoundary = false;
    switch (service.layout) {
    case CsgUpstreamServiceLayout::Query:
      break;
    case CsgUpstreamServiceLayout::Content:
      if (afn == 0x0F && service.direction == "up" &&
          (di == 0xE3010001U || di == 0xE3010002U ||
           di == 0xE3010006U)) {
        if (!ParseErrorCodeField(body, cursor, baseOffset, fields,
                                 errorMessage)) {
          return false;
        }
        break;
      }
      if (afn == 0x0F && service.direction == "down" &&
          di == 0xE3010003U) {
        break;
      }
      if (!diDefinition) {
        PreserveUnknownDataUnit(
            body, *cursor - 4, baseOffset,
            service.passwordPolicy == CsgUpstreamTailPolicy::Optional,
            cursor, fields);
        return true;
      }
      if (diKey == "E3010002") {
        if (!ParseFileSegment(body, cursor, baseOffset, fields,
                              errorMessage)) {
          return false;
        }
        break;
      }
      if (diKey == "E3020001" || diKey == "E3020002") {
        if (!ParseRelayContent(body, diKey, service.direction, cursor,
                               baseOffset, fields, errorMessage)) {
          return false;
        }
        break;
      }
      if (diDefinition->type == "Struct" &&
          !ParseStructContent(
              body, *diDefinition, service.multipleDataUnits,
              service.passwordPolicy ==
                  CsgUpstreamTailPolicy::Optional,
              cursor, baseOffset, &passwordBoundary, fields,
              errorMessage)) {
        return false;
      }
      if (diDefinition->type == "ServiceDefined") {
        PreserveUnknownDataUnit(
            body, *cursor - 4, baseOffset,
            service.passwordPolicy == CsgUpstreamTailPolicy::Optional,
            cursor, fields);
        return true;
      }
      break;
    case CsgUpstreamServiceLayout::TimedQuery:
      if (!ParseTimedQueryFields(body, cursor, baseOffset, false,
                                 fields, errorMessage)) {
        return false;
      }
      break;
    case CsgUpstreamServiceLayout::TimedQueryWithDensity:
      if (!ParseTimedQueryFields(body, cursor, baseOffset, true,
                                 fields, errorMessage)) {
        return false;
      }
      break;
    case CsgUpstreamServiceLayout::Error:
      if (!ParseErrorCodeField(body, cursor, baseOffset, fields,
                               errorMessage)) {
        return false;
      }
      break;
    case CsgUpstreamServiceLayout::TimedContent:
      if (!ParseTimedContents(
              body, diDefinition,
              service.passwordPolicy ==
                  CsgUpstreamTailPolicy::Optional,
              &historyIndex, cursor, baseOffset, fields,
              errorMessage)) {
        return false;
      }
      break;
    case CsgUpstreamServiceLayout::Opaque:
    {
      const int rawLength = static_cast<int>(body.size()) - *cursor;
      QString description = "当前阶段按原文保留";
      if (service.passwordPolicy == CsgUpstreamTailPolicy::Optional &&
          rawLength >= kPasswordLength) {
        description += "；变长内容与可选 PW 无法唯一分界";
      }
      fields->append({"未展开服务内容", HexBytes(body.mid(*cursor)),
                      description, baseOffset + *cursor,
                      rawLength, 1});
      *cursor = body.size();
      break;
    }
    case CsgUpstreamServiceLayout::Struct:
    case CsgUpstreamServiceLayout::Choice:
      *errorMessage = "DataUnits 不支持 Struct/Choice 布局";
      return false;
    }

    if (passwordBoundary) {
      break;
    }

    if (!service.multipleDataUnits) {
      break;
    }
  }
  return unitCount > 0;
}

} // namespace

bool CsgUpstreamServiceParser::parse(
    const QByteArray &applicationData, quint8 afn, quint8 seq,
    const CsgUpstreamServiceDef &service, int baseOffset,
    QList<CsgUpstreamField> *fields, QString *errorMessage) {
  QByteArray body = applicationData;
  QByteArray timeTag;
  QByteArray password;
  QByteArray taskDataTime;
  int timeTagOffset = -1;
  int passwordOffset = -1;
  int taskDataTimeOffset = -1;

  if (service.timeTagFromSeq && (seq & 0x80) != 0) {
    if (body.size() < kDataUnitHeaderLength + kTimeTagLength) {
      *errorMessage =
          QString("时间标签 Tp 数据不足，需要 5 字节，应用数据共 %1 字节")
              .arg(body.size());
      return false;
    }
    timeTagOffset = baseOffset + body.size() - kTimeTagLength;
    timeTag = body.right(kTimeTagLength);
    body.chop(kTimeTagLength);
  }

  if (service.passwordPolicy == CsgUpstreamTailPolicy::Required) {
    if (body.size() < kDataUnitHeaderLength + kPasswordLength) {
      *errorMessage =
          QString("消息认证码 PW 数据不足，需要 16 字节，Tp 前应用数据共 %1 字节")
              .arg(body.size());
      return false;
    }
    passwordOffset = baseOffset + body.size() - kPasswordLength;
    password = body.right(kPasswordLength);
    body.chop(kPasswordLength);
  }

  int cursor = 0;
  bool parsed = false;
  if (afn == 0x12 && service.direction == "up") {
    CsgUpstreamFieldDef taskTimeDefinition;
    taskTimeDefinition.name = "任务数据时间";
    taskTimeDefinition.sourceType = "YYYYMMDDhhmm";
    taskTimeDefinition.type = CsgUpstreamFieldType::BcdDateTime;
    taskTimeDefinition.length = 6;
    struct TaskTimeLayout {
      bool valid = false;
      QByteArray body;
      QByteArray time;
      int timeOffset = -1;
      int cursor = 0;
      QList<CsgUpstreamField> fields;
    };
    const auto tryTaskTimeLayout =
        [&body, afn, &service, baseOffset,
         &taskTimeDefinition](int timeOffset,
                              int expectedTailLength) {
          TaskTimeLayout layout;
          if (timeOffset < 0 || body.size() - timeOffset < 6) {
            return layout;
          }
          const QByteArray candidate = body.mid(timeOffset, 6);
          const CsgUpstreamDecodedValue decoded =
              CsgUpstreamValueDecoder::decode(candidate,
                                              taskTimeDefinition);
          if (!decoded.ok || decoded.text == "无数据") {
            return layout;
          }
          layout.body = body;
          layout.body.remove(timeOffset, 6);
          QString trialError;
          const bool serviceParsed =
              service.type == "DataUnits"
                  ? ParseDataUnits(layout.body, afn, service,
                                   &layout.cursor, baseOffset,
                                   &layout.fields, &trialError)
                  : ParseLegacyService(layout.body, service,
                                       &layout.cursor, baseOffset,
                                       &layout.fields, &trialError);
          if (!serviceParsed ||
              layout.body.size() - layout.cursor != expectedTailLength) {
            return TaskTimeLayout();
          }
          layout.valid = true;
          layout.time = candidate;
          layout.timeOffset = baseOffset + timeOffset;
          return layout;
        };
    const TaskTimeLayout withoutPassword =
        tryTaskTimeLayout(body.size() - 6, 0);
    const TaskTimeLayout withPassword =
        service.passwordPolicy == CsgUpstreamTailPolicy::Optional
            ? tryTaskTimeLayout(
                  body.size() - kPasswordLength - 6,
                  kPasswordLength)
            : TaskTimeLayout();
    if (withoutPassword.valid && withPassword.valid) {
      *errorMessage =
          "AFN12 尾部同时符合任务数据时间和任务数据时间+PW，边界存在歧义";
      return false;
    }
    const TaskTimeLayout *layout = withoutPassword.valid
                                       ? &withoutPassword
                                       : withPassword.valid
                                             ? &withPassword
                                             : nullptr;
    if (layout) {
      body = layout->body;
      taskDataTime = layout->time;
      taskDataTimeOffset = layout->timeOffset;
      cursor = layout->cursor;
      for (const CsgUpstreamField &field : layout->fields) {
        fields->append(field);
      }
      parsed = true;
    }
  }

  if (!parsed) {
    parsed = service.type == "DataUnits"
                 ? ParseDataUnits(body, afn, service, &cursor,
                                  baseOffset, fields, errorMessage)
                 : ParseLegacyService(body, service, &cursor,
                                      baseOffset, fields,
                                      errorMessage);
  }
  if (!parsed) {
    return false;
  }

  if (!taskDataTime.isEmpty()) {
    CsgUpstreamFieldDef taskTime;
    taskTime.name = "任务数据时间";
    taskTime.sourceType = "YYYYMMDDhhmm";
    taskTime.type = CsgUpstreamFieldType::BcdDateTime;
    taskTime.length = 6;
    const CsgUpstreamDecodedValue decoded =
        CsgUpstreamValueDecoder::decode(taskDataTime, taskTime);
    if (!decoded.ok) {
      *errorMessage = "任务数据时间: " + decoded.error;
      return false;
    }
    fields->append({"任务数据时间", HexBytes(taskDataTime),
                    decoded.text, taskDataTimeOffset, 6, 1});
  }

  if (service.passwordPolicy == CsgUpstreamTailPolicy::Optional &&
      body.size() - cursor == kPasswordLength) {
    passwordOffset = baseOffset + cursor;
    if (!taskDataTime.isEmpty() &&
        taskDataTimeOffset == passwordOffset) {
      passwordOffset += 6;
    }
    password = body.mid(cursor, kPasswordLength);
    cursor += kPasswordLength;
  }
  if (cursor != body.size()) {
    *errorMessage =
        QString("Service 存在 %1 字节未定义数据")
            .arg(body.size() - cursor);
    return false;
  }

  if (!password.isEmpty()) {
    fields->append({"消息认证码 PW", HexBytes(password),
                    "16 字节消息认证码", passwordOffset,
                    kPasswordLength, 0});
  }
  if (!timeTag.isEmpty()) {
    fields->append({"时间标签 Tp", HexBytes(timeTag),
                    "DDhhmmss 启动帧发送时标和允许延时",
                    timeTagOffset, kTimeTagLength, 0});
  }
  return true;
}
