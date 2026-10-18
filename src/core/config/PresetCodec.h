#pragma once
#include "../../common/JsonSettings.h"
#include <QDomDocument>

namespace PresetCodec {
inline bool decode(const QByteArray &bytes, QJsonObject *result, QString *error) {
  QDomDocument document;
  if (!document.setContent(bytes) || document.documentElement().tagName() != "presets") return ConfigJson::fail(error, QStringLiteral("预置报文 XML 无效"));
  const auto root = document.documentElement();
  for (int i = 0; i < root.attributes().count(); ++i) if (!QStringList{"version", "date"}.contains(root.attributes().item(i).nodeName())) return ConfigJson::fail(error, QStringLiteral("预置报文含未知根属性，原文件保留"));
  QJsonArray entries; QSet<QString> identities;
  for (auto node = root.firstChildElement(); !node.isNull(); node = node.nextSiblingElement()) {
    if (node.tagName() != "message") return ConfigJson::fail(error, QStringLiteral("预置报文含未知元素，原文件保留"));
    for (int i = 0; i < node.attributes().count(); ++i) if (!QStringList{"protocol", "summary"}.contains(node.attributes().item(i).nodeName())) return ConfigJson::fail(error, QStringLiteral("预置报文含未知属性，原文件保留"));
    if (node.elementsByTagName("content").size() != 1 || node.childNodes().count() == 0) return ConfigJson::fail(error, QStringLiteral("预置报文内容结构无效"));
    for (auto child = node.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
      if (child.tagName() != "content" || !child.firstChildElement().isNull()) return ConfigJson::fail(error, QStringLiteral("预置报文含未知内容结构，原文件保留"));
    const auto protocol = node.attribute("protocol"), summary = node.attribute("summary");
    const auto identity = protocol + QChar(0) + summary;
    if (protocol.isEmpty() || summary.isEmpty() || identities.contains(identity)) return ConfigJson::fail(error, QStringLiteral("预置报文名称缺失或重复"));
    identities.insert(identity);
    entries.append(QJsonObject{{"protocol", protocol}, {"summary", summary}, {"content", node.firstChildElement("content").text().trimmed()}});
  }
  *result = {{"version", root.attribute("version", "v1.0.0")}, {"date", root.attribute("date")}, {"presets", entries}};
  return true;
}
inline QByteArray encode(const QJsonObject &messages) {
  QDomDocument document;
  auto root = document.createElement("presets");
  root.setAttribute("version", messages.value("version").toString("v1.0.0"));
  root.setAttribute("date", messages.value("date").toString()); document.appendChild(root);
  for (const auto &entry : messages.value("presets").toArray()) {
    auto message = document.createElement("message"); const auto row = entry.toObject();
    message.setAttribute("protocol", row.value("protocol").toString()); message.setAttribute("summary", row.value("summary").toString());
    auto content = document.createElement("content"); content.appendChild(document.createTextNode(row.value("content").toString()));
    message.appendChild(content); root.appendChild(message);
  }
  return document.toByteArray(2);
}
}
