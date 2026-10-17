#ifndef PROT698DATAENCODER_H
#define PROT698DATAENCODER_H

#include "Prot698Def.h"
#include "Prot698OadManager.h"
#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QJsonValue>

struct Prot698EncodeOptions {
  bool withTag = true;
};

class Prot698DataEncoder {
public:
  static QByteArray encode(const Prot698SubItem &def, const QVariant &value,
                           QString *err = nullptr,
                           const Prot698EncodeOptions &options = {});

  static QJsonValue toExactJson(const QVariant &value);
  static QVariantMap subItemToSchema(const Prot698SubItem &item);
  static bool validateJsonValue(const Prot698SubItem &def,
                                const QJsonValue &value, QVariant *rawValue,
                                QString *err = nullptr,
                                const QString &path = "$",
                                bool aiInput = true);
  static Prot698DataType typeFromName(const QString &typeName);
  static QByteArray encodeLength(int len);
  static QByteArray encodeOAD(const QString &oiHex, int attrId, int index = 0,
                              QString *err = nullptr);
  static QByteArray encodeOMD(const QString &oiHex, int methodId, int mode = 0,
                              QString *err = nullptr);
  static QByteArray encodePIID(int seqNo, bool highPriority = false);
  static QByteArray encodeDateTimeS(const QVariantMap &value,
                                    QString *err = nullptr);
  static QByteArray encodeTI(int unit, int value, QString *err = nullptr);

private:
  static QByteArray encodeValue(Prot698DataType type,
                                const Prot698SubItem &def,
                                const QVariant &value, QString *err);
  static QString normalizeTypeName(const QString &typeName);
  static bool parseHexBytes(const QString &text, QByteArray *bytes,
                            QString *err, int expectedBytes = -1);
};

#endif // PROT698DATAENCODER_H
