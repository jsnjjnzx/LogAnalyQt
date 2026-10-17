#ifndef PROT645CONFIGMANAGER_H
#define PROT645CONFIGMANAGER_H

#include "Prot645Def.h"
#include <QMap>
#include <QObject>

class Prot645ConfigManager {
public:
  static Prot645ConfigManager &instance();

  bool loadXml(const QString &filePath);

  // 根据数据标识获取定义 (diCode format: "00000000")
  // 如果找不到返回空指针
  // 如果找不到返回空指针
  Prot645DataDef *getDataDef(const QString &diCode);
  QString getVersion() const { return m_version; }
  QString getDate() const { return m_date; }

private:
  Prot645ConfigManager();
  QMap<QString, Prot645DataDef> m_dataDefs;
  QString m_version;
  QString m_date;
};

#endif // PROT645CONFIGMANAGER_H
