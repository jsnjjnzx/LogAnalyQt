#include "PresetManager.h"
#include "../common/AppPaths.h"
#include "../common/AppSettings.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

PresetManager::PresetManager(QObject *parent) : QObject(parent) {
  loadFromFile();
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
  auto settings = appSettings();
  m_version = settings.value("messages/version", "v1.0.0").toString();
  m_date = settings.value("messages/date", "").toString();
  m_messages.clear();
  for (const auto &entry : settings.value("messages/presets").toList()) {
    const auto row = entry.toMap();
    m_messages.append({row.value("protocol").toString(), row.value("summary").toString(), row.value("content").toString()});
  }
}

bool PresetManager::saveToFile() {
  bumpVersion();
  QVariantList entries;
  for (const auto &message : m_messages) { auto content = message.content; content.remove(' '); entries.append(QVariantMap{{"protocol", message.protocol}, {"summary", message.summary}, {"content", content}}); }
  auto settings = appSettings();
  settings.setValue("messages/version", m_version); settings.setValue("messages/date", m_date);
  settings.setValue("messages/presets", entries); settings.sync();
  if (settings.status() != QSettings::NoError) { loadFromFile(); return false; }
  return true;
}

QStringList
PresetManager::getMessagesByProtocol(const QString &protocol) const {
  const_cast<PresetManager *>(this)->loadFromFile();
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
  const_cast<PresetManager *>(this)->loadFromFile();
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol && msg.summary == summary) {
      return msg.content;
    }
  }
  return QString();
}

bool PresetManager::addMessage(const QString &protocol, const QString &summary,
                               const QString &content) {
  loadFromFile();
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
  loadFromFile();
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
  loadFromFile();
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
  const_cast<PresetManager *>(this)->loadFromFile();
  return m_messages;
}

QList<PresetMessage>
PresetManager::getMessageListByProtocol(const QString &protocol) const {
  const_cast<PresetManager *>(this)->loadFromFile();
  QList<PresetMessage> result;
  for (const PresetMessage &msg : m_messages) {
    if (msg.protocol == protocol) {
      result.append(msg);
    }
  }
  return result;
}
