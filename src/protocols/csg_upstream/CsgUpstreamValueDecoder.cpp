#include "CsgUpstreamValueDecoder.h"

#include "CsgUpstreamConfigManager.h"

#include <QDate>
#include <QStringList>

#include <algorithm>

namespace {

quint8 ByteAt(const QByteArray &data, int offset) {
  return static_cast<quint8>(data.at(offset));
}

QString HexByte(quint8 value) {
  return QString("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString HexBytes(const QByteArray &data) {
  return QString::fromLatin1(data.toHex()).toUpper();
}

QString HexCode(quint32 value) {
  return QString("%1").arg(value, 8, 16, QLatin1Char('0')).toUpper();
}

quint64 ReadUnsigned(const QByteArray &bytes, bool littleEndian) {
  quint64 value = 0;
  if (littleEndian) {
    for (int index = bytes.size() - 1; index >= 0; --index) {
      value = (value << 8) | ByteAt(bytes, index);
    }
  } else {
    for (int index = 0; index < bytes.size(); ++index) {
      value = (value << 8) | ByteAt(bytes, index);
    }
  }
  return value;
}

bool ValidateBcd(const QByteArray &bytes, QString *error) {
  for (char byte : bytes) {
    const quint8 value = static_cast<quint8>(byte);
    if ((value >> 4) > 9 || (value & 0x0F) > 9) {
      *error = "包含非法 BCD 数字: " + HexByte(value);
      return false;
    }
  }
  return true;
}

int BcdValue(const QByteArray &bytes, int index) {
  const quint8 value = ByteAt(bytes, index);
  return static_cast<int>((value >> 4) * 10 + (value & 0x0F));
}

CsgUpstreamDecodedValue Success(const QString &text) {
  return {true, text, QString()};
}

CsgUpstreamDecodedValue Failure(const QString &error) {
  return {false, QString(), error};
}

QString DecodeDa(quint8 da1, quint8 da2) {
  if (da1 == 0 && da2 == 0) {
    return "p0";
  }

  QStringList points;
  for (int bit = 0; bit < 8; ++bit) {
    if ((da1 & (1U << bit)) != 0) {
      const int point = da2 == 0 ? bit + 1 : (da2 - 1) * 8 + bit + 1;
      points.append("p" + QString::number(point));
    }
  }
  if (points.isEmpty()) {
    return QString("第 %1 组，无有效信息点").arg(da2);
  }
  return QString("第 %1 组 %2").arg(da2).arg(points.join(", "));
}

struct DiLookup {
  QString key;
  QString nameSuffix;
};

DiLookup ResolveDiLookup(quint32 di) {
  const quint8 itemNumber = static_cast<quint8>(di & 0xFF);
  if ((di & 0xFFFFFF00U) == 0xE0000300U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00003XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0000400U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00004XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0001500U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00015XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0001600U &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {"E00016XX", QString::number(itemNumber)};
  }
  if ((di & 0xFFFFFF00U) == 0xE0000200U) {
    if (itemNumber <= 0x1E) {
      return {"E0000200",
              QString("（第%1路485）").arg(itemNumber + 1)};
    }
    if (itemNumber == 0x1F) {
      return {"E0000200", "（载波通道）"};
    }
    if (itemNumber == 0x20) {
      return {"E0000200", "（无线通道）"};
    }
    if (itemNumber == 0x22) {
      return {"E0000200", "（光纤通道）"};
    }
    if (itemNumber == 0x23) {
      return {"E0000200", "（混合通道）"};
    }
    if (itemNumber >= 0x24 && itemNumber <= 0x29) {
      return {"E0000200",
              QString("（备用通道%1）").arg(itemNumber - 0x23)};
    }
    if (itemNumber >= 0x30 && itemNumber <= 0x50) {
      return {"E0000230", QString::number(itemNumber - 0x2F)};
    }
  }
  const quint32 series = di & 0xFFFFFF00U;
  if ((series == 0xE0000500U || series == 0xE0000600U ||
       series == 0xE0000700U || series == 0xE0000800U) &&
      itemNumber >= 0x01 && itemNumber <= 0xFD) {
    return {HexCode(series).left(6) + "XX",
            QString::number(itemNumber)};
  }
  if (series == 0xE0000A00U && itemNumber <= 0x1E) {
    return {"E0000AXX",
            QString("（端口号 %1H）")
                .arg(itemNumber, 2, 16, QLatin1Char('0'))
                .toUpper()};
  }
  if (series == 0xE0000C00U) {
    const quint8 rangeStart = itemNumber <= 0x0E
                                  ? 0x00
                                  : itemNumber >= 0x10 && itemNumber <= 0x1E
                                        ? 0x10
                                        : itemNumber >= 0x20 && itemNumber <= 0x2E
                                              ? 0x20
                                              : itemNumber >= 0x30 && itemNumber <= 0x3E
                                                    ? 0x30
                                                    : 0xFF;
    if (rangeStart != 0xFF) {
      return {HexCode(series | rangeStart),
              QString("（开关编号 %1H）")
                  .arg(itemNumber - rangeStart, 2, 16,
                       QLatin1Char('0'))
                  .toUpper()};
    }
  }
  if (series == 0xE0000D00U) {
    const quint8 rangeStart = itemNumber >= 0x01 && itemNumber <= 0x08
                                  ? 0x01
                                  : itemNumber >= 0x11 && itemNumber <= 0x18
                                        ? 0x11
                                        : itemNumber >= 0x21 && itemNumber <= 0x28
                                              ? 0x21
                                              : itemNumber >= 0x31 && itemNumber <= 0x38
                                                    ? 0x31
                                                    : 0xFF;
    if (rangeStart != 0xFF) {
      return {HexCode(series | rangeStart),
              QString("（时段 %1）").arg(itemNumber - rangeStart + 1)};
    }
  }
  if (series == 0xE0000E00U &&
      itemNumber >= 0x21 && itemNumber <= 0x2C) {
    return {"E0000E21",
            QString("（轮次 %1）").arg(itemNumber - 0x20)};
  }
  return {HexCode(di), QString()};
}

QString ResolveDiName(const CsgUpstreamConfigManager &manager,
                      const DiLookup &lookup) {
  QString name = manager.getDiName(lookup.key);
  if (!name.isEmpty()) {
    name += lookup.nameSuffix;
  }
  return name;
}

CsgUpstreamDecodedValue DecodeBcd(
    const QByteArray &bytes,
    const CsgUpstreamFieldDef &definition) {
  QByteArray magnitudeBytes = bytes;
  bool negative = false;
  if (definition.signMode == "leading-bit" &&
      !magnitudeBytes.isEmpty()) {
    negative = (ByteAt(magnitudeBytes, 0) & 0x80U) != 0;
    magnitudeBytes[0] = static_cast<char>(
        ByteAt(magnitudeBytes, 0) & 0x7FU);
  }

  QString error;
  if (!ValidateBcd(magnitudeBytes, &error)) {
    return Failure(error);
  }
  if (definition.littleEndian) {
    std::reverse(magnitudeBytes.begin(), magnitudeBytes.end());
  }
  QString value = HexBytes(magnitudeBytes);
  if (!definition.format.isEmpty()) {
    QString formatted;
    int digit = 0;
    for (QChar character : definition.format) {
      if (character == 'N') {
        formatted += value.at(digit);
        ++digit;
      } else {
        formatted += character;
      }
    }
    value = formatted;
  }
  if (negative) {
    value.prepend('-');
  }
  if (!definition.unit.isEmpty()) {
    value += " " + definition.unit;
  }
  return Success(value);
}

CsgUpstreamDecodedValue DecodeTime(
    const QByteArray &bytes,
    const CsgUpstreamFieldDef &definition) {
  QString sourceType = definition.sourceType;
  if (sourceType.isEmpty() || sourceType == "BCD_DATETIME") {
    sourceType = bytes.size() == 5 ? "YYMMDDhhmm"
                                   : "YYMMDDhhmmss";
  }
  const bool allFf = !bytes.isEmpty() &&
                     std::all_of(bytes.cbegin(), bytes.cend(), [](char byte) {
                       return static_cast<quint8>(byte) == 0xFF;
                     });
  const bool allZero = !bytes.isEmpty() &&
                       std::all_of(bytes.cbegin(), bytes.cend(), [](char byte) {
                         return static_cast<quint8>(byte) == 0x00;
                       });
  const bool containsDate = sourceType != "hhmm" && sourceType != "hhmmss";
  if (allFf || (allZero && containsDate)) {
    return Success("无数据");
  }

  QString error;
  if (!ValidateBcd(bytes, &error)) {
    return Failure(error);
  }
  const auto timeValue = [&bytes, &definition](int index) {
    const int byteIndex = definition.littleEndian
                              ? bytes.size() - 1 - index
                              : index;
    return BcdValue(bytes, byteIndex);
  };
  if (sourceType == "YYMMDDhhmm" ||
      sourceType == "YYMMDDhhmmss") {
    const int year = 2000 + timeValue(0);
    const int month = timeValue(1);
    const int day = timeValue(2);
    const int hour = timeValue(3);
    const int minute = timeValue(4);
    const bool hasSeconds = sourceType == "YYMMDDhhmmss";
    const int second = hasSeconds ? timeValue(5) : 0;
    if (!QDate(year, month, day).isValid()) {
      return Failure("日期无效");
    }
    if (hour > 23 || minute > 59 || second > 59) {
      return Failure("时间无效");
    }
    QString text = QString("%1-%2-%3 %4:%5")
                       .arg(year, 4, 10, QLatin1Char('0'))
                       .arg(month, 2, 10, QLatin1Char('0'))
                       .arg(day, 2, 10, QLatin1Char('0'))
                       .arg(hour, 2, 10, QLatin1Char('0'))
                       .arg(minute, 2, 10, QLatin1Char('0'));
    if (hasSeconds) {
      text += QString(":%1").arg(second, 2, 10, QLatin1Char('0'));
    }
    return Success(text);
  }
  if (sourceType == "YYYYMMDDhhmm") {
    const int year = timeValue(0) * 100 + timeValue(1);
    const int month = timeValue(2);
    const int day = timeValue(3);
    const int hour = timeValue(4);
    const int minute = timeValue(5);
    if (!QDate(year, month, day).isValid()) {
      return Failure("日期无效");
    }
    if (hour > 23 || minute > 59) {
      return Failure("时间无效");
    }
    return Success(QString("%1-%2-%3 %4:%5")
                       .arg(year, 4, 10, QLatin1Char('0'))
                       .arg(month, 2, 10, QLatin1Char('0'))
                       .arg(day, 2, 10, QLatin1Char('0'))
                       .arg(hour, 2, 10, QLatin1Char('0'))
                       .arg(minute, 2, 10, QLatin1Char('0')));
  }
  if (sourceType == "YYMMDD") {
    const int year = 2000 + timeValue(0);
    const int month = timeValue(1);
    const int day = timeValue(2);
    if (!QDate(year, month, day).isValid()) {
      return Failure("日期无效");
    }
    return Success(QString("%1-%2-%3")
                       .arg(year, 4, 10, QLatin1Char('0'))
                       .arg(month, 2, 10, QLatin1Char('0'))
                       .arg(day, 2, 10, QLatin1Char('0')));
  }
  if (sourceType == "MMDDhhmm") {
    const int month = timeValue(0);
    const int day = timeValue(1);
    const int hour = timeValue(2);
    const int minute = timeValue(3);
    if (!QDate(2000, month, day).isValid()) {
      return Failure("月日无效");
    }
    if (hour > 23 || minute > 59) {
      return Failure("时间无效");
    }
    return Success(QString("%1-%2 %3:%4")
                       .arg(month, 2, 10, QLatin1Char('0'))
                       .arg(day, 2, 10, QLatin1Char('0'))
                       .arg(hour, 2, 10, QLatin1Char('0'))
                       .arg(minute, 2, 10, QLatin1Char('0')));
  }
  if (sourceType == "DDhhmmss") {
    const int day = timeValue(0);
    const int hour = timeValue(1);
    const int minute = timeValue(2);
    const int second = timeValue(3);
    if (day < 1 || day > 31) {
      return Failure("日无效");
    }
    if (hour > 23 || minute > 59 || second > 59) {
      return Failure("时间无效");
    }
    return Success(QString("%1日 %2:%3:%4")
                       .arg(day, 2, 10, QLatin1Char('0'))
                       .arg(hour, 2, 10, QLatin1Char('0'))
                       .arg(minute, 2, 10, QLatin1Char('0'))
                       .arg(second, 2, 10, QLatin1Char('0')));
  }
  if (sourceType == "hhmm" || sourceType == "hhmmss") {
    const int hour = timeValue(0);
    const int minute = timeValue(1);
    const bool hasSeconds = sourceType == "hhmmss";
    const int second = hasSeconds ? timeValue(2) : 0;
    if (hour > 23 || minute > 59 || second > 59) {
      return Failure("时间无效");
    }
    QString text = QString("%1:%2")
                       .arg(hour, 2, 10, QLatin1Char('0'))
                       .arg(minute, 2, 10, QLatin1Char('0'));
    if (hasSeconds) {
      text += QString(":%1").arg(second, 2, 10, QLatin1Char('0'));
    }
    return Success(text);
  }
  return Failure("不支持的时间类型: " + sourceType);
}

CsgUpstreamDecodedValue DecodeBin(
    const QByteArray &bytes,
    const CsgUpstreamFieldDef &definition) {
  if (bytes.size() > static_cast<qsizetype>(sizeof(quint64))) {
    return Failure("BIN 字段长度超过 8 字节");
  }
  quint64 value = ReadUnsigned(bytes, definition.littleEndian);
  bool negative = false;
  if (definition.signMode == "msb" && !bytes.isEmpty()) {
    const int bitCount = static_cast<int>(bytes.size()) * 8;
    const quint64 signBit = quint64{1} << (bitCount - 1);
    negative = (value & signBit) != 0;
    value &= ~signBit;
  }

  if (definition.display == "ValidityFlag") {
    return Success(value == 0 ? "无效"
                              : value == 1
                                    ? "有效"
                                    : QString("未知有效性标志 %1").arg(value));
  }
  if (definition.display == "PeriodUnit") {
    static const QStringList units = {"分", "时", "日", "月"};
    return Success(value < static_cast<quint64>(units.size())
                       ? units.at(static_cast<qsizetype>(value))
                       : QString("未知周期单位 %1").arg(value));
  }
  if (definition.display == "DataStructureMode") {
    return Success(value == 0
                       ? "自描述格式"
                       : value == 1
                             ? "任务定义格式"
                             : QString("未知数据结构方式 %1").arg(value));
  }
  if (definition.display == "ConfirmationResult") {
    return Success(value == 0
                       ? "全部肯定"
                       : value == 1
                             ? "全部否定"
                             : QString("未知确认结果 %1").arg(value));
  }
  if (definition.display == "SecurityModuleFlag") {
    return Success(value == 0
                       ? "不具备"
                       : value == 1
                             ? "具备"
                             : QString("未知安全模块标志 %1").arg(value));
  }
  if (definition.display == "ArithmeticSign") {
    return Success(value == 0x00
                       ? "无效"
                       : value == 0x01
                             ? "加"
                             : value == 0x81
                                   ? "减"
                                   : QString("未知运算符号 %1").arg(value));
  }
  if (definition.display == "PortFunction") {
    static const QStringList functions = {
        "485抄表", "485总线级联", "485被抄",
        "485上传", "485任务数据发送"};
    return Success(value < static_cast<quint64>(functions.size())
                       ? functions.at(static_cast<qsizetype>(value))
                       : QString("未知端口功能 %1").arg(value));
  }
  if (definition.display == "BaudRate300") {
    return Success(QString("%1 bps").arg(value * 300));
  }
  if (definition.display == "Parity") {
    static const QStringList values = {"无校验", "偶校验", "奇校验"};
    return Success(value < static_cast<quint64>(values.size())
                       ? values.at(static_cast<qsizetype>(value))
                       : QString("未知校验方式 %1").arg(value));
  }
  if (definition.display == "DataBits") {
    return Success(QString("%1 位").arg(value));
  }
  if (definition.display == "StopBits") {
    static const QStringList values = {"1 位", "1.5 位", "2 位"};
    return Success(value < static_cast<quint64>(values.size())
                       ? values.at(static_cast<qsizetype>(value))
                       : QString("未知停止位 %1").arg(value));
  }
  if (definition.display == "AppControlState") {
    static const QStringList states = {"启动", "停止", "卸载"};
    return Success(value < static_cast<quint64>(states.size())
                       ? states.at(static_cast<qsizetype>(value))
                       : QString("未知 APP 控制状态 %1").arg(value));
  }
  if (definition.display == "AppRunState") {
    static const QMap<quint64, QString> states = {
        {1, "运行"}, {2, "停止"}, {3, "异常"}};
    const QString state = states.value(value);
    return Success(state.isEmpty()
                       ? QString("未知 APP 运行状态 %1").arg(value)
                       : state);
  }
  if (definition.display == "AlarmState") {
    return Success(value == 0x00
                       ? "告警恢复"
                       : value == 0x01
                             ? "告警发生"
                             : value == 0xFF
                                   ? "告警状态无效"
                                   : QString("未知告警状态 %1")
                                         .arg(value));
  }
  if (definition.display == "SecurityEventType") {
    static const QStringList types = {
        QString(), "以太网远程端口开放", "以太网外联信息",
        "SSH服务异常", "以太网登录控制台信息", "口令信息变更",
        "USB设备接入", "蓝牙接口设备接入"};
    const QString type =
        value < static_cast<quint64>(types.size())
            ? types.at(static_cast<qsizetype>(value))
            : QString();
    return Success(type.isEmpty()
                       ? QString("未知安全事件类型 %1").arg(value)
                       : type);
  }
  if (definition.display == "FileNature") {
    static const QMap<quint64, QString> names = {
        {0x00, "终端升级文件"},
        {0x01, "终端普通文件"},
        {0x02, "远程通信模块文件"},
        {0x03, "APP安装文件"},
        {0x0F, "清除正在传输的文件"},
        {0x11, "集中器本地通信模块文件"},
        {0x12, "从节点本地通信模块文件"},
        {0x13, "采集器文件"},
        {0x15, "电表升级文件"},
        {0x20, "RS-485模块"},
        {0x21, "遥信模块"},
        {0x22, "遥控模块"},
        {0x23, "分支监测模块"},
        {0x24, "M-Bus通信模块"},
        {0x25, "其他模块"},
    };
    const QString name = names.value(value);
    return Success(name.isEmpty()
                       ? QString("未知文件性质 %1").arg(value)
                       : name);
  }
  if (definition.display == "IPv4") {
    if (bytes.size() != 4) {
      return Failure("IPv4 必须为 4 字节");
    }
    return Success(QString("%1.%2.%3.%4")
                       .arg(ByteAt(bytes, 0))
                       .arg(ByteAt(bytes, 1))
                       .arg(ByteAt(bytes, 2))
                       .arg(ByteAt(bytes, 3)));
  }
  if (definition.display == "DA") {
    if (bytes.size() != 2) {
      return Failure("DA 必须为 2 字节");
    }
    return Success(DecodeDa(ByteAt(bytes, 0), ByteAt(bytes, 1)));
  }
  if (definition.display == "DI") {
    if (bytes.size() != 4) {
      return Failure("DI 必须为 4 字节");
    }
    const quint32 di = static_cast<quint32>(value);
    const DiLookup lookup = ResolveDiLookup(di);
    const auto &manager = CsgUpstreamConfigManager::instance();
    QString text = HexCode(di);
    const QString name = ResolveDiName(manager, lookup);
    if (!name.isEmpty()) {
      text += " " + name;
    } else if (manager.isLoaded()) {
      text += " 数据项暂未配置";
    } else {
      text += " 协议配置未加载";
    }
    return Success(text);
  }

  const QString magnitude = QString::number(value);
  return Success(negative ? "-" + magnitude : magnitude);
}

CsgUpstreamDecodedValue DecodeAscii(const QByteArray &bytes) {
  int textLength = bytes.size();
  while (textLength > 0 && bytes.at(textLength - 1) == '\0') {
    --textLength;
  }
  for (int index = 0; index < textLength; ++index) {
    const quint8 value = ByteAt(bytes, index);
    if (value < 0x20 || value > 0x7E) {
      return Failure("包含不可显示 ASCII 字节: " + HexByte(value));
    }
  }
  return Success(QString::fromLatin1(bytes.constData(), textLength));
}

CsgUpstreamDecodedValue DecodeBitString(
    const QByteArray &bytes,
    const CsgUpstreamFieldDef &definition) {
  if (definition.bits.isEmpty()) {
    return Success(QString("%1 位位串").arg(bytes.size() * 8));
  }
  QStringList values;
  for (const CsgUpstreamBitDef &bit : definition.bits) {
    const int byteIndex = bit.index / 8;
    const int bitIndex = bit.index % 8;
    const bool set = (ByteAt(bytes, byteIndex) & (1U << bitIndex)) != 0;
    values.append(bit.name + "=" +
                  (set ? bit.oneText : bit.zeroText));
  }
  return Success(values.join("；"));
}

} // namespace

CsgUpstreamDecodedValue CsgUpstreamValueDecoder::decode(
    const QByteArray &bytes,
    const CsgUpstreamFieldDef &definition) {
  if (definition.fillByte >= 0 && !bytes.isEmpty()) {
    const quint8 fillByte = static_cast<quint8>(definition.fillByte);
    const bool isFill = std::all_of(
        bytes.cbegin(), bytes.cend(), [fillByte](char byte) {
          return static_cast<quint8>(byte) == fillByte;
        });
    if (isFill) {
      return Success("无数据");
    }
  }

  switch (definition.type) {
  case CsgUpstreamFieldType::Bcd:
    return DecodeBcd(bytes, definition);
  case CsgUpstreamFieldType::BcdDateTime:
    return DecodeTime(bytes, definition);
  case CsgUpstreamFieldType::Bin:
    return DecodeBin(bytes, definition);
  case CsgUpstreamFieldType::Ascii:
    return DecodeAscii(bytes);
  case CsgUpstreamFieldType::BitString:
    return DecodeBitString(bytes, definition);
  case CsgUpstreamFieldType::Blob:
    return Success(QString());
  case CsgUpstreamFieldType::Enum: {
    if (bytes.size() > static_cast<qsizetype>(sizeof(quint64))) {
      return Failure("enum 字段长度超过 8 字节");
    }
    const quint64 value =
        ReadUnsigned(bytes, definition.littleEndian);
    const QString name = definition.enumValues.value(value);
    return Success(name.isEmpty()
                       ? QString("%1 (未知枚举值)").arg(value)
                       : QString("%1 (%2)").arg(value).arg(name));
  }
  case CsgUpstreamFieldType::Struct:
  case CsgUpstreamFieldType::Choice:
  case CsgUpstreamFieldType::DataItemGroups:
  case CsgUpstreamFieldType::ReferencedDataItems:
    return Failure("复合字段必须由字段解析器处理");
  }
  return Failure("未知字段类型");
}
