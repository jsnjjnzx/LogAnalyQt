#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>
#include <cstdint> // Fix for uint8_t/uint16_t

namespace Common {

// 基于 DL/T 698.45 数据类型定义
enum class DataType {
  Null = 0,
  Array = 1,
  Structure = 2,
  Bool = 3,
  BitString = 4,
  DoubleLong = 5,         // int32
  DoubleLongUnsigned = 6, // uint32
  Float32 = 7,            // float
  OctetString = 9,
  VisibleString = 10,
  Utf8String = 12,
  Integer = 15,        // int8
  Long = 16,           // int16
  Unsigned = 17,       // uint8
  LongUnsigned = 18,   // uint16
  Long64 = 20,         // int64
  Long64Unsigned = 21, // uint64
  Enum = 22,
  Float64 = 23, // double
  DateTime = 25,
  Date = 26,
  Time = 27,
  DateTimeS = 28,

  // 698 Extended Types
  OI = 80,         // Object Identifier (OCTET-STRING 2)
  OAD = 81,        // Object Attribute Descriptor (OI + Unsigned8 + Unsigned8)
  ROAD = 82,       // Record Object Attribute Descriptor (OAD + ...)
  OMD = 83,        // Object Method Descriptor (OI + Unsigned8 + Unsigned8)
  TI = 84,         // Time Interval
  TSA = 85,        // Target Server Address
  MAC = 86,        // Message Authentication Code
  RN = 87,         // Random Number
  Region = 88,     // Region
  ScalerUnit = 89, // Scaler and Unit
  RSD = 90,        // Record Select Descriptor
  CSD = 91,        // Column Select Descriptor
  MS = 92,         // Metering Status
  SID = 93,        // Session ID
  SID_MAC = 94,    // Session ID + MAC
  COMDCB = 95,     // Comm Descriptor
  RCSD = 96,       // Record Column Select Descriptor
  VQDS = 97,       // Value Quality Descriptor
  Checksum = 98,   // Checksum

  // Custom/Special types used in other protocols but compatible
  // 698 specific complex types can be handled as Structure or specific logic in
  // 698 parser
  Unknown = -1
};

struct ParsedField {
  QString name;
  QString valueStr; // Display value
  QString rawHex;
  QString description;
  int offset = 0;
  int length = 0;
  QVariant value; // Raw value (int, string, etc.)

  QList<ParsedField> children; // For Sequence/Structure/Array
};

class DataParser {
public:
  /**
   * @brief 解析数据
   * @param data 原始数据
   * @param cursor 当前偏移量 (会被更新)
   * @param type 预期类型 (如果 expectTag=true，此参数会被 Tag 覆盖)
   * @param expectTag 是否需要读取 Tag (698模式为 true, Module/App模式为 false)
   * @param fixedLength 固定长度 (>0时用于字符串类型,跳过长度字节读取)
   * @return 解析结果
   */
  static ParsedField parse(const QByteArray &data, int &cursor, DataType type,
                           bool expectTag = false, int fixedLength = -1);

  /**
   * @brief 解析长度 (AXDR / 698 长度编码)
   * @return 长度值，如果失败返回 -1 或 0
   */
  static int decodeLength(const QByteArray &data, int &cursor);

  /**
   * @brief 获取类型名称
   */
  static QString getDataTypeName(DataType type);

private:
  static ParsedField parsePrimitive(const QByteArray &data, int &cursor,
                                    DataType type, int length = -1);
};

} // namespace Common
