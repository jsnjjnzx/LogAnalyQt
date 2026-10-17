#include "AppMsgBuilder.h"
#include "AppMsgDataEncoder.h"
#include "AppMsgLoader.h"
#include "AppMsgServiceSchema.h"

namespace AppMsg {

void AppMsgBuilder::appendUInt16LE(QByteArray &bytes, int value) {
  bytes.append(static_cast<char>(value & 0xFF));
  bytes.append(static_cast<char>((value >> 8) & 0xFF));
}

bool AppMsgBuilder::appendAppName(QByteArray &bytes, const QString &name,
                                  const QString &fieldName, QString *err) {
  QByteArray encoded = name.toUtf8();
  if (encoded.size() > 23) {
    if (err)
      *err = QString("%1 长度不能超过 23 字节").arg(fieldName);
    return false;
  }
  for (char ch : encoded) {
    uchar value = static_cast<uchar>(ch);
    if (value < 0x20 || value > 0x7E) {
      if (err)
        *err = QString("%1 只能包含可打印 ASCII 字符").arg(fieldName);
      return false;
    }
  }
  bytes.append(encoded);
  bytes.append('\0');
  return true;
}

QByteArray AppMsgBuilder::buildMessageBody(int iid, int iop,
                                           const QString &kind,
                                           const QVariant &treeValue,
                                           QString *err) {
  const InterfaceDef *iface =
      AppMsgLoader::instance().getInterface(static_cast<uint16_t>(iid),
                                            static_cast<uint16_t>(iop));
  if (!iface) {
    if (err)
      *err = "找不到指定 IID/IOP 的 app 协议定义";
    return {};
  }
  const MessageDef *message = AppMsgServiceSchema::messageForKind(*iface, kind);
  if (!message) {
    if (err)
      *err = "当前命令没有对应的消息体定义";
    return {};
  }

  QVariant actualValue = treeValue;
  QByteArray output;
  if (message->isOptional) {
    QVariantMap optionalValue = treeValue.toMap();
    bool present = optionalValue.contains("present")
                       ? optionalValue.value("present").toBool()
                       : true;
    output.append(static_cast<char>(present ? 1 : 0));
    if (!present)
      return output;
    if (optionalValue.contains("value"))
      actualValue = optionalValue.value("value");
  }

  QString format = message->format.trimmed().toLower();
  if (format == "null" || message->fields.isEmpty())
    return output;

  if (format == "sequence_of") {
    QVariantList values = actualValue.toList();
    output.append(AppMsgDataEncoder::encodeLength(values.size()));
    if (message->fields.isEmpty() && !values.isEmpty()) {
      if (err)
        *err = "sequence_of 缺少 element 定义";
      return {};
    }
    for (const QVariant &value : values) {
      QString childErr;
      output.append(AppMsgDataEncoder::encodeField(message->fields.first(),
                                                   value, &childErr));
      if (!childErr.isEmpty()) {
        if (err)
          *err = childErr;
        return {};
      }
    }
    return output;
  }

  if (message->fields.size() == 1 && format != "sequence")
    return output + AppMsgDataEncoder::encodeField(message->fields.first(),
                                                   actualValue, err);

  QVariantList values = actualValue.toList();
  for (int fieldIndex = 0; fieldIndex < message->fields.size(); ++fieldIndex) {
    QString childErr;
    QVariant value = fieldIndex < values.size() ? values.at(fieldIndex)
                                                : QVariant();
    output.append(AppMsgDataEncoder::encodeField(message->fields.at(fieldIndex),
                                                 value, &childErr));
    if (!childErr.isEmpty()) {
      if (err)
        *err = QString("%1 编码失败：%2")
                   .arg(message->fields.at(fieldIndex).name)
                   .arg(childErr);
      return {};
    }
  }
  return output;
}

QByteArray AppMsgBuilder::buildPayload(int iid, int iop, const QString &kind,
                                       const QVariant &treeValue,
                                       QString *err) {
  return buildMessageBody(iid, iop, kind, treeValue, err);
}

QByteArray AppMsgBuilder::buildMessage(int iid, int iop, const QString &kind,
                                       const QVariant &treeValue,
                                       const QVariantMap &headerOptions,
                                       QString *err) {
  QByteArray payload = buildMessageBody(iid, iop, kind, treeValue, err);
  if (err && !err->isEmpty())
    return {};

  QString normalizedKind = AppMsgServiceSchema::normalizeKind(kind);
  bool defaultPrm = normalizedKind != "response";
  int prm = headerOptions.value("prm", defaultPrm).toBool() ? 1 : 0;
  int priority = headerOptions.value("priority", 0).toInt();
  int index = headerOptions.value("index", 0).toInt();
  int label = headerOptions.value("label", 0).toInt();
  if (priority < 0 || priority > 0x7F || index < 0 || index > 0xFFFF ||
      label < 0 || label > 0xFFFF) {
    if (err)
      *err = "APP 消息头参数范围不合法";
    return {};
  }

  QString source = headerOptions.value("source", "client").toString();
  QString destination = headerOptions.value("destination", "server").toString();
  if (source.isEmpty() && destination.isEmpty()) {
    if (err)
      *err = "Source 和 Destination 至少需要填写一个";
    return {};
  }

  QByteArray output;
  output.append(static_cast<char>((priority << 1) | prm));
  appendUInt16LE(output, index);
  appendUInt16LE(output, label);
  if (!appendAppName(output, source, "Source", err) ||
      !appendAppName(output, destination, "Destination", err))
    return {};
  appendUInt16LE(output, iop);
  appendUInt16LE(output, iid);
  output.append(AppMsgDataEncoder::encodeLength(payload.size()));
  output.append(payload);
  return output;
}

} // namespace AppMsg