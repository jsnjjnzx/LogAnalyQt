#pragma once
#include "AppMsgDef.h"
#include <QDomDocument>
#include <QFile>
#include <QMap>

namespace AppMsg {

class AppMsgLoader {
public:
  static AppMsgLoader &instance();
  bool load(const QString &filePath);
  const QMap<uint16_t, InterfaceGroupDef> &getGroups() const;
  const InterfaceDef *getInterface(uint16_t iid, uint16_t iop) const;

  // 获取 IID（接口组）的中文名称
  QString getIidName(uint16_t iid) const;
  // 获取 IOP（接口）的中文名称
  QString getIopName(uint16_t iid, uint16_t iop) const;

  QString getVersion() const { return m_version; }
  QString getDate() const { return m_date; }

private:
  AppMsgLoader() = default;
  QMap<uint16_t, InterfaceGroupDef> m_groups;
  QString m_version;
  QString m_date;

  FieldType parseTypeString(const QString &typeStr);
  FieldDef parseFieldNode(const QDomElement &elem);
  MessageDef parseMessageNode(const QDomElement &elem);
};

} // namespace AppMsg
