#include "Prot645ConfigManager.h"
#include "../../common/AppPaths.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTextStream>

// 日志辅助函数
static void writeLog(const QString &msg) {
  QString logDir = AppPaths::logDirectory();
  QDir().mkpath(logDir);
  QFile logFile(logDir + "/log.txt");
  if (logFile.open(QIODevice::WriteOnly | QIODevice::Append |
                   QIODevice::Text)) {
    QTextStream out(&logFile);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
        << " " << msg << "\n";
    logFile.close();
  }
}

Prot645ConfigManager &Prot645ConfigManager::instance() {
  static Prot645ConfigManager inst;
  return inst;
}

Prot645ConfigManager::Prot645ConfigManager() {}

bool Prot645ConfigManager::loadXml(const QString &xmlPath) {
  writeLog("=== Start loading 645 protocol XML ===");
  writeLog(QString("XML path: %1").arg(xmlPath));

  QFile file(xmlPath);
  QString absolutePath = QFileInfo(file).absoluteFilePath();
  writeLog(QString("Absolute path: %1").arg(absolutePath));

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qDebug() << "Failed to open 645 XML:" << xmlPath;
    return false;
  }

  QDomDocument doc;
  if (!doc.setContent(&file)) {
    qDebug() << "Failed to parse 645 XML content";
    file.close();
    return false;
  }
  file.close();

  m_dataDefs.clear();

  QDomElement root = doc.documentElement(); // Prot645Config
  m_version = root.attribute("version");
  m_date = root.attribute("date");

  QDomNode groupNode = root.firstChild();

  int count = 0;
  while (!groupNode.isNull()) {
    QDomElement groupElem = groupNode.toElement();
    if (!groupElem.isNull() && groupElem.tagName() == "DI_Group") {

      QDomNode diNode = groupElem.firstChild();
      while (!diNode.isNull()) {
        QDomElement diElem = diNode.toElement();
        if (!diElem.isNull() && diElem.tagName() == "DI") {
          Prot645DataDef def;
          def.code = diElem.attribute("code");
          def.name = diElem.attribute("name");

          // 诊断: 如果是06040001,输出完整XML
          if (def.code == "06040001") {
            writeLog("=== Found 06040001, dumping XML ===");
            QString xmlContent;
            QTextStream xmlStream(&xmlContent);
            diElem.save(xmlStream, 2);
            writeLog(xmlContent);
          }

          // 处理变长标识
          QString lenStr = diElem.attribute("len");
          if (lenStr == "var" || lenStr.isEmpty()) {
            def.length = -1; // -1表示变长
          } else {
            def.length = lenStr.toInt();
          }

          def.format = diElem.attribute("format");
          def.unit = diElem.attribute("unit");
          def.rw = diElem.attribute("rw");
          // 解析sign属性，支持TRUE/true/True
          QString signStr = diElem.attribute("sign").toLower();
          def.sign = (signStr == "true");

          // 解析子字段 (Field元素, 以及Req/Rsp容器)
          auto parseFieldList = [&](const QDomElement &parentElem,
                                    QList<Prot645FieldDef> &targetFields) {
            QDomNode fieldNode = parentElem.firstChild();
            while (!fieldNode.isNull()) {
              QDomElement fieldElem = fieldNode.toElement();
              if (!fieldElem.isNull() && fieldElem.tagName() == "Field") {
                Prot645FieldDef fieldDef;
                fieldDef.name = fieldElem.attribute("name");

                // 处理变长字段
                QString fieldLenStr = fieldElem.attribute("len");
                if (fieldLenStr == "var" || fieldLenStr.isEmpty()) {
                  fieldDef.length = -1;
                } else {
                  fieldDef.length = fieldLenStr.toInt();
                }

                fieldDef.format = fieldElem.attribute("format");
                fieldDef.separator = fieldElem.attribute("separator");
                fieldDef.expected = fieldElem.attribute("expected");

                // 调试: 开始处理Field
                writeLog(QString("Processing Field: %1, hasChildren=%2")
                             .arg(fieldDef.name)
                             .arg(fieldElem.hasChildNodes()));

                // 解析子字段内的SubField元素
                QDomNode subFieldNode = fieldElem.firstChild();
                int subFieldCount = 0;
                while (!subFieldNode.isNull()) {
                  writeLog(QString(
                               "  Child node type: %1, isElement: %2, tagName: %3")
                               .arg(subFieldNode.nodeType())
                               .arg(subFieldNode.isElement())
                               .arg(subFieldNode.isElement()
                                        ? subFieldNode.toElement().tagName()
                                        : "N/A"));

                  if (subFieldNode.isElement()) {
                    QDomElement subFieldElem = subFieldNode.toElement();
                    if (subFieldElem.tagName() == "SubField") {
                      Prot645SubFieldDef subFieldDef;
                      subFieldDef.name = subFieldElem.attribute("name");
                      subFieldDef.length =
                          subFieldElem.attribute("len").toInt();
                      subFieldDef.format = subFieldElem.attribute("format");
                      subFieldDef.unit = subFieldElem.attribute("unit");
                      // 解析sign属性，支持TRUE/true/True
                      QString subSignStr =
                          subFieldElem.attribute("sign").toLower();
                      subFieldDef.sign = (subSignStr == "true");

                      fieldDef.subFields.append(subFieldDef);
                      subFieldCount++;
                      writeLog(QString("    Added SubField: %1")
                                   .arg(subFieldDef.name));
                    }
                  }
                  subFieldNode = subFieldNode.nextSibling();
                }

                writeLog(QString("Field %1 loaded with %2 subfields")
                             .arg(fieldDef.name)
                             .arg(subFieldCount));

                // 调试: 输出SubField数量
                if (!fieldDef.subFields.isEmpty()) {
                  writeLog(QString("Loaded %1 subfields for %2")
                               .arg(fieldDef.subFields.size())
                               .arg(fieldDef.name));
                }

                targetFields.append(fieldDef);
              }
              fieldNode = fieldNode.nextSibling();
            }
          };

          bool hasReqRsp = false;
          QDomNode childNode = diElem.firstChild();
          while (!childNode.isNull()) {
            QDomElement childElem = childNode.toElement();
            if (!childElem.isNull() && childElem.tagName() == "Req") {
              parseFieldList(childElem, def.reqFields);
              hasReqRsp = true;
            } else if (!childElem.isNull() && childElem.tagName() == "Rsp") {
              parseFieldList(childElem, def.rspFields);
              hasReqRsp = true;
            } else if (!childElem.isNull() && childElem.tagName() == "Field") {
              parseFieldList(diElem, def.fields);
              break;
            }
            childNode = childNode.nextSibling();
          }

          if (hasReqRsp) {
            writeLog(QString("Loaded Req/Rsp fields for %1: req=%2, rsp=%3")
                         .arg(def.code)
                         .arg(def.reqFields.size())
                         .arg(def.rspFields.size()));
          }

          // 解析 <Bit> 子节点（用于 BitField 格式，如主动上报状态字）
          QDomNode bitNode = diElem.firstChild();
          while (!bitNode.isNull()) {
            QDomElement bitElem = bitNode.toElement();
            if (!bitElem.isNull() && bitElem.tagName() == "Bit") {
              Prot645BitDef bitDef;
              bitDef.pos  = bitElem.attribute("pos").toInt();
              bitDef.name = bitElem.attribute("name");
              def.bitDefs.append(bitDef);
            }
            bitNode = bitNode.nextSibling();
          }
          if (!def.bitDefs.isEmpty()) {
            writeLog(QString("Loaded %1 bit defs for %2")
                         .arg(def.bitDefs.size())
                         .arg(def.code));
          }

          m_dataDefs.insert(def.code, def);
          count++;
        }
        diNode = diNode.nextSibling();
      }
    }
    groupNode = groupNode.nextSibling();
  }

  Q_UNUSED(count);
  return true;
}

Prot645DataDef *Prot645ConfigManager::getDataDef(const QString &diCode) {
  if (m_dataDefs.contains(diCode)) {
    return &m_dataDefs[diCode];
  }
  return nullptr;
}
