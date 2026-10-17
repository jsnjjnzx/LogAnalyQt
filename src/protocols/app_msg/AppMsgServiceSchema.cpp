#include "AppMsgServiceSchema.h"
#include "AppMsgLoader.h"

namespace AppMsg {

QString AppMsgServiceSchema::normalizeKind(const QString &kind) {
  QString normalized = kind.trimmed().toLower();
  if (normalized == "响应" || normalized == "response")
    return "response";
  if (normalized == "事件" || normalized == "event")
    return "event";
  return "request";
}

bool AppMsgServiceSchema::hasMessage(const MessageDef &message) {
  return message.isDefined;
}

const MessageDef *AppMsgServiceSchema::messageForKind(
    const InterfaceDef &iface, const QString &kind) {
  QString normalized = normalizeKind(kind);
  if (normalized == "event")
    return hasMessage(iface.event) ? &iface.event : nullptr;
  if (normalized == "response")
    return hasMessage(iface.response) ? &iface.response : nullptr;
  return hasMessage(iface.request) ? &iface.request : nullptr;
}

QVariantList AppMsgServiceSchema::interfaceGroups() {
  QVariantList result;
  const auto &groups = AppMsgLoader::instance().getGroups();
  for (auto groupIter = groups.cbegin(); groupIter != groups.cend();
       ++groupIter) {
    const InterfaceGroupDef &group = groupIter.value();
    QVariantMap item;
    item["iid"] = static_cast<int>(group.iid);
    item["iidHex"] = QString("0x%1")
                         .arg(group.iid, 4, 16, QChar('0'))
                         .toUpper();
    item["name"] = group.name;
    item["description"] = group.description;
    item["count"] = group.interfaces.size();
    result.append(item);
  }
  return result;
}

QVariantList AppMsgServiceSchema::interfaces(int iid, const QString &kind,
                                             const QString &filterText) {
  QVariantList result;
  const auto &groups = AppMsgLoader::instance().getGroups();
  auto groupIter = groups.find(static_cast<uint16_t>(iid));
  if (groupIter == groups.end())
    return result;

  QString normalizedKind = normalizeKind(kind);
  QString filter = filterText.trimmed().toLower();
  for (auto ifaceIter = groupIter.value().interfaces.cbegin();
       ifaceIter != groupIter.value().interfaces.cend(); ++ifaceIter) {
    const InterfaceDef &iface = ifaceIter.value();
    if (!messageForKind(iface, normalizedKind))
      continue;

    QString iopHex = QString("0x%1")
                         .arg(iface.iop, 4, 16, QChar('0'))
                         .toUpper();
    bool matched = filter.isEmpty() || iface.name.toLower().contains(filter) ||
                   iface.description.toLower().contains(filter) ||
                   iopHex.toLower().contains(filter);
    if (!matched)
      continue;

    QVariantMap item;
    item["iid"] = iid;
    item["iop"] = static_cast<int>(iface.iop);
    item["iopHex"] = iopHex;
    item["name"] = iface.name;
    item["description"] = iface.description;
    item["kind"] = normalizedKind;
    item["isEvent"] = iface.type == "event";
    result.append(item);
  }
  return result;
}

QString AppMsgServiceSchema::typeName(FieldType type) {
  switch (type) {
  case FieldType::Null:
    return "null";
  case FieldType::Unsigned:
    return "unsigned";
  case FieldType::LongUnsigned:
    return "long_unsigned";
  case FieldType::DoubleLongUnsigned:
    return "double_long_unsigned";
  case FieldType::DoubleLong:
    return "double_long";
  case FieldType::Long:
    return "long";
  case FieldType::Integer:
    return "integer";
  case FieldType::Long64:
    return "long64";
  case FieldType::Long64Unsigned:
    return "long64_unsigned";
  case FieldType::Float32:
    return "float32";
  case FieldType::Float64:
    return "float64";
  case FieldType::Bool:
    return "bool";
  case FieldType::BitString:
    return "bit_string";
  case FieldType::OctetString:
    return "octet_string";
  case FieldType::VisibleString:
    return "visible_string";
  case FieldType::Utf8String:
    return "utf8_string";
  case FieldType::DateTime:
    return "date_time";
  case FieldType::DateTimeS:
    return "date_time_s";
  case FieldType::Enum:
    return "enum";
  case FieldType::Sequence:
    return "structure";
  case FieldType::SequenceOf:
    return "array";
  case FieldType::Choice:
    return "choice";
  case FieldType::Unknown:
    return "null";
  }
  return "null";
}

QVariantMap AppMsgServiceSchema::applyFormDefaults(QVariantMap schema) {
  QVariantList normalizedChildren;
  const QVariantList rawChildren = schema.value("children").toList();
  for (const QVariant &child : rawChildren) {
    QVariantMap childSchema = child.toMap();
    normalizedChildren.append(childSchema.isEmpty() ? child
                                                    : applyFormDefaults(childSchema));
  }
  schema["children"] = normalizedChildren;

  if (schema.value("optional").toBool() && !schema.contains("defaultPresent"))
    schema["defaultPresent"] = true;

  const QString schemaType = schema.value("type").toString();
  if (schemaType == "array" && !schema.contains("defaultCount"))
    schema["defaultCount"] = normalizedChildren.isEmpty() ? 0 : 1;
  if (schemaType == "choice" && !schema.contains("defaultChoiceIndex"))
    schema["defaultChoiceIndex"] = normalizedChildren.isEmpty() ? -1 : 0;
  return schema;
}

QVariantMap AppMsgServiceSchema::fieldToSchema(const FieldDef &field) {
  QVariantMap schema;
  schema["name"] = field.name.isEmpty() ? QString("字段") : field.name;
  schema["type"] = typeName(field.type);
  schema["optional"] = field.isOptional;
  schema["fixedSize"] = field.fixedSize;
  schema["format"] = field.format;
  schema["choiceValue"] = field.choiceValue;
  schema["description"] = field.description;

  if (!field.enumMap.isEmpty()) {
    QVariantList enumValues;
    for (auto enumIter = field.enumMap.cbegin(); enumIter != field.enumMap.cend();
         ++enumIter) {
      QVariantMap item;
      item["value"] = enumIter.key();
      item["text"] = QString("%1 - %2").arg(enumIter.key()).arg(enumIter.value());
      enumValues.append(item);
    }
    schema["enumValues"] = enumValues;
  }

  QVariantList children;
  for (const FieldDef &child : field.children)
    children.append(fieldToSchema(child));
  schema["children"] = children;
  return applyFormDefaults(schema);
}

QVariantMap AppMsgServiceSchema::messageToSchema(const MessageDef &message,
                                                 const QString &name) {
  QString format = message.format.trimmed().toLower();
  if (format == "null") {
    QVariantMap schema;
    schema["name"] = name;
    schema["type"] = "null";
    schema["optional"] = message.isOptional;
    schema["children"] = QVariantList{};
    return schema;
  }

  if (message.fields.size() == 1 && format != "sequence" &&
      format != "sequence_of") {
    QVariantMap schema = fieldToSchema(message.fields.first());
    if (schema.value("name").toString() == "字段")
      schema["name"] = name;
    schema["optional"] = message.isOptional;
    return schema;
  }

  QVariantMap schema;
  schema["name"] = name;
  schema["optional"] = message.isOptional;
  schema["type"] = format == "sequence_of" ? QString("array")
                                             : QString("structure");
  QVariantList children;
  for (const FieldDef &field : message.fields)
    children.append(fieldToSchema(field));
  schema["children"] = children;
  return applyFormDefaults(schema);
}

QVariantMap AppMsgServiceSchema::schemaForInterface(int iid, int iop,
                                                    const QString &kind) {
  const InterfaceDef *iface =
      AppMsgLoader::instance().getInterface(static_cast<uint16_t>(iid),
                                            static_cast<uint16_t>(iop));
  if (!iface)
    return {};
  const MessageDef *message = messageForKind(*iface, kind);
  if (!message)
    return {};

  QVariantMap schema = messageToSchema(*message, iface->name);
  schema["iid"] = iid;
  schema["iop"] = iop;
  schema["kind"] = normalizeKind(kind);
  schema["interfaceName"] = iface->name;
  schema["description"] = iface->description;
  return schema;
}

} // namespace AppMsg