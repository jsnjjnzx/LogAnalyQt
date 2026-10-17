#pragma once
#include "ModuleMsgDef.h"
#include <QDomDocument>
#include <QFile>
#include <QMap>
#include <QString>

namespace ModuleMsg {

class ModuleMsgLoader {
public:
  static ModuleMsgLoader &instance();
  bool load(const QString &filePath);
  const QMap<uint16_t, InterfaceGroupDef> &getGroups() const;
  const InterfaceDef *getInterface(uint16_t dta1, uint16_t dt) const;
  QString getVersion() const { return m_version; }
  QString getDate() const { return m_date; }

private:
  ModuleMsgLoader() {}
  QMap<uint16_t, InterfaceGroupDef> m_groups;
  QString m_version;
  QString m_date;

  MessageDef parseMessageNode(const QDomElement &elem);
  FieldDef parseFieldNode(const QDomElement &elem);
};

} // namespace ModuleMsg
