#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>
#include <cstdint>

namespace ModuleMsg {

enum class FieldType {
  Unknown,
  Null,
  Unsigned,           // 1 byte
  LongUnsigned,       // 2 bytes
  DoubleLongUnsigned, // 4 bytes
  DoubleLong,         // 4 bytes (signed)
  Long,               // 2 bytes (signed)
  Integer,            // 1 byte (signed)
  Long64,             // 8 bytes
  Long64Unsigned,     // 8 bytes
  Float32,
  Float64,
  Bool,
  BitString,
  OctetString,   // Byte array
  VisibleString, // String
  Utf8String,
  DateTime,
  DateTimeS,
  Enum,
  Sequence,
  SequenceOf,
  Choice
};

struct FieldDef {
  QString name;
  FieldType type = FieldType::Unknown;
  int fixedSize = -1;
  QString description;
  bool isOptional = false;

  // For Sequence, SequenceOf, Choice
  QList<FieldDef> children;

  // For Enum
  QMap<int, QString> enumMap;
};

struct MessageDef {
  QString format; // SEQUENCE, NULL, etc.
  QList<FieldDef> fields;
};

struct InterfaceDef {
  uint16_t dt;
  QString name;
  QString description;
  MessageDef request;
  MessageDef response;
  MessageDef event;
};

struct InterfaceGroupDef {
  uint16_t dta1;
  QString name;
  QString description;
  QMap<uint16_t, InterfaceDef> interfaces;
};

struct ParsedField {
  QString name;
  QString valueStr;
  QString rawHex;
  int offset = 0;
  int length = 0;
  QString description;
  int level = 0;

  QList<ParsedField> children;
};

struct ParsedPacket {
  bool isValid = false;
  QString errorString;

  // Header Fields
  int prm = 0; // From Control Field
  int fid = 0; // Frame ID (Sequence)

  // APDU Fields
  int apduType = 0;  // 0=Request, 1=Response, 4=Notification?
  uint16_t dt = 0;   // Information Class ID
  uint16_t dta1 = 0; // DTA1
  int msgLength = 0; // APDU Length (Data Length from Header)

  // Payload
  QList<ParsedField> payload;
};

// UI 展示字段结构（与其他协议 Parser 保持一致）
struct ModuleMsgField {
  QString name;        // 字段名称
  QString value;       // 原始十六进制数据
  QString description; // 说明文字
  int offset = -1;     // 字节偏移
  int length = -1;     // 字节长度
  int indent = 0;      // 缩进层级

  ModuleMsgField() = default;
  ModuleMsgField(const QString &n, const QString &v, const QString &d, int o,
                 int l, int i = 0)
      : name(n), value(v), description(d), offset(o), length(l), indent(i) {}
};

} // namespace ModuleMsg
