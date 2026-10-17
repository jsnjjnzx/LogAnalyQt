#ifndef CSGUPSTREAMVALUEDECODER_H
#define CSGUPSTREAMVALUEDECODER_H

#include "CsgUpstreamDef.h"

#include <QByteArray>

struct CsgUpstreamDecodedValue {
  bool ok = false;
  QString text;
  QString error;
};

class CsgUpstreamValueDecoder {
public:
  static CsgUpstreamDecodedValue decode(
      const QByteArray &bytes,
      const CsgUpstreamFieldDef &definition);
};

#endif // CSGUPSTREAMVALUEDECODER_H
