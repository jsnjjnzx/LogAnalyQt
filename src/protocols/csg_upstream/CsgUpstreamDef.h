#ifndef CSGUPSTREAMDEF_H
#define CSGUPSTREAMDEF_H

#include <QList>
#include <QMap>
#include <QString>
#include <QtGlobal>

enum class CsgUpstreamFieldType {
  Bcd,
  BcdDateTime,
  Enum,
  Bin,
  Ascii,
  BitString,
  Struct,
  Choice,
  Blob,
  DataItemGroups,
  ReferencedDataItems
};

enum class CsgUpstreamServiceLayout {
  Struct,
  Choice,
  Query,
  Content,
  TimedQuery,
  TimedQueryWithDensity,
  TimedContent,
  Error,
  Opaque
};

enum class CsgUpstreamTailPolicy {
  None,
  Optional,
  Required
};

struct CsgUpstreamBitDef {
  int index = -1;
  QString name;
  QString zeroText;
  QString oneText;
};

struct CsgUpstreamFieldDef {
  QString id;
  QString name;
  QString sourceType;
  CsgUpstreamFieldType type = CsgUpstreamFieldType::Bcd;
  int length = 0;
  bool required = true;
  bool littleEndian = false;
  QString description;
  QString format;
  QString unit;
  QString display;
  QString countRef;
  int countRefFfValue = -1;
  QString lengthRef;
  QString selectorRef;
  QString signMode;
  QString productRef;
  quint64 productMaxExclusive = 0;
  QString pointCountRef;
  QString diCountRef;
  int fillByte = -1;
  QMap<quint64, QString> enumValues;
  QList<CsgUpstreamBitDef> bits;
  QList<CsgUpstreamFieldDef> children;
  QMap<quint64, QList<CsgUpstreamFieldDef>> cases;
};

struct CsgUpstreamServiceCaseDef {
  quint64 value = 0;
  QString name;
  QList<CsgUpstreamFieldDef> fields;
};

struct CsgUpstreamServiceDef {
  QString direction;
  QString type;
  CsgUpstreamServiceLayout layout = CsgUpstreamServiceLayout::Struct;
  bool multipleDataUnits = false;
  CsgUpstreamTailPolicy passwordPolicy = CsgUpstreamTailPolicy::None;
  bool timeTagFromSeq = false;
  bool hasSelector = false;
  CsgUpstreamFieldDef selector;
  QList<CsgUpstreamFieldDef> fields;
  QMap<quint64, CsgUpstreamServiceCaseDef> cases;
};

struct CsgUpstreamDiDef {
  QString code;
  QString name;
  QString type;
  QList<CsgUpstreamFieldDef> fields;
};

struct CsgUpstreamAfnDef {
  quint8 code = 0;
  QString name;
  QMap<QString, CsgUpstreamServiceDef> services;
};

struct CsgUpstreamField {
  QString name;
  QString value;
  QString description;
  int offset = -1;
  int length = 0;
  int indent = 0;
};

#endif // CSGUPSTREAMDEF_H
