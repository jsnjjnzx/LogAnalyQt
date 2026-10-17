#include "Prot698OadManager.h"
#include <QDebug>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFile>
#include <QVariantMap>

Prot698OadManager &Prot698OadManager::instance() {
  static Prot698OadManager inst;
  return inst;
}

Prot698OadManager::Prot698OadManager() {}

bool Prot698OadManager::loadXml(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  const QByteArray content = file.readAll();
  QDomDocument doc;
  if (!doc.setContent(content))
    return false;
  const QDomElement root = doc.documentElement();
  if (root.tagName() != "OIConfig")
    return false;

  QMap<QString, Prot698ObjDef> objects;
  QVariantList diagnostics;
  QMap<QString, int> objectPositions;
  int itemPosition = 0;
  const auto warning = [&](const QString &code, const QString &oi,
                           const QString &member, int previous, int current,
                           const QString &message) {
    diagnostics.append(QVariantMap{{"code", code}, {"severity", "warning"},
        {"oi", oi}, {"member", member}, {"previousPosition", previous},
        {"position", current}, {"message", message}, {"sourcePath", filePath}});
  };
  for (QDomElement item = root.firstChildElement("Item"); !item.isNull();
       item = item.nextSiblingElement("Item")) {
    ++itemPosition;
    const QString oi = item.attribute("OI").trimmed().toUpper();
    if (!QRegularExpression("^[0-9A-F]{4}$").match(oi).hasMatch())
      return false;
    if (objects.contains(oi))
      warning("duplicate_oi", oi, {}, objectPositions.value(oi), itemPosition,
              "存在重复 OI，采用最后定义（整体覆盖）");
    Prot698ObjDef object;
    object.name = item.attribute("Name");
    QMap<QString, int> memberPositions;
    int nodePosition = 0;
    for (QDomElement node = item.firstChildElement("Node"); !node.isNull();
         node = node.nextSiblingElement("Node")) {
      ++nodePosition;
      bool idOk = false;
      int id = node.attribute("ID").toInt(&idOk);
      const QString method = node.attribute("Method");
      if (!idOk || id < 1 || id > 255 ||
          (method != "attrib" && method != "method"))
        return false;
      const QString key = method + ":" + QString::number(id);
      if (memberPositions.contains(key))
        warning("duplicate_member", oi, key, memberPositions.value(key),
                nodePosition, "存在重复成员，采用最后定义");
      Prot698AttrDef definition;
      definition.name = node.attribute("Name");
      definition.op = node.attribute("OP");
      const QStringList ops = definition.op.split('|');
      bool opOk = !ops.isEmpty();
      for (const QString &op : ops)
        opOk = opOk && (method == "method" ? op == "ACTION"
                                               : op == "GET" || op == "SET");
      if (!opOk)
        warning("unsupported_op", oi, key, 0, nodePosition,
                "无法确认 OP：" + definition.op);
      const QDomElement subItem = node.firstChildElement("SubItem");
      if (!subItem.isNull())
        parseSubItem(subItem, definition.dataDef);
      if (definition.dataDef.type.trimmed().isEmpty())
        warning("missing_type", oi, key, 0, nodePosition, "成员缺少明确数据类型");
      if (method == "attrib")
        object.attributes.insert(id, definition);
      else
        object.methods.insert(id, definition);
      memberPositions.insert(key, nodePosition);
    }
    objects.insert(oi, object);
    objectPositions.insert(oi, itemPosition);
  }
  if (objects.isEmpty())
    return false;
  m_objects = std::move(objects);
  m_diagnostics = std::move(diagnostics);
  m_version = root.attribute("version");
  m_date = root.attribute("date");
  m_sourcePath = filePath.startsWith(":/") ? filePath : QFileInfo(filePath).absoluteFilePath();
  // This revision also freezes service and recursive input contracts.
  m_definitionId = QString::fromLatin1(QCryptographicHash::hash(
      QByteArray("698-framing-contract-v3") + char(0) + content,
      QCryptographicHash::Sha256).toHex());
  return true;
}

void Prot698OadManager::parseSubItem(const QDomElement &elem,
                                     Prot698SubItem &item) {
  item.name = elem.attribute("Name");
  item.type = elem.attribute("Type");
  item.unit = elem.attribute("Unit");
  item.scaler = elem.attribute("Scaler").toInt();
  item.hasScaler = !elem.attribute("Scaler").trimmed().isEmpty();
  item.size = elem.attribute("Size");
  item.typeOption = elem.attribute("TypeOption");
  item.selected = elem.attribute("Selected").compare("true", Qt::CaseInsensitive) == 0;
  item.remark = elem.attribute("Remark");
  item.format = elem.attribute("Format");
  item.defVal = elem.attribute("DefVal");

  if (item.type.compare("enum", Qt::CaseInsensitive) == 0) {
    parseEnumValues(elem.attribute("DefVal"), item.enumValues);
  }

  // Children
  QDomNode childNode = elem.firstChild();
  while (!childNode.isNull()) {
    QDomElement childElem = childNode.toElement();
    if (!childElem.isNull() && childElem.tagName() == "SubItem") {
      Prot698SubItem child;
      parseSubItem(childElem, child);
      item.children.append(child);
    }
    childNode = childNode.nextSibling();
  }
}

void Prot698OadManager::parseEnumValues(const QString &defVal,
                                        QMap<int, QString> &values) {
  // Format: "0.主站授时=0;1.心跳校时=1;..."
  QStringList parts = defVal.split(';', Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    int eqIdx = part.lastIndexOf('=');
    if (eqIdx != -1) {
      QString valStr = part.mid(eqIdx + 1);
      QString desc = part.left(eqIdx);
      // desc might be "0.主站授时". Remove prefix "0." if present?
      // Usually it's "Value.Desc".
      // Let's keep full desc for now or try to clean it.
      // Example: "0.主站授时".
      int dotIdx = desc.indexOf('.');
      if (dotIdx != -1) {
        desc = desc.mid(dotIdx + 1);
      }
      bool ok = false;
      const int value = valStr.toInt(&ok);
      if (ok && value >= 0 && value <= 255)
        values.insert(value, desc);
    }
  }
}

Prot698ObjDef *Prot698OadManager::getObjDef(const QString &oi) {
  if (m_objects.contains(oi)) {
    return &m_objects[oi];
  }
  return nullptr;
}

Prot698AttrDef *Prot698OadManager::getAttrDef(const QString &oi, int attrId) {
  if (m_objects.contains(oi)) {
    if (m_objects[oi].attributes.contains(attrId)) {
      return &m_objects[oi].attributes[attrId];
    }
  }
  return nullptr;
}

Prot698AttrDef *Prot698OadManager::getMethodDef(const QString &oi,
                                                int methodId) {
  if (m_objects.contains(oi)) {
    if (m_objects[oi].methods.contains(methodId)) {
      return &m_objects[oi].methods[methodId];
    }
  }
  return nullptr;
}

Prot698SubItem *Prot698OadManager::resolveIndexedDataDef(Prot698SubItem *root,
                                                         int dataIndex) {
  if (!root || dataIndex == 0) {
    return root;
  }

  if (dataIndex < 0)
    return nullptr;
  Prot698SubItem *current = root;
  if (current->type.compare("array", Qt::CaseInsensitive) == 0) {
    if (current->children.isEmpty()) {
      return nullptr;
    }
    current = &current->children[0];
  }

  if (current->type.compare("structure", Qt::CaseInsensitive) == 0) {
    int childIndex = dataIndex - 1;
    if (childIndex >= 0 && childIndex < current->children.size()) {
      return &current->children[childIndex];
    }
  }

  return nullptr;
}

Prot698SubItem *Prot698OadManager::getAttrDataDef(const QString &oi, int attrId,
                                                  int dataIndex) {
  Prot698AttrDef *definition = getAttrDef(oi, attrId);
  if (!definition) {
    return nullptr;
  }
  return resolveIndexedDataDef(&definition->dataDef, dataIndex);
}

Prot698SubItem *Prot698OadManager::getMethodDataDef(const QString &oi,
                                                    int methodId) {
  Prot698AttrDef *definition = getMethodDef(oi, methodId);
  if (!definition) {
    return nullptr;
  }
  return &definition->dataDef;
}

QVariantList Prot698OadManager::listAllOi(const QString &filterText) const {
  QVariantList result;
  QString filter = filterText.trimmed().toLower();
  const auto searchText = [](const QString &text) {
    static const QRegularExpression separators(QStringLiteral("[\\p{P}\\s]+"));
    return text.normalized(QString::NormalizationForm_KC).toCaseFolded().remove(separators);
  };
  const QString normalizedFilter = searchText(filter);
  const auto matches = [&](const QString &text) {
    return text.toLower().contains(filter) ||
        (!normalizedFilter.isEmpty() && searchText(text).contains(normalizedFilter));
  };

  for (auto objIter = m_objects.cbegin(); objIter != m_objects.cend();
       ++objIter) {
    const QString oi = objIter.key();
    const Prot698ObjDef &obj = objIter.value();
    bool matched = filter.isEmpty() || matches(oi) || matches(obj.name);

    QVariantList attrs;
    for (auto attrIter = obj.attributes.cbegin();
         attrIter != obj.attributes.cend(); ++attrIter) {
      const Prot698AttrDef &attr = attrIter.value();
      if (!matched && matches(attr.name))
        matched = true;
      QVariantMap attrItem;
      attrItem["id"] = attrIter.key();
      attrItem["name"] = attr.name;
      attrItem["op"] = attr.op;
      attrs.append(attrItem);
    }

    QVariantList methods;
    for (auto methodIter = obj.methods.cbegin();
         methodIter != obj.methods.cend(); ++methodIter) {
      const Prot698AttrDef &method = methodIter.value();
      if (!matched && matches(method.name))
        matched = true;
      QVariantMap methodItem;
      methodItem["id"] = methodIter.key();
      methodItem["name"] = method.name;
      methodItem["op"] = method.op;
      methods.append(methodItem);
    }

    if (!matched)
      continue;

    QVariantMap item;
    item["oi"] = oi;
    item["name"] = obj.name;
    item["attrs"] = attrs;
    item["methods"] = methods;
    result.append(item);
  }
  return result;
}
