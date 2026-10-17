#pragma once

#include <QByteArray>
#include <QVariantMap>

namespace AppMsg {

class AppMsgBuilder {
public:
  static QByteArray buildPayload(int iid, int iop, const QString &kind,
                                 const QVariant &treeValue,
                                 QString *err = nullptr);
  static QByteArray buildMessage(int iid, int iop, const QString &kind,
                                 const QVariant &treeValue,
                                 const QVariantMap &headerOptions,
                                 QString *err = nullptr);

private:
  static QByteArray buildMessageBody(int iid, int iop, const QString &kind,
                                     const QVariant &treeValue,
                                     QString *err);
  static void appendUInt16LE(QByteArray &bytes, int value);
  static bool appendAppName(QByteArray &bytes, const QString &name,
                            const QString &fieldName, QString *err);
};

} // namespace AppMsg