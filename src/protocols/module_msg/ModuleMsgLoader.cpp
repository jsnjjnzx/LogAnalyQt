#include "ModuleMsgLoader.h"
#include <QDebug>

namespace ModuleMsg {

ModuleMsgLoader &ModuleMsgLoader::instance() {
  static ModuleMsgLoader instance;
  return instance;
}

static FieldType parseTypeString(const QString &typeStr) {
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

bool ModuleMsgLoader::load(const QString &filePath) {
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
      group.dta1 = elem.attribute("dta1").toUInt(&ok, 0);
      group.name = elem.attribute("name");
      group.description = elem.attribute("description");

      // Check if group already exists to merge
      if (m_groups.contains(group.dta1)) {
        group = m_groups[group.dta1];
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
          iface.dt = ifElem.attribute("dt").toUInt(&ok, 0);
          iface.name = ifElem.attribute("name");

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
          group.interfaces.insert(iface.dt, iface);
        }
        ifNode = ifNode.nextSibling();
      }
      m_groups.insert(group.dta1, group);
    }
    node = node.nextSibling();
  }
  return true;
}

const QMap<uint16_t, InterfaceGroupDef> &ModuleMsgLoader::getGroups() const {
  return m_groups;
}

const InterfaceDef *ModuleMsgLoader::getInterface(uint16_t dta1,
                                                  uint16_t dt) const {
  if (m_groups.contains(dta1)) {
    const auto &group = m_groups[dta1];
    if (group.interfaces.contains(dt)) {
      auto it = group.interfaces.find(dt);
      if (it != group.interfaces.end()) {
        return &it.value();
      }
    }
  }
  return nullptr;
}

FieldDef ModuleMsgLoader::parseFieldNode(const QDomElement &elem) {
  FieldDef field;
  field.name = elem.attribute("name");
  field.type = parseTypeString(elem.attribute("type"));
  if (elem.hasAttribute("size"))
    field.fixedSize = elem.attribute("size").toInt();
  if (elem.hasAttribute("optional") && elem.attribute("optional") == "true") {
    field.isOptional = true;
  }

  // Handle children
  QDomNode child = elem.firstChild();
  while (!child.isNull()) {
    QDomElement childElem = child.toElement();
    if (!childElem.isNull()) {
      if (childElem.tagName() == "field" || childElem.tagName() == "element" ||
          childElem.tagName() == "choice") {
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

MessageDef ModuleMsgLoader::parseMessageNode(const QDomElement &elem) {
  MessageDef msg;
  msg.format = elem.attribute("format");

  QDomNode child = elem.firstChild();
  while (!child.isNull()) {
    QDomElement childElem = child.toElement();
    if (!childElem.isNull() &&
        (childElem.tagName() == "field" || childElem.tagName() == "element" ||
         childElem.tagName() == "choice")) {
      msg.fields.append(parseFieldNode(childElem));
    }
    child = child.nextSibling();
  }
  return msg;
}

} // namespace ModuleMsg
