#ifndef CSGUPSTREAMPARSER_H
#define CSGUPSTREAMPARSER_H

#include "CsgUpstreamDef.h"
#include "ProtocolDetector.h"

#include <QByteArray>
#include <QList>

class CsgUpstreamParser {
public:
  static int getFrameLength(const QByteArray &data);
  static bool isValidFrame(const QByteArray &data);
  QList<CsgUpstreamField> parse(const QByteArray &data) const;
  static ProtocolSummary getSummary(const QByteArray &data);

private:
  static quint8 calculateChecksum(const QByteArray &data,
                                  int offset, int length);
  static QString decodeDa(quint8 da1, quint8 da2);
};

#endif // CSGUPSTREAMPARSER_H
