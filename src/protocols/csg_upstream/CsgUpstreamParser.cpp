#include "CsgUpstreamParser.h"

#include "CsgUpstreamConfigManager.h"
#include "CsgUpstreamFieldParser.h"
#include "CsgUpstreamServiceParser.h"

#include <QStringList>

namespace {

constexpr int kControlOffset = 6;
constexpr int kAddressOffset = 7;
constexpr int kAfnOffset = 14;
constexpr int kSeqOffset = 15;
constexpr int kDaOffset = 16;
constexpr int kDiOffset = 18;
constexpr int kDataOffset = 22;
constexpr int kMinimumUserLength = 16;
constexpr int kEnvelopeHeaderLength = 4;
constexpr int kEnvelopeOverhead = 5;

quint8 ByteAt(const QByteArray &data, int offset) {
  return static_cast<quint8>(data.at(offset));
}

QString HexByte(quint8 value) {
  return QString("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper();
}

QString HexBytes(const QByteArray &data) {
  return QString::fromLatin1(data.toHex()).toUpper();
}

QString ReverseBcdText(const QByteArray &data) {
  QByteArray reversed;
  reversed.reserve(data.size());
  for (auto it = data.crbegin(); it != data.crend(); ++it) {
    reversed.append(*it);
  }
  return HexBytes(reversed);
}

quint32 ReadLittleEndian32(const QByteArray &data, int offset) {
  quint32 value = 0;
  for (int index = 0; index < 4; ++index) {
    value |= static_cast<quint32>(ByteAt(data, offset + index))
             << (index * 8);
  }
  return value;
}

QString HexCode(quint32 value, int width) {
  return QString("%1").arg(value, width, 16, QLatin1Char('0')).toUpper();
}

QString ComposeDescription(const QString &parsed,
                           const QString &note) {
  if (parsed.isEmpty()) {
    return note;
  }
  return note.isEmpty() ? parsed : parsed + "（" + note + "）";
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
    return {HexCode(series, 8).left(6) + "XX",
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
      return {HexCode(series | rangeStart, 8),
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
      return {HexCode(series | rangeStart, 8),
              QString("（时段 %1）").arg(itemNumber - rangeStart + 1)};
    }
  }
  if (series == 0xE0000E00U &&
      itemNumber >= 0x21 && itemNumber <= 0x2C) {
    return {"E0000E21",
            QString("（轮次 %1）").arg(itemNumber - 0x20)};
  }
  return {HexCode(di, 8), QString()};
}

QString ResolveDiName(const CsgUpstreamConfigManager &manager,
                      const DiLookup &lookup) {
  QString name = manager.getDiName(lookup.key);
  if (!name.isEmpty()) {
    name += lookup.nameSuffix;
  }
  return name;
}

} // namespace

int CsgUpstreamParser::getFrameLength(const QByteArray &data) {
  if (data.size() >= kEnvelopeHeaderLength &&
      ByteAt(data, 0) == 0x88) {
    const quint16 payloadLength =
        ByteAt(data, 2) |
        (static_cast<quint16>(ByteAt(data, 3)) << 8);
    return static_cast<int>(payloadLength) + kEnvelopeOverhead;
  }
  if (data.size() < 5 || ByteAt(data, 0) != 0x68) {
    return 0;
  }

  const quint16 firstLength =
      ByteAt(data, 1) |
      (static_cast<quint16>(ByteAt(data, 2)) << 8);
  const quint16 repeatedLength =
      ByteAt(data, 3) |
      (static_cast<quint16>(ByteAt(data, 4)) << 8);
  if (firstLength != repeatedLength ||
      firstLength < kMinimumUserLength) {
    return 0;
  }
  return static_cast<int>(firstLength) + 8;
}

bool CsgUpstreamParser::isValidFrame(const QByteArray &data) {
  const int frameLength = getFrameLength(data);
  if (frameLength == 0 || data.size() < frameLength) {
    return false;
  }
  if (ByteAt(data, 0) == 0x88) {
    return frameLength >= kEnvelopeOverhead &&
           ByteAt(data, frameLength - 1) == 0x77;
  }
  if (ByteAt(data, 5) != 0x68) {
    return false;
  }

  const quint16 userLength =
      ByteAt(data, 1) |
      (static_cast<quint16>(ByteAt(data, 2)) << 8);
  const int checksumOffset = 6 + userLength;
  if (calculateChecksum(data, 6, userLength) !=
      ByteAt(data, checksumOffset)) {
    return false;
  }
  return ByteAt(data, checksumOffset + 1) == 0x16;
}

QList<CsgUpstreamField>
CsgUpstreamParser::parse(const QByteArray &data) const {
  QList<CsgUpstreamField> fields;
  if (!isValidFrame(data)) {
    return fields;
  }
  if (ByteAt(data, 0) == 0x88) {
    const quint8 flags = ByteAt(data, 1);
    const quint16 payloadLength =
        ByteAt(data, 2) |
        (static_cast<quint16>(ByteAt(data, 3)) << 8);
    const bool compressed = (flags & 0x01U) != 0;
    fields.append({"报头标志", "88", "附录 I 固定报头", 0, 1, 0});
    fields.append({"加密压缩算法操作标志", HexByte(flags),
                   "D0 压缩；D1 完整性验证；D2～D7 保留",
                   1, 1, 0});
    fields.append({"压缩标志", QString::number(flags & 0x01U),
                   compressed ? "压缩" : "不压缩", 1, 1, 1});
    fields.append({"完整性验证标志",
                   QString::number((flags >> 1) & 0x01U),
                   (flags & 0x02U) != 0 ? "有完整性验证"
                                        : "无完整性验证",
                   1, 1, 1});
    fields.append({"数据长度", HexBytes(data.mid(2, 2)),
                   QString("%1 字节，小端 BIN").arg(payloadLength),
                   2, 2, 0});
    fields.append({"封装载荷",
                   HexBytes(data.mid(kEnvelopeHeaderLength,
                                     payloadLength)),
                   compressed ? "压缩载荷未展开"
                              : "封装载荷按原文保留",
                   kEnvelopeHeaderLength, payloadLength, 0});
    fields.append({"报尾标志", "77", "附录 I 固定报尾",
                   kEnvelopeHeaderLength + payloadLength, 1, 0});
    return fields;
  }

  const quint16 userLength =
      ByteAt(data, 1) |
      (static_cast<quint16>(ByteAt(data, 2)) << 8);
  const int checksumOffset = 6 + userLength;
  const quint8 control = ByteAt(data, kControlOffset);
  const quint8 afn = ByteAt(data, kAfnOffset);
  const quint8 seq = ByteAt(data, kSeqOffset);
  const quint8 da1 = ByteAt(data, kDaOffset);
  const quint8 da2 = ByteAt(data, kDaOffset + 1);
  const quint32 di = ReadLittleEndian32(data, kDiOffset);
  const int payloadLength = static_cast<int>(userLength) -
                            kMinimumUserLength;
  const QByteArray payload = data.mid(kDataOffset, payloadLength);
  const int applicationDataLength =
      static_cast<int>(userLength) - (kDaOffset - kControlOffset);
  const QByteArray applicationData =
      data.mid(kDaOffset, applicationDataLength);

  fields.append({"起始字符", "68", "固定帧起始字符", 0, 1, 0});
  fields.append({"长度 L",
                 HexBytes(data.mid(1, 2)),
                 ComposeDescription(QString::number(userLength),
                                    "用户数据区长度"),
                 1, 2, 0});
  fields.append({"重复长度 L",
                 HexBytes(data.mid(3, 2)),
                 ComposeDescription(QString::number(userLength),
                                    "与首个长度字段一致"),
                 3, 2, 0});
  fields.append({"第二起始字符", "68", "固定帧起始字符", 5, 1, 0});
  fields.append({"控制域 C", HexByte(control), "链路控制域",
                 kControlOffset, 1, 0});
  fields.append({"传输方向 DIR",
                 QString::number((control >> 7) & 0x01),
                 (control & 0x80) ? "上行" : "下行",
                 kControlOffset, 1, 1});
  fields.append({"启动标志 PRM",
                 QString::number((control >> 6) & 0x01), QString(),
                 kControlOffset, 1, 1});
  fields.append({"帧计数位 FCB/ACD",
                 QString::number((control >> 5) & 0x01), QString(),
                 kControlOffset, 1, 1});
  fields.append({"帧计数有效位 FCV/保留",
                 QString::number((control >> 4) & 0x01), QString(),
                 kControlOffset, 1, 1});
  fields.append({"功能码",
                 QString::number(control & 0x0F), QString(),
                 kControlOffset, 1, 1});

  const QByteArray a1Bytes = data.mid(kAddressOffset, 3);
  fields.append({"行政区划码 A1", HexBytes(a1Bytes),
                 ComposeDescription(ReverseBcdText(a1Bytes),
                                    "BCD 低字节在前"),
                 kAddressOffset, 3, 0});
  const quint32 terminalAddress =
      ByteAt(data, kAddressOffset + 3) |
      (static_cast<quint32>(ByteAt(data, kAddressOffset + 4)) << 8) |
      (static_cast<quint32>(ByteAt(data, kAddressOffset + 5)) << 16);
  fields.append({"终端地址 A2",
                 HexBytes(data.mid(kAddressOffset + 3, 3)),
                 ComposeDescription(
                     QString("%1 (0x%2)")
                         .arg(terminalAddress)
                         .arg(terminalAddress, 6, 16,
                              QLatin1Char('0'))
                         .toUpper(),
                     "三字节小端 BIN"),
                 kAddressOffset + 3, 3, 0});
  fields.append({"主站地址 A3", HexByte(ByteAt(data, 13)),
                 QString::number(ByteAt(data, 13)), 13, 1, 0});

  const auto &manager = CsgUpstreamConfigManager::instance();
  const CsgUpstreamAfnDef *afnDefinition = manager.getAfnDef(afn);
  const QString afnName = afnDefinition ? afnDefinition->name : QString();
  fields.append({"应用功能码 AFN", HexByte(afn),
                 afnName.isEmpty() ? "AFN 暂未配置" : afnName,
                 kAfnOffset, 1, 0});
  fields.append({"序列域 SEQ", HexByte(seq), "序列控制域",
                 kSeqOffset, 1, 0});
  fields.append({"时标有效位 TpV", QString::number((seq >> 7) & 0x01),
                 QString(), kSeqOffset, 1, 1});
  fields.append({"首帧标志 FIR", QString::number((seq >> 6) & 0x01),
                 QString(), kSeqOffset, 1, 1});
  fields.append({"末帧标志 FIN", QString::number((seq >> 5) & 0x01),
                 QString(), kSeqOffset, 1, 1});
  fields.append({"确认标志 CON", QString::number((seq >> 4) & 0x01),
                 QString(), kSeqOffset, 1, 1});
  fields.append({"帧序号 PSEQ/RSEQ", QString::number(seq & 0x0F),
                 QString(), kSeqOffset, 1, 1});

  const QString serviceDirection =
      (control & 0x80) != 0 ? "up" : "down";
  const CsgUpstreamServiceDef *serviceDefinition = nullptr;
  if (afnDefinition) {
    const auto serviceIt =
        afnDefinition->services.constFind(serviceDirection);
    if (serviceIt != afnDefinition->services.cend()) {
      serviceDefinition = &serviceIt.value();
    }
  }

  const DiLookup diLookup = ResolveDiLookup(di);
  const CsgUpstreamDiDef *diDefinition =
      manager.getDiDef(diLookup.key);
  if (!serviceDefinition) {
    fields.append({"信息点标识 DA",
                   HexByte(da1) + HexByte(da2),
                   decodeDa(da1, da2), kDaOffset, 2, 0});
    QString diDescription = HexCode(di, 8);
    if (diDefinition) {
      diDescription += " " + ResolveDiName(manager, diLookup);
    } else if (manager.isLoaded()) {
      diDescription += " 数据项暂未配置";
    } else {
      diDescription += " 协议配置未加载";
    }
    fields.append({"数据标识 DI", HexBytes(data.mid(kDiOffset, 4)),
                   diDescription, kDiOffset, 4, 0});
  }

  if (serviceDefinition) {
    QString serviceError;
    if (!CsgUpstreamServiceParser::parse(
            applicationData, afn, seq, *serviceDefinition, kDaOffset,
            &fields, &serviceError)) {
      const QByteArray rawServiceData = applicationData;
      const int rawServiceOffset = kDaOffset;
      const int rawServiceLength =
          static_cast<int>(rawServiceData.size());
      fields.append({"数据项解析错误", QString(),
                     ComposeDescription(
                         serviceError,
                         "链路帧有效，Service 按原始内容保留"),
                     rawServiceOffset, rawServiceLength, 1});
      fields.append({"未解析应用数据", HexBytes(rawServiceData),
                     "完整 Service 内容", rawServiceOffset,
                     rawServiceLength, 1});
    }
  } else {
    bool payloadError = false;
    QString payloadErrorMessage;
    if (!diDefinition) {
      payloadError = true;
      payloadErrorMessage = manager.isLoaded()
                                ? "数据项暂未配置"
                                : "协议配置未加载";
    } else if (diDefinition->type == "None") {
      if (!payload.isEmpty()) {
        payloadError = true;
        payloadErrorMessage = "None 类型数据项包含了额外数据";
      }
    } else if (diDefinition->type == "ServiceDefined") {
      payloadError = true;
      payloadErrorMessage = "当前 AFN 和方向未配置 Service 结构";
    } else {
      int cursor = 0;
      payloadError = !CsgUpstreamFieldParser::parse(
          payload, diDefinition->fields, &cursor, kDataOffset,
          QString(), 1, &fields, &payloadErrorMessage);

      if (!payloadError && cursor != payload.size()) {
        payloadError = true;
        payloadErrorMessage =
            QString("数据项存在 %1 字节未定义数据")
                .arg(payload.size() - cursor);
      }
    }

    if (payloadError) {
      fields.append({"数据项解析错误", QString(),
                     ComposeDescription(
                         payloadErrorMessage,
                         "链路帧有效，数据项按原始内容保留"),
                     kDataOffset, payloadLength, 1});
      fields.append({"未解析应用数据", HexBytes(payload),
                     "完整数据项内容", kDataOffset, payloadLength, 1});
    }
  }

  const quint8 receivedChecksum = ByteAt(data, checksumOffset);
  const quint8 calculatedChecksum =
      calculateChecksum(data, 6, userLength);
  fields.append({"校验和 CS", HexByte(receivedChecksum),
                 ComposeDescription(
                     "计算 " + HexByte(calculatedChecksum) +
                         "，校验通过",
                     "用户数据区八位算术和"),
                 checksumOffset, 1, 0});
  fields.append({"结束字符", HexByte(ByteAt(data, checksumOffset + 1)),
                 "固定 16H", checksumOffset + 1, 1, 0});
  return fields;
}

ProtocolSummary CsgUpstreamParser::getSummary(const QByteArray &data) {
  ProtocolSummary summary;
  if (!isValidFrame(data)) {
    return summary;
  }
  if (ByteAt(data, 0) == 0x88) {
    summary.direction = "封装";
    summary.messageType = "附录 I 压缩/加密外层";
    return summary;
  }

  const quint8 control = ByteAt(data, kControlOffset);
  const quint8 afn = ByteAt(data, kAfnOffset);
  const quint8 seq = ByteAt(data, kSeqOffset);
  const quint32 di = ReadLittleEndian32(data, kDiOffset);
  const QString areaCode =
      ReverseBcdText(data.mid(kAddressOffset, 3));
  const quint32 terminalAddress =
      ByteAt(data, kAddressOffset + 3) |
      (static_cast<quint32>(ByteAt(data, kAddressOffset + 4)) << 8) |
      (static_cast<quint32>(ByteAt(data, kAddressOffset + 5)) << 16);

  const auto &manager = CsgUpstreamConfigManager::instance();
  const CsgUpstreamAfnDef *afnDefinition = manager.getAfnDef(afn);
  const DiLookup diLookup = ResolveDiLookup(di);
  const CsgUpstreamDiDef *diDefinition =
      manager.getDiDef(diLookup.key);
  const QString afnName = afnDefinition ? afnDefinition->name : QString();
  const QString diName = ResolveDiName(manager, diLookup);

  summary.direction = (control & 0x80) ? "上行" : "下行";
  summary.address = QString("A1:%1 A2:%2")
                        .arg(areaCode)
                        .arg(terminalAddress);
  summary.frameSeq = QString::number(seq & 0x0F);
  summary.messageType =
      QString("AFN %1 %2 | DI %3 %4")
          .arg(HexByte(afn))
          .arg(afnName)
          .arg(HexCode(di, 8))
          .arg(diName)
          .simplified();
  if (!diDefinition) {
    summary.messageType += " 暂未展开";
  }
  return summary;
}

quint8 CsgUpstreamParser::calculateChecksum(const QByteArray &data,
                                            int offset, int length) {
  quint8 checksum = 0;
  for (int index = 0; index < length; ++index) {
    checksum = static_cast<quint8>(
        checksum + ByteAt(data, offset + index));
  }
  return checksum;
}

QString CsgUpstreamParser::decodeDa(quint8 da1, quint8 da2) {
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
