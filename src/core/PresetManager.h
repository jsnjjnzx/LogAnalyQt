#ifndef PRESETMANAGER_H
#define PRESETMANAGER_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>


// 预置报文结构体
struct PresetMessage {
  QString protocol; // 协议类型
  QString summary;  // 报文概述
  QString content;  // 报文内容（十六进制字符串）
};

class PresetManager : public QObject {
  Q_OBJECT

public:
  explicit PresetManager(QObject *parent = nullptr);

  // 获取指定协议的所有报文概述列表
  QStringList getMessagesByProtocol(const QString &protocol) const;

  // 获取指定报文的内容
  QString getMessageContent(const QString &protocol,
                            const QString &summary) const;

  // 添加新的预置报文
  bool addMessage(const QString &protocol, const QString &summary,
                  const QString &content);

  // 删除预置报文
  bool removeMessage(const QString &protocol, const QString &summary);

  // 修改预置报文（更新概述和内容）
  bool updateMessage(const QString &protocol, const QString &oldSummary,
                     const QString &newSummary, const QString &newContent);

  // 获取所有预置报文（用于管理界面显示）
  QList<PresetMessage> getAllMessages() const;

  // 获取指定协议的所有报文（用于管理界面显示）
  QList<PresetMessage> getMessageListByProtocol(const QString &protocol) const;

private:
  // 从 XML 文件加载预置报文
  void loadFromFile();

  // 保存预置报文到 XML 文件
  bool saveToFile();

  // 获取 XML 文件路径


  // 确保 preset 目录存在


  // 递增版本号并更新日期
  void bumpVersion();

  QList<PresetMessage> m_messages; // 所有预置报文列表
  QString m_version;               // XML 版本号
  QString m_date;                  // XML 版本日期
};

#endif // PRESETMANAGER_H
