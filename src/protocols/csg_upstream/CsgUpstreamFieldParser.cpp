#include "CsgUpstreamFieldParser.h"

#include "CsgUpstreamConfigManager.h"
#include "CsgUpstreamValueDecoder.h"

#include <QMap>
#include <QStringList>

#include <limits>

namespace {

quint8 ByteAt(const QByteArray &data, int offset) {
  return static_cast<quint8>(data.at(offset));
}

QString HexBytes(const QByteArray &data) {
  return QString::fromLatin1(data.toHex()).toUpper();
}

QString HexCode(quint32 value, int width) {
  return QString("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString ComposeDescription(const QString &parsed,
                           const QString &note) {
  if (parsed.isEmpty()) {
    return note;
  }
  return note.isEmpty() ? parsed : parsed + "（" + note + "）";
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

quint64 ReadBcdUnsigned(const QByteArray &bytes, bool littleEndian) {
  quint64 value = 0;
  if (littleEndian) {
    for (int index = bytes.size() - 1; index >= 0; --index) {
      const quint8 byte = ByteAt(bytes, index);
      value = value * 100 + (byte >> 4) * 10 + (byte & 0x0FU);
    }
  } else {
    for (int index = 0; index < bytes.size(); ++index) {
      const quint8 byte = ByteAt(bytes, index);
      value = value * 100 + (byte >> 4) * 10 + (byte & 0x0FU);
    }
  }
  return value;
}

} // namespace

bool CsgUpstreamFieldParser::parse(
    const QByteArray &payload,
    const QList<CsgUpstreamFieldDef> &definitions,
    int *cursor, int baseOffset, const QString &namePrefix,
    int indent, QList<CsgUpstreamField> *fields,
    QString *errorMessage) {
  QMap<QString, quint64> numericValues;
  return parseDefinitions(payload, definitions, cursor, baseOffset,
                          namePrefix, indent, &numericValues, fields,
                          errorMessage);
}

bool CsgUpstreamFieldParser::parseDefinitions(
    const QByteArray &payload,
    const QList<CsgUpstreamFieldDef> &definitions,
    int *cursor, int baseOffset, const QString &namePrefix,
    int indent, QMap<QString, quint64> *numericValues,
    QList<CsgUpstreamField> *fields, QString *errorMessage) {
  for (const CsgUpstreamFieldDef &definition : definitions) {
    const int remaining = payload.size() - *cursor;
    if (!definition.required && remaining == 0) {
      continue;
    }

    if (definition.type == CsgUpstreamFieldType::DataItemGroups) {
      if (!parseDataItemGroups(
              payload, definition,
              numericValues->value(definition.pointCountRef),
              numericValues->value(definition.diCountRef), cursor,
              baseOffset, fields, errorMessage)) {
        return false;
      }
      continue;
    }
    if (definition.type ==
        CsgUpstreamFieldType::ReferencedDataItems) {
      if (!parseReferencedDataItems(
              payload, definition,
              numericValues->value(definition.countRef), cursor,
              baseOffset, fields, errorMessage)) {
        return false;
      }
      continue;
    }

    quint64 repeatCount = definition.countRef.isEmpty()
                              ? 1
                              : numericValues->value(
                                    definition.countRef);
    if (repeatCount == 0xFFU && definition.countRefFfValue > 0) {
      repeatCount = static_cast<quint64>(
          definition.countRefFfValue);
    }
    for (quint64 repeat = 0; repeat < repeatCount; ++repeat) {
      QString fieldName = namePrefix + definition.name;
      if (!definition.countRef.isEmpty()) {
        fieldName += QString("[%1]").arg(repeat + 1);
      }

      if (definition.type == CsgUpstreamFieldType::Struct ||
          definition.type == CsgUpstreamFieldType::Choice) {
        const QList<CsgUpstreamFieldDef> *childDefinitions =
            &definition.children;
        QString parentDescription = "复合结构";
        if (definition.type == CsgUpstreamFieldType::Choice) {
          const quint64 selectorValue =
              numericValues->value(definition.selectorRef);
          const auto caseIt = definition.cases.constFind(selectorValue);
          if (caseIt == definition.cases.cend()) {
            *errorMessage =
                QString("字段 %1 未配置选择值 %2")
                    .arg(definition.name)
                    .arg(selectorValue);
            return false;
          }
          childDefinitions = &caseIt.value();
          parentDescription =
              QString("选择值 %1").arg(selectorValue);
        }

        const int fieldStart = *cursor;
        QList<CsgUpstreamField> childFields;
        QMap<QString, quint64> childValues;
        if (!parseDefinitions(
                payload, *childDefinitions, cursor, baseOffset,
                fieldName + "/", indent + 1, &childValues,
                &childFields, errorMessage)) {
          return false;
        }
        const int fieldLength = *cursor - fieldStart;
        fields->append({fieldName,
                        HexBytes(payload.mid(fieldStart, fieldLength)),
                        parentDescription, baseOffset + fieldStart,
                        fieldLength, indent});
        for (const CsgUpstreamField &childField : childFields) {
          fields->append(childField);
        }
        continue;
      }

      const int repeatedRemaining = payload.size() - *cursor;
      int fieldLength = definition.length;
      if (!definition.lengthRef.isEmpty()) {
        const quint64 referencedLength =
            numericValues->value(definition.lengthRef);
        if (referencedLength >
            static_cast<quint64>(std::numeric_limits<int>::max())) {
          *errorMessage =
              "字段 " + definition.name + " 的 lengthRef 超出范围";
          return false;
        }
        fieldLength = static_cast<int>(referencedLength);
      } else if (definition.type == CsgUpstreamFieldType::Blob &&
                 definition.length == -1) {
        fieldLength = repeatedRemaining;
      }
      if (repeatedRemaining < fieldLength) {
        *errorMessage =
            QString("字段 %1 数据不足，需要 %2 字节，剩余 %3 字节")
                .arg(definition.name)
                .arg(fieldLength)
                .arg(repeatedRemaining);
        return false;
      }

      const QByteArray fieldBytes = payload.mid(*cursor, fieldLength);
      const CsgUpstreamDecodedValue decoded =
          CsgUpstreamValueDecoder::decode(fieldBytes, definition);
      if (!decoded.ok) {
        *errorMessage = "字段 " + definition.name + ": " + decoded.error;
        return false;
      }
      const quint64 numericValue =
          definition.type == CsgUpstreamFieldType::Bcd
              ? ReadBcdUnsigned(fieldBytes, definition.littleEndian)
              : ReadUnsigned(fieldBytes, definition.littleEndian);
      if (!definition.productRef.isEmpty()) {
        const quint64 referenceValue =
            numericValues->value(definition.productRef);
        const quint64 maximum = definition.productMaxExclusive;
        if (referenceValue != 0 &&
            numericValue > (maximum - 1) / referenceValue) {
          *errorMessage =
              QString("字段 %1 与 %2 的乘积必须小于 %3")
                  .arg(definition.name)
                  .arg(definition.productRef)
                  .arg(maximum);
          return false;
        }
      }
      fields->append({fieldName, HexBytes(fieldBytes),
                      ComposeDescription(decoded.text,
                                         definition.description),
                      baseOffset + *cursor, fieldLength, indent});
      if (!definition.id.isEmpty()) {
        numericValues->insert(definition.id, numericValue);
      }
      *cursor += fieldLength;
    }
  }
  return true;
}

bool CsgUpstreamFieldParser::parseDataItemGroups(
    const QByteArray &payload,
    const CsgUpstreamFieldDef &definition,
    quint64 pointCount, quint64 diCount, int *cursor,
    int baseOffset, QList<CsgUpstreamField> *fields,
    QString *errorMessage) {
  const quint64 groupCount = pointCount * diCount;
  if (groupCount >= 256) {
    *errorMessage = QString("任务数据组数 n×m 必须小于 256，实际为 %1")
                        .arg(groupCount);
    return false;
  }

  const auto &manager = CsgUpstreamConfigManager::instance();
  for (quint64 group = 0; group < groupCount; ++group) {
    const QString prefix =
        QString("%1[%2]/").arg(definition.name).arg(group + 1);
    int remaining = payload.size() - *cursor;
    if (remaining < 2) {
      *errorMessage =
          QString("第 %1 组信息点标识数据不足，需要 2 字节，剩余 %2 字节")
              .arg(group + 1)
              .arg(remaining);
      return false;
    }
    const quint8 da1 = ByteAt(payload, *cursor);
    const quint8 da2 = ByteAt(payload, *cursor + 1);
    fields->append({prefix + "信息点标识",
                    HexBytes(payload.mid(*cursor, 2)),
                    decodeDa(da1, da2), baseOffset + *cursor, 2, 2});
    *cursor += 2;

    remaining = payload.size() - *cursor;
    if (remaining < 4) {
      *errorMessage =
          QString("第 %1 组数据标识编码数据不足，需要 4 字节，剩余 %2 字节")
              .arg(group + 1)
              .arg(remaining);
      return false;
    }
    const quint32 di =
        static_cast<quint32>(ByteAt(payload, *cursor)) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 1)) << 8) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 2)) << 16) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 3)) << 24);
    const QString diKey = HexCode(di, 8);
    const CsgUpstreamDiDef *diDefinition = manager.getDiDef(diKey);
    fields->append({prefix + "数据标识编码",
                    HexBytes(payload.mid(*cursor, 4)),
                    diKey +
                        (diDefinition
                             ? " " + diDefinition->name
                             : " 数据项暂未配置"),
                    baseOffset + *cursor, 4, 2});
    *cursor += 4;

    if (!diDefinition) {
      if (group + 1 < groupCount) {
        *errorMessage =
            QString("第 %1 组为非最后一组，内层 DI %2 未配置，无法确定后续任务组边界")
                .arg(group + 1)
                .arg(diKey);
        return false;
      }
      remaining = payload.size() - *cursor;
      if (remaining < 5) {
        *errorMessage =
            QString("第 %1 组未知内层 DI 缺少 5 字节数据时间")
                .arg(group + 1);
        return false;
      }
      const int rawLength = remaining - 5;
      fields->append({prefix + "未配置数据标识内容",
                      HexBytes(payload.mid(*cursor, rawLength)),
                      "内层 DI 未配置，按原文保留",
                      baseOffset + *cursor, rawLength, 3});
      *cursor += rawLength;
    } else if (diDefinition->type == "Struct") {
      if (!parse(payload, diDefinition->fields, cursor, baseOffset,
                 prefix, 3, fields, errorMessage)) {
        *errorMessage = QString("第 %1 组内层 DI %2: %3")
                            .arg(group + 1)
                            .arg(diKey, *errorMessage);
        return false;
      }
    } else if (diDefinition->type != "None") {
      *errorMessage =
          QString("第 %1 组内层 DI %2 的类型 %3 不能确定内容长度")
              .arg(group + 1)
              .arg(diKey, diDefinition->type);
      return false;
    }

    remaining = payload.size() - *cursor;
    if (remaining < 5) {
      *errorMessage =
          QString("第 %1 组数据时间不足，需要 5 字节，剩余 %2 字节")
              .arg(group + 1)
              .arg(remaining);
      return false;
    }
    CsgUpstreamFieldDef timeDefinition;
    timeDefinition.name = "数据时间";
    timeDefinition.sourceType = "YYMMDDhhmm";
    timeDefinition.type = CsgUpstreamFieldType::BcdDateTime;
    timeDefinition.length = 5;
    const CsgUpstreamDecodedValue decodedTime =
        CsgUpstreamValueDecoder::decode(
            payload.mid(*cursor, 5), timeDefinition);
    if (!decodedTime.ok) {
      *errorMessage = QString("第 %1 组数据时间: %2")
                          .arg(group + 1)
                          .arg(decodedTime.error);
      return false;
    }
    fields->append({prefix + "数据时间",
                    HexBytes(payload.mid(*cursor, 5)), decodedTime.text,
                    baseOffset + *cursor, 5, 2});
    *cursor += 5;
  }
  return true;
}

bool CsgUpstreamFieldParser::parseReferencedDataItems(
    const QByteArray &payload,
    const CsgUpstreamFieldDef &definition, quint64 itemCount,
    int *cursor, int baseOffset, QList<CsgUpstreamField> *fields,
    QString *errorMessage) {
  const auto &manager = CsgUpstreamConfigManager::instance();
  for (quint64 item = 0; item < itemCount; ++item) {
    const QString prefix =
        QString("%1[%2]/").arg(definition.name).arg(item + 1);
    const int remaining = payload.size() - *cursor;
    if (remaining < 4) {
      *errorMessage =
          QString("第 %1 个引用数据标识不足，需要 4 字节，剩余 %2 字节")
              .arg(item + 1)
              .arg(remaining);
      return false;
    }
    const quint32 di =
        static_cast<quint32>(ByteAt(payload, *cursor)) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 1)) << 8) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 2)) << 16) |
        (static_cast<quint32>(ByteAt(payload, *cursor + 3)) << 24);
    const QString diKey = HexCode(di, 8);
    const CsgUpstreamDiDef *diDefinition = manager.getDiDef(diKey);
    fields->append({prefix + "数据标识编码",
                    HexBytes(payload.mid(*cursor, 4)),
                    diKey +
                        (diDefinition
                             ? " " + diDefinition->name
                             : " 数据项暂未配置"),
                    baseOffset + *cursor, 4, 2});
    *cursor += 4;
    if (!diDefinition) {
      *errorMessage = QString("第 %1 个引用数据标识 %2 未配置")
                          .arg(item + 1)
                          .arg(diKey);
      return false;
    }
    if (diDefinition->type == "Struct") {
      if (!parse(payload, diDefinition->fields, cursor, baseOffset,
                 prefix, 3, fields, errorMessage)) {
        *errorMessage = QString("第 %1 个引用数据标识 %2: %3")
                            .arg(item + 1)
                            .arg(diKey, *errorMessage);
        return false;
      }
    } else if (diDefinition->type != "None") {
      *errorMessage =
          QString("第 %1 个引用数据标识 %2 的类型 %3 不能确定内容长度")
              .arg(item + 1)
              .arg(diKey, diDefinition->type);
      return false;
    }
  }
  return true;
}

QString CsgUpstreamFieldParser::decodeDa(quint8 da1, quint8 da2) {
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
