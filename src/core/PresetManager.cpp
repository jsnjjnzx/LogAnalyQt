#include "PresetManager.h"
#include "../common/AppPaths.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

PresetManager::PresetManager(QObject *parent) : QObject(parent) {
  ensureDirectoryExists();
  loadFromFile();
}

QString PresetManager::getFilePath() const {
  // 获取程序运行目录下的 protocol_xml/preset_messages.xml
  return QDir(AppPaths::protocolDirectory())
      .filePath(QStringLiteral("preset_messages.xml"));
}

void PresetManager::ensureDirectoryExists() {
  QDir dir;
  dir.mkpath(AppPaths::protocolDirectory());
}

void PresetManager::bumpVersion() {
  m_date = QDateTime::currentDateTime().toString("yyyy-MM-dd");

  if (m_version.isEmpty() || !m_version.startsWith('v')) {
    m_version = "v1.0.0";
  } else {
    QString verStr = m_version.mid(1);
    QStringList parts = verStr.split('.');
    if (parts.size() == 3) {
      int patch = parts[2].toInt();
      patch++;
      m_version = QString("v%1.%2.%3").arg(parts[0]).arg(parts[1]).arg(patch);
    } else {
      m_version = "v1.0.0";
    }
  }
}

void PresetManager::loadFromFile() {
  m_messages.clear();
  m_version = "v1.0.0";
  m_date = QDateTime::currentDateTime().toString("yyyy-MM-dd");

  QString filePath = getFilePath();
  QFile file(filePath);

  if (!file.exists()) {
    QString oldPath = QDir(AppPaths::presetDirectory())
                          .filePath(QStringLiteral("preset_messages.xml"));
    if (QFile::exists(oldPath)) {
      ensureDirectoryExists();
      if (QFile::copy(oldPath, filePath)) {
        qInfo() << "已将预置报文从" << oldPath << "迁移至" << filePath;
      }
    }
  }

  if (!file.exists()) {
    // 文件仍然不存在，创建一个空的 XML 文件
    saveToFile();
    return;
  }

  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "无法打开预置报文文件:" << filePath;
    return;
  }

  QDomDocument doc;
  QString errorMsg;
  int errorLine, errorColumn;
  if (!doc.setContent(&file, &errorMsg, &errorLine, &errorColumn)) {
    qWarning() << "解析预置报文 XML 失败:" << errorMsg << "行:" << errorLine
               << "列:" << errorColumn;
    file.close();
    return;
  }
  file.close();

  QDomElement root = doc.documentElement();
  if (root.tagName() != "presets") {
    qWarning() << "预置报文 XML 根元素不正确";
    return;
  }

  if (root.hasAttribute("version")) {
    m_version = root.attribute("version");
  }
  if (root.hasAttribute("date")) {
    m_date = root.attribute("date");
  }

  QDomNodeList messageNodes = root.elementsByTagName("message");
  for (int i = 0; i < messageNodes.count(); ++i) {
    QDomElement msgElem = messageNodes.at(i).toElement();
    if (msgElem.isNull()) {
      continue;
    }

    PresetMessage msg;
    msg.protocol = msgElem.attribute("protocol");
    msg.summary = msgElem.attribute("summary");

    // 获取 content 子元素的文本内容
    QDomElement contentElem = msgElem.firstChildElement("content");
    if (!contentElem.isNull()) {
      msg.content = contentElem.text().trimmed();
    }

    if (!msg.protocol.isEmpty() && !msg.summary.isEmpty()) {
      m_messages.append(msg);
    }
  }
}

bool PresetManager::saveToFile() {
  ensureDirectoryExists();
  bumpVersion();

  QDomDocument doc;
  QDomProcessingInstruction xmlDecl = doc.createProcessingInstruction(
      "xml", "version=\"1.0\" encoding=\"UTF-8\"");
  doc.appendChild(xmlDecl);

  QDomElement root = doc.createElement("presets");
  root.setAttribute("version", m_version);
  root.setAttribute("date", m_date);
  doc.appendChild(root);

  for (const PresetMessage &msg : m_messages) {
    QDomElement msgElem = doc.createElement("message");
    msgElem.setAttribute("protocol", msg.protocol);
    msgElem.setAttribute("summary", msg.summary);

    QDomElement contentElem = doc.createElement("content");
    // 去除内容中的空格
    QString cleanContent = msg.content;
    cleanContent.remove(' ');
    QDomText contentText = doc.createTextNode(cleanContent);
    contentElem.appendChild(contentText);
    msgElem.appendChild(contentElem);

    root.appendChild(msgElem);
  }

  QString filePath = getFilePath();
  QFile file(filePath);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    qWarning() << "无法写入预置报文文件:" << filePath;
    return false;
  }

  QTextStream out(&file);
  out.setEncoding(QStringConverter::Utf8);
  doc.save(out, 4); // 4 表示缩排 4 个空格，方便阅读
  file.close();

  qDebug() << "预置报文已保存到:" << filePath;
  return true;
}

QStringList
PresetManager::getMessagesByProtocol(const QString &protocol) const {
  QStringList summaries;
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol) {
      summaries.append(msg.summary);
    }
  }
  return summaries;
}

QString PresetManager::getMessageContent(const QString &protocol,
                                         const QString &summary) const {
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol && msg.summary == summary) {
      return msg.content;
    }
  }
  return QString();
}

bool PresetManager::addMessage(const QString &protocol, const QString &summary,
                               const QString &content) {
  // 检查是否已存在相同的报文
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol && msg.summary == summary) {
      qWarning() << "预置报文已存在:" << protocol << summary;
      return false;
    }
  }

  PresetMessage newMsg;
  newMsg.protocol = protocol;
  newMsg.summary = summary;
  newMsg.content = content;
  m_messages.append(newMsg);

  return saveToFile();
}

bool PresetManager::removeMessage(const QString &protocol,
                                  const QString &summary) {
  for (int i = 0; i < m_messages.size(); ++i) {
    if (m_messages[i].protocol == protocol &&
        m_messages[i].summary == summary) {
      m_messages.removeAt(i);
      return saveToFile();
    }
  }
  return false;
}

bool PresetManager::updateMessage(const QString &protocol,
                                  const QString &oldSummary,
                                  const QString &newSummary,
                                  const QString &newContent) {
  // 如果 newSummary 与 oldSummary 不同，检查新名称是否已存在
  if (oldSummary != newSummary) {
    for (const PresetMessage &msg : m_messages) {
      if (msg.protocol == protocol && msg.summary == newSummary) {
        qWarning() << "预置报文名称已存在:" << protocol << newSummary;
        return false;
      }
    }
  }

  // 查找并更新目标报文
  for (int i = 0; i < m_messages.size(); ++i) {
    if (m_messages[i].protocol == protocol &&
        m_messages[i].summary == oldSummary) {
      m_messages[i].summary = newSummary;
      m_messages[i].content = newContent;
      return saveToFile();
    }
  }
  return false;
}

QList<PresetMessage> PresetManager::getAllMessages() const {
  return m_messages;
}

QList<PresetMessage>
PresetManager::getMessageListByProtocol(const QString &protocol) const {
  QList<PresetMessage> result;
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol) {
      result.append(msg);
    }
  }
  return result;
}
