#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

namespace AppMsg {

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
  QString format;

  // For Sequence, SequenceOf, Choice
  QList<FieldDef> children;

  // For Enum
  QMap<int, QString> enumMap;

  // For Choice branch value (用于标识 choice 分支)
  int choiceValue = -1;
};

struct MessageDef {
  QString format; // SEQUENCE, NULL, etc.
  QList<FieldDef> fields;
  bool isDefined = false;
  bool isOptional = false; // 整个消息是否可选（需要先读取 optional 标志位）
};

struct InterfaceDef {
  uint16_t iop;
  QString name;
  QString type;
  QString description;
  MessageDef request;
  MessageDef response;
  MessageDef event;
};

struct InterfaceGroupDef {
  uint16_t iid;
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

// 用于 UI 展示的统一字段格式（与 Prot698Field 结构一致）
struct AppMsgField {
  QString name;        // 字段名称
  QString value;       // 原始十六进制数据
  QString description; // 说明文字
  int offset = -1;     // 字节偏移
  int length = -1;     // 字节长度
  int indent = 0;      // 缩进层级

  AppMsgField() = default;
  AppMsgField(const QString &n, const QString &v, const QString &d, int o = -1,
              int l = -1, int i = 0)
      : name(n), value(v), description(d), offset(o), length(l), indent(i) {}
};

struct ParsedPacket {
  bool isValid = false;
  QString errorString;

  // Header Fields
  int prm = 0;
  int priority = 0;
  int index = 0;
  int label = 0;
  QString source;
  QString destination;
  uint16_t iid = 0;
  uint16_t iop = 0;
  int msgLength = 0;

  // Payload
  QList<ParsedField> payload;
};

} // namespace AppMsg
