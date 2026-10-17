#ifndef CSGUPSTREAMFIELDPARSER_H
#define CSGUPSTREAMFIELDPARSER_H

#include "CsgUpstreamDef.h"

#include <QByteArray>
#include <QList>

class CsgUpstreamFieldParser {
public:
  static bool parse(const QByteArray &payload,
                    const QList<CsgUpstreamFieldDef> &definitions,
                    int *cursor, int baseOffset,
                    const QString &namePrefix, int indent,
                    QList<CsgUpstreamField> *fields,
                    QString *errorMessage);

private:
  static bool parseDefinitions(
      const QByteArray &payload,
      const QList<CsgUpstreamFieldDef> &definitions,
      int *cursor, int baseOffset, const QString &namePrefix,
      int indent, QMap<QString, quint64> *numericValues,
      QList<CsgUpstreamField> *fields, QString *errorMessage);
  static bool parseDataItemGroups(
      const QByteArray &payload,
      const CsgUpstreamFieldDef &definition,
      quint64 pointCount, quint64 diCount, int *cursor,
      int baseOffset, QList<CsgUpstreamField> *fields,
      QString *errorMessage);
  static bool parseReferencedDataItems(
      const QByteArray &payload,
      const CsgUpstreamFieldDef &definition,
      quint64 itemCount, int *cursor, int baseOffset,
      QList<CsgUpstreamField> *fields,
      QString *errorMessage);
  static QString decodeDa(quint8 da1, quint8 da2);
};

#endif // CSGUPSTREAMFIELDPARSER_H
