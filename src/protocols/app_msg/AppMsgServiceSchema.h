#pragma once

#include "AppMsgDef.h"
#include <QVariantList>
#include <QVariantMap>

namespace AppMsg {

class AppMsgServiceSchema {
public:
  static QVariantList interfaceGroups();
  static QVariantList interfaces(int iid, const QString &kind,
                                 const QString &filterText = QString());
  static QVariantMap schemaForInterface(int iid, int iop,
                                        const QString &kind);
  static const MessageDef *messageForKind(const InterfaceDef &iface,
                                          const QString &kind);
  static QString normalizeKind(const QString &kind);

private:
  static bool hasMessage(const MessageDef &message);
  static QString typeName(FieldType type);
  static QVariantMap applyFormDefaults(QVariantMap schema);
  static QVariantMap fieldToSchema(const FieldDef &field);
  static QVariantMap messageToSchema(const MessageDef &message,
                                     const QString &name);
};

} // namespace AppMsg