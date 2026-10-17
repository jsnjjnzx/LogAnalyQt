#include "CsgUpstreamConfigManager.h"

#include <QDate>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <limits>

namespace {

bool Matches(const QString &value, const QString &pattern) {
  return QRegularExpression(pattern).match(value).hasMatch();
}

QString FileError(const QString &filePath, const QString &message) {
  return QFileInfo(filePath).fileName() + ": " + message;
}

void SetError(QString *errorMessage, const QString &message) {
  if (errorMessage) {
    *errorMessage = message;
  }
}

bool ParseServiceLayout(const QString &value,
                        CsgUpstreamServiceLayout *layout) {
  if (value == "query") {
    *layout = CsgUpstreamServiceLayout::Query;
  } else if (value == "content") {
    *layout = CsgUpstreamServiceLayout::Content;
  } else if (value == "timed-query") {
    *layout = CsgUpstreamServiceLayout::TimedQuery;
  } else if (value == "timed-query-density") {
    *layout = CsgUpstreamServiceLayout::TimedQueryWithDensity;
  } else if (value == "timed-content") {
    *layout = CsgUpstreamServiceLayout::TimedContent;
  } else if (value == "error") {
    *layout = CsgUpstreamServiceLayout::Error;
  } else if (value == "opaque") {
    *layout = CsgUpstreamServiceLayout::Opaque;
  } else {
    return false;
  }
  return true;
}

bool ParseTailPolicy(const QString &value,
                     CsgUpstreamTailPolicy *policy) {
  if (value == "none") {
    *policy = CsgUpstreamTailPolicy::None;
  } else if (value == "optional") {
    *policy = CsgUpstreamTailPolicy::Optional;
  } else if (value == "required") {
    *policy = CsgUpstreamTailPolicy::Required;
  } else {
    return false;
  }
  return true;
}

bool ValidateNoAssociationAttributes(const QDomElement &element,
                                     const QString &context,
                                     const QString &filePath,
                                     QString *errorMessage,
                                     bool allowDirection = false) {
  if (!allowDirection && element.hasAttribute("direction")) {
    SetError(errorMessage,
             FileError(filePath,
                       context + " 不允许属性 direction"));
    return false;
  }
  const char *attributes[] = {
      "ref", "binding", "Binding", "schema", "Schema", "afn"};
  for (const char *attribute : attributes) {
    if (element.hasAttribute(attribute)) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " 不允许属性 " + attribute));
      return false;
    }
  }
  return true;
}

bool ValidateAllowedAttributes(const QDomElement &element,
                               const QStringList &allowed,
                               const QString &context,
                               const QString &filePath,
                               QString *errorMessage) {
  const QDomNamedNodeMap attributes = element.attributes();
  for (int index = 0; index < attributes.count(); ++index) {
    const QString name = attributes.item(index).nodeName();
    if (!allowed.contains(name)) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " 不允许属性 " + name));
      return false;
    }
  }
  return true;
}

bool ParseFieldList(const QDomElement &parentElement,
                    const QString &context,
                    const QString &filePath, bool allowBlob,
                    bool allowDataItemGroups,
                    QList<CsgUpstreamFieldDef> *fields,
                    QString *errorMessage);

bool ParseFieldElement(const QDomElement &fieldElement,
                       const QString &context,
                       const QString &filePath, bool allowBlob,
                       bool allowDataItemGroups,
                       CsgUpstreamFieldDef *fieldDefinition,
                       QString *errorMessage) {
  if (!ValidateNoAssociationAttributes(
          fieldElement, context + " / " + fieldElement.tagName(),
          filePath, errorMessage)) {
    return false;
  }
  if (!ValidateAllowedAttributes(
          fieldElement,
          {"id", "name", "type", "length", "required",
           "byteOrder", "description", "format", "unit",
           "display", "countRef", "countRefFF", "lengthRef", "selectorRef",
           "sign", "productRef", "productMaxExclusive",
           "pointCountRef", "diCountRef", "fill"},
          context + " / " + fieldElement.tagName(), filePath,
          errorMessage)) {
    return false;
  }
  fieldDefinition->id = fieldElement.attribute("id").trimmed();
  fieldDefinition->name = fieldElement.attribute("name").trimmed();
  fieldDefinition->description =
      fieldElement.attribute("description");
  fieldDefinition->format = fieldElement.attribute("format");
  fieldDefinition->unit = fieldElement.attribute("unit");
  fieldDefinition->display =
      fieldElement.attribute("display").trimmed();
  fieldDefinition->countRef =
      fieldElement.attribute("countRef").trimmed();
  const QString countRefFfText =
      fieldElement.attribute("countRefFF").trimmed();
  if (!countRefFfText.isEmpty()) {
    bool countRefFfOk = false;
    const int countRefFfValue =
        countRefFfText.toInt(&countRefFfOk, 10);
    if (fieldDefinition->countRef.isEmpty() || !countRefFfOk ||
        countRefFfValue <= 0 ||
        !Matches(countRefFfText, "^[1-9][0-9]*$")) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / " + fieldElement.tagName() +
                             " 的 countRefFF 必须与 countRef 同时配置且为正整数"));
      return false;
    }
    fieldDefinition->countRefFfValue = countRefFfValue;
  }
  fieldDefinition->lengthRef =
      fieldElement.attribute("lengthRef").trimmed();
  fieldDefinition->selectorRef =
      fieldElement.attribute("selectorRef").trimmed();
  fieldDefinition->signMode =
      fieldElement.attribute("sign").trimmed();
  fieldDefinition->productRef =
      fieldElement.attribute("productRef").trimmed();
  fieldDefinition->pointCountRef =
      fieldElement.attribute("pointCountRef").trimmed();
  fieldDefinition->diCountRef =
      fieldElement.attribute("diCountRef").trimmed();
  const QString fieldContext =
      context + " / " + fieldElement.tagName() + " " +
      fieldDefinition->name;
  if (fieldDefinition->name.isEmpty()) {
    SetError(errorMessage,
             FileError(filePath, fieldContext + " 的 name 不能为空"));
    return false;
  }

  const QString fieldType = fieldElement.attribute("type");
  fieldDefinition->sourceType = fieldType;
  const QMap<QString, int> timeTypeLengths = {
      {"YYMMDD", 3},       {"YYMMDDhhmm", 5},
      {"YYYYMMDDhhmm", 6}, {"YYMMDDhhmmss", 6},
      {"MMDDhhmm", 4},
      {"DDhhmmss", 4},     {"hhmm", 2},
      {"hhmmss", 3},
  };
  const bool isNamedTimeType = timeTypeLengths.contains(fieldType);
  if (fieldType == "BCD") {
    fieldDefinition->type = CsgUpstreamFieldType::Bcd;
  } else if (fieldType == "BCD_DATETIME" || isNamedTimeType) {
    fieldDefinition->type = CsgUpstreamFieldType::BcdDateTime;
  } else if (fieldType == "enum") {
    fieldDefinition->type = CsgUpstreamFieldType::Enum;
  } else if (fieldType == "BIN") {
    fieldDefinition->type = CsgUpstreamFieldType::Bin;
  } else if (fieldType == "ASCII") {
    fieldDefinition->type = CsgUpstreamFieldType::Ascii;
  } else if (fieldType == "BS") {
    fieldDefinition->type = CsgUpstreamFieldType::BitString;
  } else if (fieldType == "Struct") {
    fieldDefinition->type = CsgUpstreamFieldType::Struct;
  } else if (fieldType == "Choice") {
    fieldDefinition->type = CsgUpstreamFieldType::Choice;
  } else if (fieldType == "Blob" && allowBlob) {
    fieldDefinition->type = CsgUpstreamFieldType::Blob;
  } else if (fieldType == "DataItemGroups" &&
             allowDataItemGroups) {
    fieldDefinition->type = CsgUpstreamFieldType::DataItemGroups;
  } else if (fieldType == "ReferencedDataItems") {
    fieldDefinition->type =
        CsgUpstreamFieldType::ReferencedDataItems;
  } else if (fieldType == "DataItemGroups") {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 DataItemGroups 仅允许用于 Service"));
    return false;
  } else {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 Field type 不受支持: " +
                           fieldType));
    return false;
  }

  const QString lengthText = fieldElement.attribute("length");
  bool lengthOk = false;
  const int fieldLength = lengthText.toInt(&lengthOk, 10);
  const bool isComposite =
      fieldDefinition->type == CsgUpstreamFieldType::Struct ||
      fieldDefinition->type == CsgUpstreamFieldType::Choice;
  if (!lengthText.isEmpty() &&
      !fieldDefinition->lengthRef.isEmpty()) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 length 与 lengthRef 互斥"));
    return false;
  }
  if (fieldDefinition->type == CsgUpstreamFieldType::DataItemGroups ||
      fieldDefinition->type ==
          CsgUpstreamFieldType::ReferencedDataItems ||
      isComposite) {
    if (!lengthText.isEmpty() ||
        !fieldDefinition->lengthRef.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext + " 的 " + fieldType +
                             " 不允许 length/lengthRef"));
      return false;
    }
    fieldDefinition->length = 0;
  } else if (fieldDefinition->type == CsgUpstreamFieldType::Blob) {
    if (fieldDefinition->lengthRef.isEmpty() &&
        (!lengthOk || (fieldLength != -1 && fieldLength <= 0))) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 Blob 必须配置正整数 length、length=-1 或 lengthRef"));
      return false;
    }
    fieldDefinition->length = fieldDefinition->lengthRef.isEmpty()
                                  ? fieldLength
                                  : 0;
  } else if (fieldDefinition->lengthRef.isEmpty() &&
             (!Matches(lengthText, "^[1-9][0-9]*$") || !lengthOk ||
              fieldLength <= 0)) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 length 必须为正整数"));
    return false;
  } else {
    fieldDefinition->length =
        fieldDefinition->lengthRef.isEmpty() ? fieldLength : 0;
  }

  if (isNamedTimeType &&
      fieldDefinition->length != timeTypeLengths.value(fieldType)) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 " + fieldType +
                           " length 必须为 " +
                           QString::number(
                               timeTypeLengths.value(fieldType))));
    return false;
  }

  const QString required =
      fieldElement.attribute("required", "true");
  if (required != "true" && required != "false") {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 required 只能是 true 或 false"));
    return false;
  }
  fieldDefinition->required = required == "true";

  const QString byteOrder = fieldElement.attribute("byteOrder");
  if (!byteOrder.isEmpty() && byteOrder != "little") {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 byteOrder 只能为空或 little"));
    return false;
  }
  fieldDefinition->littleEndian = byteOrder == "little";

  if (isNamedTimeType && !fieldDefinition->format.isEmpty()) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 " + fieldType +
                           " 不需要配置 format"));
    return false;
  }

  if (fieldType == "BCD_DATETIME" &&
      fieldDefinition->length != 5 && fieldDefinition->length != 6) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 BCD_DATETIME length 必须为 5 或 6"));
    return false;
  }
  if (fieldType == "BCD_DATETIME" &&
      !fieldDefinition->format.isEmpty()) {
    const QString expectedFormat =
        fieldDefinition->length == 5 ? "YYMMDDhhmm"
                                     : "YYMMDDhhmmss";
    if (fieldDefinition->format != expectedFormat) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext + " 的 format 应为 " +
                             expectedFormat));
      return false;
    }
  } else if (fieldDefinition->type == CsgUpstreamFieldType::Bcd &&
             !fieldDefinition->format.isEmpty()) {
    if (!Matches(fieldDefinition->format, "^[N.]+$") ||
        fieldDefinition->length <= 0 ||
        fieldDefinition->format.count('N') !=
            fieldDefinition->length * 2) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 BCD format 中 N 的数量必须等于字段数字位数"));
      return false;
    }
  } else if (fieldDefinition->type != CsgUpstreamFieldType::Bcd &&
             fieldDefinition->type !=
                 CsgUpstreamFieldType::BcdDateTime &&
             !fieldDefinition->format.isEmpty()) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 format 仅适用于 BCD 或 BCD_DATETIME"));
    return false;
  }

  if (!fieldDefinition->unit.isEmpty() &&
      fieldDefinition->type != CsgUpstreamFieldType::Bcd) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 unit 仅适用于 BCD"));
    return false;
  }

  if (!fieldDefinition->display.isEmpty()) {
    const bool isDaDisplay = fieldDefinition->display == "DA";
    const bool isDiDisplay = fieldDefinition->display == "DI";
    const bool isIpv4Display = fieldDefinition->display == "IPv4";
    const bool isFileNatureDisplay =
        fieldDefinition->display == "FileNature";
    const bool isSingleByteBinDisplay =
        fieldDefinition->display == "ValidityFlag" ||
        fieldDefinition->display == "PeriodUnit" ||
        fieldDefinition->display == "DataStructureMode" ||
        fieldDefinition->display == "ConfirmationResult" ||
        fieldDefinition->display == "SecurityModuleFlag" ||
        fieldDefinition->display == "ArithmeticSign" ||
        fieldDefinition->display == "PortFunction" ||
        fieldDefinition->display == "BaudRate300" ||
        fieldDefinition->display == "Parity" ||
        fieldDefinition->display == "DataBits" ||
        fieldDefinition->display == "StopBits" ||
        fieldDefinition->display == "AppControlState" ||
        fieldDefinition->display == "AppRunState" ||
        fieldDefinition->display == "AlarmState" ||
        fieldDefinition->display == "SecurityEventType";
    if (!isDaDisplay && !isDiDisplay && !isIpv4Display &&
        !isFileNatureDisplay && !isSingleByteBinDisplay) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 display 取值不受支持"));
      return false;
    }
    const int expectedLength = isFileNatureDisplay
                                   ? 2
                                   : isDaDisplay
                                         ? 2
                                         : (isDiDisplay || isIpv4Display)
                                               ? 4
                                               : 1;
    if (fieldDefinition->type != CsgUpstreamFieldType::Bin ||
        fieldDefinition->length != expectedLength) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext + " 的 display=" +
                             fieldDefinition->display +
                             " 仅适用于长度为 " +
                             QString::number(expectedLength) +
                             " 的 BIN Field"));
      return false;
    }
    if (isDiDisplay && !fieldDefinition->littleEndian) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 display=DI 必须配置 byteOrder=little"));
      return false;
    }
  }

  if (!fieldDefinition->signMode.isEmpty()) {
    const bool validBinSign =
        fieldDefinition->type == CsgUpstreamFieldType::Bin &&
        fieldDefinition->signMode == "msb";
    const bool validBcdSign =
        fieldDefinition->type == CsgUpstreamFieldType::Bcd &&
        fieldDefinition->signMode == "leading-bit";
    if (!validBinSign && !validBcdSign) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 sign 仅支持 BIN/msb 或 BCD/leading-bit"));
      return false;
    }
  }

  const QString productMaximumText =
      fieldElement.attribute("productMaxExclusive").trimmed();
  if (fieldDefinition->productRef.isEmpty() !=
      productMaximumText.isEmpty()) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext +
                           " 的 productRef 与 productMaxExclusive 必须同时配置"));
    return false;
  }
  if (!fieldDefinition->productRef.isEmpty()) {
    bool maximumOk = false;
    const quint64 maximum =
        productMaximumText.toULongLong(&maximumOk, 10);
    if (fieldDefinition->type != CsgUpstreamFieldType::Bin) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的乘积约束仅适用于 BIN Field"));
      return false;
    }
    if (!Matches(productMaximumText, "^[1-9][0-9]*$") ||
        !maximumOk) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 productMaxExclusive 必须为正整数"));
      return false;
    }
    fieldDefinition->productMaxExclusive = maximum;
  }

  const QString fillText = fieldElement.attribute("fill");
  if (!fillText.isEmpty()) {
    bool fillOk = false;
    const uint fill = fillText.toUInt(&fillOk, 16);
    if (fieldDefinition->type != CsgUpstreamFieldType::Bcd) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext + " 的 fill 仅适用于 BCD"));
      return false;
    }
    if (!Matches(fillText, "^[0-9A-Fa-f]{2}$") || !fillOk) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 fill 必须是两位十六进制"));
      return false;
    }
    fieldDefinition->fillByte = static_cast<int>(fill);
  }

  if ((fieldDefinition->type == CsgUpstreamFieldType::Enum ||
       fieldDefinition->type == CsgUpstreamFieldType::Bin) &&
      fieldDefinition->length > static_cast<int>(sizeof(quint64))) {
    SetError(errorMessage,
             FileError(filePath,
                       fieldContext + " 的 " + fieldType +
                           " Field length 不能超过 8"));
    return false;
  }

  if (fieldDefinition->type == CsgUpstreamFieldType::Enum) {
    const quint64 maximum =
        fieldDefinition->length == static_cast<int>(sizeof(quint64))
            ? std::numeric_limits<quint64>::max()
            : (quint64{1} << (fieldDefinition->length * 8)) - 1;
    bool hasEnumValue = false;
    for (QDomElement enumElement = fieldElement.firstChildElement();
         !enumElement.isNull();
         enumElement = enumElement.nextSiblingElement()) {
      if (enumElement.tagName() != "enum_value") {
        SetError(errorMessage,
                 FileError(filePath,
                           "<Field> 只能包含 <enum_value>"));
        return false;
      }
      hasEnumValue = true;
      const QString valueText = enumElement.attribute("value");
      bool valueOk = false;
      const quint64 enumValue = valueText.toULongLong(&valueOk, 10);
      if (!Matches(valueText, "^[0-9]+$") || !valueOk) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext +
                               " 的 enum_value value 必须为非负整数"));
        return false;
      }
      if (enumValue > maximum) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext + " 的 enum_value " +
                               valueText + " 超出字段长度范围"));
        return false;
      }
      if (fieldDefinition->enumValues.contains(enumValue)) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext + " 包含重复 enum_value: " +
                               valueText));
        return false;
      }
      fieldDefinition->enumValues.insert(
          enumValue, enumElement.attribute("name"));
    }
    if (!hasEnumValue) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 enum Field 必须包含 enum_value"));
      return false;
    }
  } else if (fieldDefinition->type ==
             CsgUpstreamFieldType::BitString) {
    QMap<int, QString> bitNames;
    for (QDomElement bitElement = fieldElement.firstChildElement();
         !bitElement.isNull();
         bitElement = bitElement.nextSiblingElement()) {
      if (bitElement.tagName() != "Bit") {
        SetError(errorMessage,
                 FileError(filePath,
                           "<Field type=BS> 只能包含 <Bit>"));
        return false;
      }
      if (!ValidateAllowedAttributes(
              bitElement, {"index", "name", "zero", "one"},
              fieldContext + " / Bit", filePath, errorMessage)) {
        return false;
      }
      bool indexOk = false;
      const int index =
          bitElement.attribute("index").toInt(&indexOk, 10);
      if (!indexOk || index < 0 ||
          index >= fieldDefinition->length * 8) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext +
                               " 的 Bit index 超出字段位数"));
        return false;
      }
      if (bitNames.contains(index)) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext + " 包含重复 Bit index: " +
                               QString::number(index)));
        return false;
      }
      CsgUpstreamBitDef bitDefinition;
      bitDefinition.index = index;
      bitDefinition.name = bitElement.attribute("name").trimmed();
      bitDefinition.zeroText = bitElement.attribute("zero");
      bitDefinition.oneText = bitElement.attribute("one");
      if (bitDefinition.name.isEmpty() ||
          bitDefinition.zeroText.isEmpty() ||
          bitDefinition.oneText.isEmpty()) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext +
                               " 的 Bit name/zero/one 不能为空"));
        return false;
      }
      bitNames.insert(index, bitDefinition.name);
      fieldDefinition->bits.append(bitDefinition);
    }
  } else if (fieldDefinition->type ==
             CsgUpstreamFieldType::Struct) {
    if (!ParseFieldList(fieldElement, fieldContext, filePath,
                        allowBlob, allowDataItemGroups,
                        &fieldDefinition->children, errorMessage)) {
      return false;
    }
    if (fieldDefinition->children.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 Struct 必须包含 Field"));
      return false;
    }
  } else if (fieldDefinition->type ==
             CsgUpstreamFieldType::Choice) {
    for (QDomElement caseElement = fieldElement.firstChildElement();
         !caseElement.isNull();
         caseElement = caseElement.nextSiblingElement()) {
      if (caseElement.tagName() != "Case") {
        SetError(errorMessage,
                 FileError(filePath,
                           "<Field type=Choice> 只能包含 <Case>"));
        return false;
      }
      if (!ValidateAllowedAttributes(
              caseElement, {"value"}, fieldContext + " / Case",
              filePath, errorMessage)) {
        return false;
      }
      const QString valueText = caseElement.attribute("value");
      bool valueOk = false;
      const quint64 value =
          valueText.toULongLong(&valueOk, 10);
      if (!valueOk || !Matches(valueText, "^[0-9]+$")) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext +
                               " 的 Case value 必须为非负整数"));
        return false;
      }
      if (fieldDefinition->cases.contains(value)) {
        SetError(errorMessage,
                 FileError(filePath,
                           fieldContext + " 包含重复 Case value: " +
                               valueText));
        return false;
      }
      QList<CsgUpstreamFieldDef> caseFields;
      if (!ParseFieldList(caseElement,
                          fieldContext + " / Case " + valueText,
                          filePath, allowBlob, allowDataItemGroups,
                          &caseFields, errorMessage)) {
        return false;
      }
      fieldDefinition->cases.insert(value, caseFields);
    }
    if (fieldDefinition->cases.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         fieldContext +
                             " 的 Choice 必须包含 Case"));
      return false;
    }
  } else if (!fieldElement.firstChildElement().isNull()) {
    SetError(errorMessage,
             FileError(filePath,
                       "<" + fieldElement.tagName() +
                           "> 不能包含子元素"));
    return false;
  }

  return true;
}

bool ParseFieldList(const QDomElement &parentElement,
                    const QString &context,
                    const QString &filePath, bool allowBlob,
                    bool allowDataItemGroups,
                    QList<CsgUpstreamFieldDef> *fields,
                    QString *errorMessage) {
  bool optionalFieldSeen = false;
  QMap<QString, CsgUpstreamFieldType> fieldIds;
  for (QDomElement fieldElement = parentElement.firstChildElement();
       !fieldElement.isNull();
       fieldElement = fieldElement.nextSiblingElement()) {
    if (fieldElement.tagName() != "Field") {
      SetError(errorMessage,
               FileError(filePath,
                         "<" + parentElement.tagName() +
                             "> 只能包含 <Field>"));
      return false;
    }

    CsgUpstreamFieldDef fieldDefinition;
    if (!ParseFieldElement(fieldElement, context, filePath, allowBlob,
                           allowDataItemGroups, &fieldDefinition,
                           errorMessage)) {
      return false;
    }

    if (!fieldDefinition.id.isEmpty()) {
      if (!Matches(fieldDefinition.id,
                   "^[A-Za-z][A-Za-z0-9_]*$")) {
        SetError(errorMessage,
                 FileError(filePath,
                           context + " / Field " +
                               fieldDefinition.name +
                               " 的 id 格式无效"));
        return false;
      }
      if (fieldIds.contains(fieldDefinition.id)) {
        SetError(errorMessage,
                 FileError(filePath,
                           context + " 包含重复 Field id: " +
                               fieldDefinition.id));
        return false;
      }
    }

    const CsgUpstreamFieldType countReferenceType =
        fieldIds.value(fieldDefinition.countRef,
                       CsgUpstreamFieldType::Blob);
    if (!fieldDefinition.countRef.isEmpty() &&
        countReferenceType != CsgUpstreamFieldType::Bin &&
        countReferenceType != CsgUpstreamFieldType::Bcd) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 countRef 必须引用此前的 BIN 或 BCD Field"));
      return false;
    }

    if (!fieldDefinition.lengthRef.isEmpty() &&
        fieldIds.value(fieldDefinition.lengthRef,
                       CsgUpstreamFieldType::Blob) !=
            CsgUpstreamFieldType::Bin) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 lengthRef 必须引用此前的 BIN Field"));
      return false;
    }

    if (fieldDefinition.type == CsgUpstreamFieldType::Choice) {
      if (fieldIds.value(fieldDefinition.selectorRef,
                         CsgUpstreamFieldType::Blob) !=
          CsgUpstreamFieldType::Bin) {
        SetError(errorMessage,
                 FileError(filePath,
                           context + " / Field " +
                               fieldDefinition.name +
                               " 的 selectorRef 必须引用此前的 BIN Field"));
        return false;
      }
    } else if (!fieldDefinition.selectorRef.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 selectorRef 仅适用于 Choice"));
      return false;
    }

    if (!fieldDefinition.productRef.isEmpty() &&
        fieldIds.value(fieldDefinition.productRef,
                       CsgUpstreamFieldType::Blob) !=
            CsgUpstreamFieldType::Bin) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 productRef 必须引用此前的 BIN Field"));
      return false;
    }

    if (fieldDefinition.type ==
        CsgUpstreamFieldType::DataItemGroups) {
      if (fieldIds.value(fieldDefinition.pointCountRef,
                         CsgUpstreamFieldType::Blob) !=
          CsgUpstreamFieldType::Bin) {
        SetError(errorMessage,
                 FileError(filePath,
                           context + " / Field " +
                               fieldDefinition.name +
                               " 的 pointCountRef 必须引用此前的 BIN Field"));
        return false;
      }
      if (fieldIds.value(fieldDefinition.diCountRef,
                         CsgUpstreamFieldType::Blob) !=
          CsgUpstreamFieldType::Bin) {
        SetError(errorMessage,
                 FileError(filePath,
                           context + " / Field " +
                               fieldDefinition.name +
                               " 的 diCountRef 必须引用此前的 BIN Field"));
        return false;
      }
    } else if (!fieldDefinition.pointCountRef.isEmpty() ||
               !fieldDefinition.diCountRef.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 pointCountRef/diCountRef 仅适用于 DataItemGroups"));
      return false;
    }
    if (fieldDefinition.type ==
            CsgUpstreamFieldType::ReferencedDataItems &&
        fieldDefinition.countRef.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " +
                             fieldDefinition.name +
                             " 的 ReferencedDataItems 必须配置 countRef"));
      return false;
    }
    if (fieldDefinition.required && optionalFieldSeen) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " / Field " + fieldDefinition.name +
                             "：必选字段不能位于可选字段之后"));
      return false;
    }
    optionalFieldSeen =
        optionalFieldSeen || !fieldDefinition.required;
    if (fieldDefinition.type == CsgUpstreamFieldType::Blob &&
        fieldDefinition.length == -1 &&
        !fieldElement.nextSiblingElement().isNull()) {
      SetError(errorMessage,
               FileError(filePath,
                         context + " 的 Blob 必须位于最后"));
      return false;
    }
    if (fieldDefinition.type ==
            CsgUpstreamFieldType::DataItemGroups &&
        !fieldElement.nextSiblingElement().isNull()) {
      SetError(errorMessage,
               FileError(filePath,
                         context +
                             " 的 DataItemGroups 必须位于最后"));
      return false;
    }
    if (fieldDefinition.type ==
            CsgUpstreamFieldType::ReferencedDataItems &&
        !fieldElement.nextSiblingElement().isNull()) {
      SetError(errorMessage,
               FileError(filePath,
                         context +
                             " 的 ReferencedDataItems 必须位于最后"));
      return false;
    }
    if (!fieldDefinition.id.isEmpty()) {
      fieldIds.insert(fieldDefinition.id, fieldDefinition.type);
    }
    fields->append(fieldDefinition);
  }
  return true;
}

} // namespace

CsgUpstreamConfigManager &CsgUpstreamConfigManager::instance() {
  static CsgUpstreamConfigManager manager;
  return manager;
}

bool CsgUpstreamConfigManager::loadXml(const QString &filePath,
                                       QString *errorMessage) {
  QMap<int, CsgUpstreamAfnDef> candidateAfns;
  QMap<QString, CsgUpstreamDiDef> candidateDataItems;
  QString candidateVersion;
  QString candidateDate;
  if (!parseXml(filePath, &candidateAfns, &candidateDataItems,
                &candidateVersion, &candidateDate, errorMessage)) {
    return false;
  }

  m_afns = candidateAfns;
  m_dataItems = candidateDataItems;
  m_version = candidateVersion;
  m_date = candidateDate;
  m_loaded = true;
  return true;
}

bool CsgUpstreamConfigManager::validateXml(
    const QString &filePath, QString *errorMessage) const {
  QMap<int, CsgUpstreamAfnDef> candidateAfns;
  QMap<QString, CsgUpstreamDiDef> candidateDataItems;
  QString candidateVersion;
  QString candidateDate;
  return parseXml(filePath, &candidateAfns, &candidateDataItems,
                  &candidateVersion, &candidateDate, errorMessage);
}

const CsgUpstreamAfnDef *
CsgUpstreamConfigManager::getAfnDef(quint8 afn) const {
  const auto it = m_afns.constFind(afn);
  return it == m_afns.cend() ? nullptr : &it.value();
}

const CsgUpstreamDiDef *
CsgUpstreamConfigManager::getDiDef(const QString &key) const {
  const auto it = m_dataItems.constFind(key.toUpper());
  return it == m_dataItems.cend() ? nullptr : &it.value();
}

QString CsgUpstreamConfigManager::getDiName(const QString &key) const {
  const CsgUpstreamDiDef *definition = getDiDef(key);
  return definition ? definition->name : QString();
}

QString CsgUpstreamConfigManager::getVersion() const {
  return m_version;
}

QString CsgUpstreamConfigManager::getDate() const {
  return m_date;
}

bool CsgUpstreamConfigManager::isLoaded() const {
  return m_loaded;
}

bool CsgUpstreamConfigManager::parseXml(
    const QString &filePath, QMap<int, CsgUpstreamAfnDef> *afns,
    QMap<QString, CsgUpstreamDiDef> *dataItems,
    QString *version, QString *date, QString *errorMessage) const {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    SetError(errorMessage,
             FileError(filePath, "无法打开 XML: " + file.errorString()));
    return false;
  }

  QDomDocument document;
  QString xmlError;
  qsizetype errorLine = 0;
  qsizetype errorColumn = 0;
  bool contentLoaded = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
  const QDomDocument::ParseResult parseResult = document.setContent(&file);
  contentLoaded = static_cast<bool>(parseResult);
  xmlError = parseResult.errorMessage;
  errorLine = parseResult.errorLine;
  errorColumn = parseResult.errorColumn;
#else
  int legacyErrorLine = 0;
  int legacyErrorColumn = 0;
  contentLoaded = document.setContent(
      &file, &xmlError, &legacyErrorLine, &legacyErrorColumn);
  errorLine = legacyErrorLine;
  errorColumn = legacyErrorColumn;
#endif
  if (!contentLoaded) {
    SetError(errorMessage,
             FileError(filePath,
                       QString("XML 语法错误（第 %1 行，第 %2 列）: %3")
                           .arg(errorLine)
                           .arg(errorColumn)
                           .arg(xmlError)));
    return false;
  }

  const QDomElement root = document.documentElement();
  if (root.tagName() != "ProtCsgUpstreamConfig") {
    SetError(errorMessage,
             FileError(filePath,
                       "根节点应为 <ProtCsgUpstreamConfig>"));
    return false;
  }

  const QString parsedVersion = root.attribute("version").trimmed();
  if (parsedVersion.isEmpty()) {
    SetError(errorMessage, FileError(filePath, "version 不能为空"));
    return false;
  }

  const QString parsedDate = root.attribute("date");
  if (!Matches(parsedDate, "^[0-9]{4}-[0-9]{2}-[0-9]{2}$") ||
      !QDate::fromString(parsedDate, Qt::ISODate).isValid()) {
    SetError(errorMessage,
             FileError(filePath, "date 必须是有效的 yyyy-MM-dd 日期"));
    return false;
  }

  QDomElement afnsElement;
  QDomElement disElement;
  int afnsCount = 0;
  int disCount = 0;
  for (QDomElement child = root.firstChildElement();
       !child.isNull(); child = child.nextSiblingElement()) {
    if (child.tagName() == "AFNs") {
      ++afnsCount;
      afnsElement = child;
    } else if (child.tagName() == "DIs") {
      ++disCount;
      disElement = child;
    } else {
      SetError(errorMessage,
               FileError(filePath,
                         "根节点只允许包含 <AFNs> 和 <DIs>"));
      return false;
    }
  }
  if (afnsCount != 1) {
    SetError(errorMessage,
             FileError(filePath, "<AFNs> 必须且只能出现一次"));
    return false;
  }
  if (disCount != 1) {
    SetError(errorMessage,
             FileError(filePath, "<DIs> 必须且只能出现一次"));
    return false;
  }

  QMap<int, CsgUpstreamAfnDef> parsedAfns;
  for (QDomElement afnElement = afnsElement.firstChildElement();
       !afnElement.isNull();
       afnElement = afnElement.nextSiblingElement()) {
    if (afnElement.tagName() != "AFN") {
      SetError(errorMessage,
               FileError(filePath, "<AFNs> 只能包含 <AFN>"));
      return false;
    }
    const QString afnCodeText = afnElement.attribute("code");
    if (!ValidateNoAssociationAttributes(
            afnElement, "AFN " + afnCodeText.toUpper(),
            filePath, errorMessage)) {
      return false;
    }
    bool afnOk = false;
    const uint afnCode = afnCodeText.toUInt(&afnOk, 16);
    if (!Matches(afnCodeText, "^[0-9A-Fa-f]{2}$") || !afnOk) {
      SetError(errorMessage,
               FileError(filePath,
                         "AFN code 必须是两位十六进制: " + afnCodeText));
      return false;
    }
    if (parsedAfns.contains(static_cast<int>(afnCode))) {
      SetError(errorMessage,
               FileError(filePath,
                         "重复 AFN: " + afnCodeText.toUpper()));
      return false;
    }

    CsgUpstreamAfnDef afnDefinition;
    afnDefinition.code = static_cast<quint8>(afnCode);
    afnDefinition.name = afnElement.attribute("name").trimmed();
    if (afnDefinition.name.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         "AFN name 不能为空: " + afnCodeText.toUpper()));
      return false;
    }

    for (QDomElement serviceElement = afnElement.firstChildElement();
         !serviceElement.isNull();
         serviceElement = serviceElement.nextSiblingElement()) {
      if (serviceElement.tagName() != "Service") {
        SetError(errorMessage,
                 FileError(filePath, "<AFN> 只能包含 <Service>"));
        return false;
      }

      CsgUpstreamServiceDef serviceDefinition;
      serviceDefinition.direction =
          serviceElement.attribute("direction");
      const QString serviceContext =
          "AFN " + afnCodeText.toUpper() + " / Service " +
          serviceDefinition.direction;
      if (!ValidateNoAssociationAttributes(
              serviceElement, serviceContext, filePath, errorMessage,
              true)) {
        return false;
      }
      if (!ValidateAllowedAttributes(
              serviceElement,
              {"direction", "type", "layout", "cardinality", "pw",
               "tp"},
              serviceContext, filePath, errorMessage)) {
        return false;
      }
      if (serviceDefinition.direction != "up" &&
          serviceDefinition.direction != "down") {
        SetError(errorMessage,
                 FileError(filePath,
                           serviceContext +
                               " 的 direction 只能是 up 或 down"));
        return false;
      }
      if (afnDefinition.services.contains(
              serviceDefinition.direction)) {
        SetError(errorMessage,
                 FileError(filePath,
                           "重复 Service direction: " +
                               serviceDefinition.direction));
        return false;
      }

      serviceDefinition.type = serviceElement.attribute("type");
      const bool isDataUnits = serviceDefinition.type == "DataUnits";
      if (serviceDefinition.type != "Struct" &&
          serviceDefinition.type != "Choice" && !isDataUnits) {
        SetError(errorMessage,
                 FileError(filePath,
                           serviceContext +
                               " 的 Service type 只能是 Struct、Choice 或 DataUnits"));
        return false;
      }

      const QString passwordText =
          serviceElement.attribute("pw", "none");
      if (!ParseTailPolicy(passwordText,
                           &serviceDefinition.passwordPolicy)) {
        SetError(errorMessage,
                 FileError(filePath,
                           serviceContext +
                               " 的 pw 只能是 none、optional 或 required"));
        return false;
      }
      const QString timeTagText =
          serviceElement.attribute("tp", "none");
      if (timeTagText != "none" && timeTagText != "seq") {
        SetError(errorMessage,
                 FileError(filePath,
                           serviceContext +
                               " 的 tp 只能是 none 或 seq"));
        return false;
      }
      serviceDefinition.timeTagFromSeq = timeTagText == "seq";

      if (isDataUnits) {
        const QString layoutText =
            serviceElement.attribute("layout");
        if (!ParseServiceLayout(layoutText,
                                &serviceDefinition.layout)) {
          SetError(errorMessage,
                   FileError(filePath,
                             serviceContext +
                                 " 的 layout 不受支持: " +
                                 layoutText));
          return false;
        }
        const QString cardinality =
            serviceElement.attribute("cardinality");
        if (cardinality != "one" && cardinality != "many") {
          SetError(errorMessage,
                   FileError(filePath,
                             serviceContext +
                                 " 的 cardinality 只能是 one 或 many"));
          return false;
        }
        serviceDefinition.multipleDataUnits = cardinality == "many";
        if (!serviceElement.firstChildElement().isNull()) {
          SetError(errorMessage,
                   FileError(filePath,
                             serviceContext +
                                 " 的 DataUnits 不能包含子元素"));
          return false;
        }
      } else if (serviceElement.hasAttribute("layout") ||
                 serviceElement.hasAttribute("cardinality")) {
        SetError(errorMessage,
                 FileError(filePath,
                           serviceContext +
                               " 的 layout/cardinality 仅适用于 DataUnits"));
        return false;
      }

      if (serviceDefinition.type == "Struct") {
        serviceDefinition.layout = CsgUpstreamServiceLayout::Struct;
        if (!ParseFieldList(serviceElement, serviceContext, filePath,
                            true, true, &serviceDefinition.fields,
                            errorMessage)) {
          return false;
        }
      } else if (serviceDefinition.type == "Choice") {
        serviceDefinition.layout = CsgUpstreamServiceLayout::Choice;
        int selectorCount = 0;
        for (QDomElement child = serviceElement.firstChildElement();
             !child.isNull(); child = child.nextSiblingElement()) {
          if (child.tagName() == "Selector") {
            ++selectorCount;
            if (!ParseFieldElement(
                    child, serviceContext, filePath, false, false,
                    &serviceDefinition.selector, errorMessage)) {
              return false;
            }
            if (serviceDefinition.selector.type !=
                    CsgUpstreamFieldType::Bin &&
                serviceDefinition.selector.type !=
                    CsgUpstreamFieldType::Enum) {
              SetError(errorMessage,
                       FileError(filePath,
                                 serviceContext +
                                     " 的 Selector type 只能是 BIN 或 enum"));
              return false;
            }
            serviceDefinition.hasSelector = true;
          } else if (child.tagName() != "Case") {
            SetError(errorMessage,
                     FileError(filePath,
                               "<Choice> 只能包含 <Selector> 和 <Case>"));
            return false;
          }
        }
        if (selectorCount != 1) {
          SetError(errorMessage,
                   FileError(filePath,
                             serviceContext +
                                 " 的 Choice 必须包含一个 Selector"));
          return false;
        }

        const int selectorLength = serviceDefinition.selector.length;
        const quint64 selectorMaximum =
            selectorLength == static_cast<int>(sizeof(quint64))
                ? std::numeric_limits<quint64>::max()
                : (quint64{1} << (selectorLength * 8)) - 1;
        int caseCount = 0;
        for (QDomElement caseElement =
                 serviceElement.firstChildElement("Case");
             !caseElement.isNull();
             caseElement = caseElement.nextSiblingElement("Case")) {
          ++caseCount;
          if (!ValidateNoAssociationAttributes(
                  caseElement, serviceContext + " / Case", filePath,
                  errorMessage)) {
            return false;
          }
          const QString valueText = caseElement.attribute("value");
          bool valueOk = false;
          const quint64 caseValue =
              valueText.toULongLong(&valueOk, 10);
          if (!Matches(valueText, "^[0-9]+$") || !valueOk ||
              caseValue > selectorMaximum) {
            SetError(errorMessage,
                     FileError(filePath,
                               serviceContext +
                                   " 的 Case value 无效: " + valueText));
            return false;
          }
          if (serviceDefinition.cases.contains(caseValue)) {
            SetError(errorMessage,
                     FileError(filePath,
                               "重复 Case value: " + valueText));
            return false;
          }

          CsgUpstreamServiceCaseDef caseDefinition;
          caseDefinition.value = caseValue;
          caseDefinition.name =
              caseElement.attribute("name").trimmed();
          if (caseDefinition.name.isEmpty()) {
            SetError(errorMessage,
                     FileError(filePath,
                               serviceContext + " / Case " + valueText +
                                   " 的 name 不能为空"));
            return false;
          }
          if (!ParseFieldList(
                  caseElement,
                  serviceContext + " / Case " + valueText,
                  filePath, true, true, &caseDefinition.fields,
                  errorMessage)) {
            return false;
          }
          serviceDefinition.cases.insert(caseValue, caseDefinition);
        }
        if (caseCount == 0) {
          SetError(errorMessage,
                   FileError(filePath,
                             serviceContext +
                                 " 的 Choice 必须包含至少一个 Case"));
          return false;
        }
      }

      afnDefinition.services.insert(serviceDefinition.direction,
                                    serviceDefinition);
    }
    parsedAfns.insert(static_cast<int>(afnCode), afnDefinition);
  }

  QMap<QString, CsgUpstreamDiDef> parsedDataItems;
  for (QDomElement diElement = disElement.firstChildElement();
       !diElement.isNull();
       diElement = diElement.nextSiblingElement()) {
    if (diElement.tagName() != "DI") {
      SetError(errorMessage,
               FileError(filePath, "<DIs> 只能包含 <DI>"));
      return false;
    }

    const QString diCodeText = diElement.attribute("code");
    const QString normalizedDiCode = diCodeText.toUpper();
    if (!ValidateNoAssociationAttributes(
            diElement, "DI " + normalizedDiCode,
            filePath, errorMessage)) {
      return false;
    }
    const QString diContext = "DI " + normalizedDiCode;
    if (!Matches(normalizedDiCode,
                 "^(?:[0-9A-F]{8}|[0-9A-F]{6}XX)$")) {
      SetError(errorMessage,
               FileError(filePath,
                         "DI code 必须是八位十六进制，或六位十六进制加 XX: " +
                             diCodeText));
      return false;
    }
    if (parsedDataItems.contains(normalizedDiCode)) {
      SetError(errorMessage,
               FileError(filePath,
                         "重复 DI: " + normalizedDiCode));
      return false;
    }
    if (diElement.hasAttribute("min") ||
        diElement.hasAttribute("max")) {
      SetError(errorMessage,
               FileError(filePath,
                         diContext +
                             " 不支持 min/max，任务号范围由业务解析器处理"));
      return false;
    }

    CsgUpstreamDiDef diDefinition;
    diDefinition.code = normalizedDiCode;
    diDefinition.name = diElement.attribute("name").trimmed();
    if (diDefinition.name.isEmpty()) {
      SetError(errorMessage,
               FileError(filePath,
                         "DI name 不能为空: " + normalizedDiCode));
      return false;
    }
    const QString diType = diElement.attribute("type");
    if (diType != "Struct" && diType != "None" &&
        diType != "ServiceDefined") {
      SetError(errorMessage,
               FileError(filePath,
                         diContext +
                             " 的 DI type 只能是 Struct、None 或 ServiceDefined"));
      return false;
    }
    diDefinition.type = diType;

    const QDomElement firstField = diElement.firstChildElement("Field");
    if ((diType == "None" || diType == "ServiceDefined") &&
        !firstField.isNull()) {
      SetError(errorMessage,
               FileError(filePath,
                         diContext + " 的 " + diType +
                             " 类型不能包含 Field"));
      return false;
    }

    if (!ParseFieldList(diElement, diContext, filePath, true, false,
                        &diDefinition.fields, errorMessage)) {
      return false;
    }

    parsedDataItems.insert(diDefinition.code, diDefinition);
  }

  *afns = parsedAfns;
  *dataItems = parsedDataItems;
  *version = parsedVersion;
  *date = parsedDate;
  SetError(errorMessage, QString());
  return true;
}
