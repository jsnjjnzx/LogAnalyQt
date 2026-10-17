#ifndef PROT698SERVICEBUILDER_H
#define PROT698SERVICEBUILDER_H

#include <QByteArray>
#include <QString>
#include <QVariantMap>

class Prot698ServiceBuilder {
public:
  static QByteArray buildFromSpec(const QVariantMap &serviceSpec,
                                  QString *err = nullptr);

private:
  static QByteArray buildPiid(const QVariantMap &piid);
  static QByteArray buildPiidAcd(const QVariantMap &piid);
  static QByteArray buildTimeTag(const QVariantMap &timeTag, QString *err);
  static QByteArray bytesFromHex(const QString &hex, QString *err,
                                 int expectedBytes = -1);
  static void appendUInt16(QByteArray &bytes, int value);
};

#endif // PROT698SERVICEBUILDER_H