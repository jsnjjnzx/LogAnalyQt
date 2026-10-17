#ifndef PROT698SERVICESCHEMA_H
#define PROT698SERVICESCHEMA_H

#include <QByteArray>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonObject>

class Prot698ServiceSchema {
public:
  static QVariantList serviceTypes();
  static QVariantMap schemaForService(const QString &serviceCode);
  static QString validateTreeValue(const QString &serviceCode,
                                   const QVariantMap &treeValue);
  static bool validateJsonTreeValue(const QString &serviceCode,
                                    const QJsonObject &treeValue,
                                    QVariantMap *normalizedTree,
                                    QString *err = nullptr,
                                    bool aiInput = true);
  static QVariantMap toServiceSpec(const QString &serviceCode,
                                   const QVariantMap &treeValue,
                                   QString *err = nullptr);

private:
  static QVariantMap node(const QString &key, const QString &name,
                          const QString &kind, bool required = true,
                          bool repeatable = false,
                          const QVariantList &children = {});
  static QString normalizeServiceCode(const QString &serviceCode);
  static QString oadHexFromMap(const QVariantMap &oad, QString *err);
  static QString omdHexFromMap(const QVariantMap &omd, QString *err);
  static QString encodeDataForOad(const QVariantMap &oad,
                                  const QVariant &dataValue, bool isMethod,
                                  QString *err);
  static QString encodeRsdFromMap(const QVariantMap &rsd, QString *err);
  static QString encodeRcsdFromMap(const QVariantMap &rcsd, QString *err);
  static QString encodeAResultNormalFromMap(const QVariantMap &result,
                                            QString *err);
  static QString encodeAResultRecordFromMap(const QVariantMap &result,
                                            QString *err);
  static QString encodeSetResultFromMap(const QVariantMap &result,
                                        QString *err);
  static QString encodeActionResultFromMap(const QVariantMap &result,
                                           QString *err);
  static QVariantList encodeResultList(const QVariantList &items,
                                       QString (*encoder)(const QVariantMap &,
                                                          QString *),
                                       QString *err);
  static QByteArray encodeRsdBytes(const QVariantMap &rsd, QString *err);
  static QByteArray encodeRsdSelector2Body(const QVariantMap &selector,
                                           QString *err);
  static QByteArray encodeMsBytes(const QVariantMap &ms, QString *err);
  static QByteArray encodeCsdBytes(const QVariantMap &csd, QString *err);
  static QByteArray bytesFromHexText(const QString &hex, QString *err);
  static void appendUInt16BE(QByteArray &bytes, int value);
  static void appendUInt16LE(QByteArray &bytes, int value);
};

#endif // PROT698SERVICESCHEMA_H
