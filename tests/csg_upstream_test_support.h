#ifndef CSGUPSTREAMTESTSUPPORT_H
#define CSGUPSTREAMTESTSUPPORT_H

#include "csg_upstream/CsgUpstreamDef.h"

#include <QByteArray>
#include <QList>
#include <QString>

inline QByteArray BuildCsgApplicationFrame(
    quint8 control, quint8 afn, quint8 seq,
    const QByteArray &applicationData) {
  QByteArray userData;
  userData.append(static_cast<char>(control));
  userData.append(QByteArray::fromHex("25314403041100"));
  userData.append(static_cast<char>(afn));
  userData.append(static_cast<char>(seq));
  userData.append(applicationData);

  const quint16 length = static_cast<quint16>(userData.size());
  QByteArray frame;
  frame.append('\x68');
  frame.append(static_cast<char>(length & 0xFF));
  frame.append(static_cast<char>((length >> 8) & 0xFF));
  frame.append(static_cast<char>(length & 0xFF));
  frame.append(static_cast<char>((length >> 8) & 0xFF));
  frame.append('\x68');
  frame.append(userData);

  quint8 checksum = 0;
  for (char byte : userData) {
    checksum = static_cast<quint8>(
        checksum + static_cast<quint8>(byte));
  }
  frame.append(static_cast<char>(checksum));
  frame.append('\x16');
  return frame;
}

inline QByteArray BuildCsgDataUnit(quint16 da, quint32 di,
                                   const QByteArray &content = {}) {
  QByteArray dataUnit;
  dataUnit.append(static_cast<char>(da & 0xFF));
  dataUnit.append(static_cast<char>((da >> 8) & 0xFF));
  for (int shift = 0; shift <= 24; shift += 8) {
    dataUnit.append(static_cast<char>((di >> shift) & 0xFF));
  }
  dataUnit.append(content);
  return dataUnit;
}

inline int CountCsgFields(const QList<CsgUpstreamField> &fields,
                          const QString &name) {
  int count = 0;
  for (const CsgUpstreamField &field : fields) {
    if (field.name == name) {
      ++count;
    }
  }
  return count;
}

inline QString CsgFieldValue(
    const QList<CsgUpstreamField> &fields, const QString &name,
    int occurrence = 0) {
  int current = 0;
  for (const CsgUpstreamField &field : fields) {
    if (field.name != name) {
      continue;
    }
    if (current == occurrence) {
      return field.value;
    }
    ++current;
  }
  return QString();
}

inline QString CsgFieldDescription(
    const QList<CsgUpstreamField> &fields, const QString &name,
    int occurrence = 0) {
  int current = 0;
  for (const CsgUpstreamField &field : fields) {
    if (field.name != name) {
      continue;
    }
    if (current == occurrence) {
      return field.description;
    }
    ++current;
  }
  return QString();
}

#endif // CSGUPSTREAMTESTSUPPORT_H
