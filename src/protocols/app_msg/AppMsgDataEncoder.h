#pragma once

#include "AppMsgDef.h"
#include <QByteArray>
#include <QVariant>

namespace AppMsg {

class AppMsgDataEncoder {
public:
  static QByteArray encodeField(const FieldDef &def, const QVariant &value,
                                QString *err = nullptr);
  static QByteArray encodeLength(int len);

private:
  static QByteArray encodePrimitive(const FieldDef &def, const QVariant &value,
                                    QString *err);
  static bool parseHexBytes(const QString &text, QByteArray *bytes,
                            QString *err, int expectedBytes = -1);
  static QByteArray encodeDateTime(const QVariantMap &value, bool shortFormat,
                                   QString *err);
};

} // namespace AppMsg