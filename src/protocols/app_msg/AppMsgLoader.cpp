#include "AppMsgLoader.h"
#include <QDebug>

namespace AppMsg {

AppMsgLoader &AppMsgLoader::instance() {
  static AppMsgLoader instance;
  return instance;
}

bool AppMsgLoader::load(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Failed to open protocol file:" << filePath;
    return false;
  }

  QDomDocument doc;
  if (!doc.setContent(&file)) {
    qDebug() << "Failed to parse XML content";
    file.close();
    return false;
  }
  file.close();

  m_groups.clear();

  QDomElement root = doc.documentElement();
  m_version = root.attribute("version");
  m_date = root.attribute("date");

  QDomNode node = root.firstChild();
  while (!node.isNull()) {
    QDomElement elem = node.toElement();
    if (!elem.isNull() && elem.tagName() == "interface_group") {
      InterfaceGroupDef group;
      bool ok;
      group.iid = elem.attribute("iid").toUInt(&ok, 0);
      group.name = elem.attribute("name");
      group.description = elem.attribute("description");

      // Check if group already exists to merge
      if (m_groups.contains(group.iid)) {
        group = m_groups[group.iid];
        if (!elem.attribute("name").isEmpty())
          group.name = elem.attribute("name");
        if (!elem.attribute("description").isEmpty())
          group.description = elem.attribute("description");
      }

      QDomNode ifNode = elem.firstChild();
      while (!ifNode.isNull()) {
        QDomElement ifElem = ifNode.toElement();
        if (!ifElem.isNull() && ifElem.tagName() == "interface") {
          InterfaceDef iface;
          iface.iop = ifElem.attribute("iop").toUInt(&ok, 0);
          iface.name = ifElem.attribute("name");
          iface.type = ifElem.attribute("type").trimmed().toLower();

          QDomNode child = ifElem.firstChild();
          while (!child.isNull()) {
            QDomElement childElem = child.toElement();
            if (!childElem.isNull()) {
              if (childElem.tagName() == "description") {
                iface.description = childElem.text();
              } else if (childElem.tagName() == "request") {
                iface.request = parseMessageNode(childElem);
              } else if (childElem.tagName() == "response") {
                iface.response = parseMessageNode(childElem);
              } else if (childElem.tagName() == "event") {
                iface.event = parseMessageNode(childElem);
              }
            }
            child = child.nextSibling();
          }
          group.interfaces.insert(iface.iop, iface);
        }
        ifNode = ifNode.nextSibling();
      }
      m_groups.insert(group.iid, group);
    }
    node = node.nextSibling();
  }
  return true;
}

const QMap<uint16_t, InterfaceGroupDef> &AppMsgLoader::getGroups() const {
  return m_groups;
}

const InterfaceDef *AppMsgLoader::getInterface(uint16_t iid,
                                               uint16_t iop) const {
  if (m_groups.contains(iid)) {
    const auto &group = m_groups[iid];
    if (group.interfaces.contains(iop)) {
      // Use value() or find() to avoid creating a temporary if operator[] is
      // not const-correct for QMap Actually QMap::operator[] is non-const, so
      // we must use value() or find() But value() returns by value (copy), so
      // we can't take address. We should use find().
      auto it = group.interfaces.find(iop);
      if (it != group.interfaces.end()) {
        return &it.value();
      }
    }
  }
  return nullptr;
}

QString AppMsgLoader::getIidName(uint16_t iid) const {
  if (m_groups.contains(iid)) {
    return m_groups[iid].name;
  }
  return QString();
}

QString AppMsgLoader::getIopName(uint16_t iid, uint16_t iop) const {
  if (m_groups.contains(iid)) {
    const auto &group = m_groups[iid];
    if (group.interfaces.contains(iop)) {
      auto it = group.interfaces.find(iop);
      if (it != group.interfaces.end()) {
        return it.value().name;
      }
    }
  }
  return QString();
}

FieldType AppMsgLoader::parseTypeString(const QString &typeStr) {
  QString t = typeStr.toLower();
  if (t == "unsigned")
    return FieldType::Unsigned;
  if (t == "long-unsigned")
    return FieldType::LongUnsigned;
  if (t == "double-long-unsigned")
    return FieldType::DoubleLongUnsigned;
  if (t == "double-long")
    return FieldType::DoubleLong;
  if (t == "long")
    return FieldType::Long;
  if (t == "integer")
    return FieldType::Integer;
  if (t == "long64")
    return FieldType::Long64;
  if (t == "long64-unsigned")
    return FieldType::Long64Unsigned;
  if (t == "float32")
    return FieldType::Float32;
  if (t == "float64")
    return FieldType::Float64;
  if (t == "bool")
    return FieldType::Bool;
  if (t == "bit-string")
    return FieldType::BitString;
  if (t == "octet-string")
    return FieldType::OctetString;
  if (t == "visible-string")
    return FieldType::VisibleString;
  if (t == "utf8-string")
    return FieldType::Utf8String;
  if (t == "date_time")
    return FieldType::DateTime;
  if (t == "date_time_s")
    return FieldType::DateTimeS;
  if (t == "enum")
    return FieldType::Enum;
  if (t == "sequence")
    return FieldType::Sequence;
  if (t == "sequence_of")
    return FieldType::SequenceOf;
  if (t == "choice")
    return FieldType::Choice;
  return FieldType::Unknown;
}

FieldDef AppMsgLoader::parseFieldNode(const QDomElement &elem) {
  FieldDef field;
  field.name = elem.attribute("name");
  field.type = parseTypeString(elem.attribute("type"));
  if (elem.hasAttribute("size"))
    field.fixedSize = elem.attribute("size").toInt();
  if (elem.hasAttribute("optional") && elem.attribute("optional") == "true") {
    field.isOptional = true;
  }
  if (elem.hasAttribute("format")) {
    field.format = elem.attribute("format");
  }

  // 解析 choice 的 value 属性（用于标识分支）
  if (elem.hasAttribute("value")) {
    bool ok;
    field.choiceValue = elem.attribute("value").toInt(&ok);
  }

  // Handle children
  QDomNode child = elem.firstChild();
  while (!child.isNull()) {
    QDomElement childElem = child.toElement();
    if (!childElem.isNull()) {
      if (childElem.tagName() == "field" || childElem.tagName() == "element" ||
          childElem.tagName() == "choice") {
        // choice 子标签也递归解析
        field.children.append(parseFieldNode(childElem));
      } else if (childElem.tagName() == "enum_value") {
        bool ok;
        int val = childElem.attribute("value").toInt(&ok);
        if (ok) {
          field.enumMap.insert(val, childElem.attribute("name"));
        }
      }
    }
    child = child.nextSibling();
  }
  return field;
}

MessageDef AppMsgLoader::parseMessageNode(const QDomElement &elem) {
  MessageDef msg;
  msg.isDefined = true;
  msg.format = elem.attribute("format");
  // 解析消息级别的 optional 属性（用于 response/request/event 整体可选的情况）
  if (elem.hasAttribute("optional") && elem.attribute("optional") == "true") {
    msg.isOptional = true;
  }

  if (elem.hasAttribute("type")) {
    msg.fields.append(parseFieldNode(elem));
    if (msg.format.isEmpty())
      msg.format = elem.attribute("type");
  }

  QDomNode child = elem.firstChild();
  while (!child.isNull()) {
    QDomElement childElem = child.toElement();
    if (!childElem.isNull() &&
        (childElem.tagName() == "field" || childElem.tagName() == "element")) {
      msg.fields.append(parseFieldNode(childElem));
    }
    child = child.nextSibling();
  }
  return msg;
}

} // namespace AppMsg
