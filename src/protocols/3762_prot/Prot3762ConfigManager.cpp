#include "Prot3762ConfigManager.h"
#include <QDebug>
#include <QDomDocument>
#include <QFile>

Prot3762ConfigManager &Prot3762ConfigManager::instance() {
  static Prot3762ConfigManager inst;
  return inst;
}

Prot3762ConfigManager::Prot3762ConfigManager() {}

bool Prot3762ConfigManager::loadXml(const QString &filePath) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Failed to open XML:" << filePath;
    return false;
  }

  QDomDocument doc;
  if (!doc.setContent(&file)) {
    qDebug() << "Failed to parse XML content";
    file.close();
    return false;
  }
  file.close();

  m_afns.clear();

  QDomElement root = doc.documentElement(); // Prot3762Config
  m_version = root.attribute("version");
  m_date = root.attribute("date");

  QDomNode afnNode = root.firstChild();
  int afnCount = 0;
  while (!afnNode.isNull()) {
    QDomElement afnElem = afnNode.toElement();
    if (!afnElem.isNull() && afnElem.tagName() == "AFN") {
      int afnCode = afnElem.attribute("code").toInt(nullptr, 16);
      Prot3762AfnDef afnDef;
      afnDef.name = afnElem.attribute("name");

      QDomNode fnNode = afnElem.firstChild();
      while (!fnNode.isNull()) {
        QDomElement fnElem = fnNode.toElement();
        if (!fnElem.isNull() && fnElem.tagName() == "Fn") {
          // Fn code usually F1, F2... -> 1, 2...
          QString fnCodeStr = fnElem.attribute("code");
          int fnCode = 0;
          if (fnCodeStr.startsWith("F", Qt::CaseInsensitive)) {
            fnCode = fnCodeStr.mid(1).toInt();
          } else {
            fnCode = fnCodeStr.toInt();
          }

          Prot3762FnDef fnDef;
          fnDef.name = fnElem.attribute("name");
          fnDef.type = fnElem.attribute("type");
          fnDef.direction =
              fnElem.attribute("direction"); // 读取方向属性：up/down/空

          // 读取字段定义（如果type="Struct"）
          if (fnDef.type == "Struct") {
            QDomNode fieldNode = fnElem.firstChild();
            while (!fieldNode.isNull()) {
              QDomElement fieldElem = fieldNode.toElement();
              if (!fieldElem.isNull() && fieldElem.tagName() == "Field") {
                Prot3762FieldDef fieldDef;
                fieldDef.name = fieldElem.attribute("name");
                fieldDef.type = fieldElem.attribute("type");
                fieldDef.length = fieldElem.attribute("length").toInt();
                fieldDef.description = fieldElem.attribute("description");
                fieldDef.fieldId = fieldElem.attribute("id"); // 字段ID（可选）
                fieldDef.nestedProtocolRef = fieldElem.attribute(
                    "nestedProtocolRef"); // 嵌套协议引用（可选）
                fieldDef.countRef =
                    fieldElem.attribute("countRef"); // 数量引用（可选）
                fnDef.fields.append(fieldDef);
              }
              fieldNode = fieldNode.nextSibling();
            }
          }

          // 生成存储键：如果有direction，使用"fnCode_direction"，否则使用"fnCode"
          QString fnKey = QString::number(fnCode);
          if (!fnDef.direction.isEmpty()) {
            fnKey += "_" + fnDef.direction;
          }

          afnDef.fns.insert(fnKey, fnDef);
        }
        fnNode = fnNode.nextSibling();
      }
      m_afns.insert(afnCode, afnDef);
      afnCount++;
    }
    afnNode = afnNode.nextSibling();
  }

  Q_UNUSED(afnCount);
  return true;
}

Prot3762AfnDef *Prot3762ConfigManager::getAfnDef(int afn) {
  if (m_afns.contains(afn)) {
    return &m_afns[afn];
  }
  return nullptr;
}

Prot3762FnDef *Prot3762ConfigManager::getFnDef(int afn, int fn,
                                               const QString &direction) {
  if (m_afns.contains(afn)) {
    // 优先查找带方向的定义
    if (!direction.isEmpty()) {
      QString fnKey = QString::number(fn) + "_" + direction;
      if (m_afns[afn].fns.contains(fnKey)) {
        return &m_afns[afn].fns[fnKey];
      }
    }

    // 如果没有找到带方向的，查找通用定义（无方向限制）
    QString fnKey = QString::number(fn);
    if (m_afns[afn].fns.contains(fnKey)) {
      return &m_afns[afn].fns[fnKey];
    }
  }
  return nullptr;
}
