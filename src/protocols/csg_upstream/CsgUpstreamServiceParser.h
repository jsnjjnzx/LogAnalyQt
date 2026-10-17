#ifndef CSGUPSTREAMSERVICEPARSER_H
#define CSGUPSTREAMSERVICEPARSER_H

#include "CsgUpstreamDef.h"

#include <QByteArray>
#include <QList>

class CsgUpstreamServiceParser {
public:
  static bool parse(const QByteArray &applicationData, quint8 afn,
                    quint8 seq,
                    const CsgUpstreamServiceDef &service,
                    int baseOffset,
                    QList<CsgUpstreamField> *fields,
                    QString *errorMessage);
};

#endif // CSGUPSTREAMSERVICEPARSER_H
